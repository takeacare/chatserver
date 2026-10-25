#include "chatservice.hpp"
#include "password.hpp"
#include "public.hpp"
#include "serverconfig.hpp"
#include <muduo/base/Logging.h>
#include <cctype>
#include <vector>
using namespace std;
using namespace muduo;

// 安全读取字符串字段：字段缺失或类型不符时返回空串
static string jsonString(const json &js, const char *key)
{
    auto it = js.find(key);
    if (it == js.end() || !it->is_string())
    {
        return "";
    }
    return it->get<string>();
}

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 按分帧协议向连接发送JSON
void ChatService::sendJson(const TcpConnectionPtr &conn, const json &js)
{
    conn->send(encodeMessage(js.dump()));
}

// 从连接上下文取出登录后写入的用户id；未登录返回false
bool ChatService::getAuthUserId(const TcpConnectionPtr &conn, int &userid)
{
    const boost::any &ctx = conn->getContext();
    const int *pid = boost::any_cast<int>(&ctx);
    if (pid == nullptr || *pid <= 0)
    {
        return false;
    }
    userid = *pid;
    return true;
}

// 发送私聊/群聊发送失败ACK
void ChatService::sendChatAck(const TcpConnectionPtr &conn, int errno_, const string &msg)
{
    json response;
    response["msgid"] = CHAT_SEND_ACK;
    response["errno"] = errno_;
    response["errmsg"] = msg;
    sendJson(conn, response);
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器（连接参数来自配置文件）
    const ServerConfig &cfg = ServerConfig::instance();
    if (_redis.connect(cfg.redisHost(), cfg.redisPort()))
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp) {
            LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务  email  password
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string email = jsonString(js, "email");
    string pwd = jsonString(js, "password");

    auto reply = [&](int errno_, const string &msg) {
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = errno_;
        response["errmsg"] = msg;
        sendJson(conn, response);
    };

    // 暴力破解防护：锁定key用小写化邮箱，防止大小写变体绕过
    string attemptKey;
    attemptKey.reserve(email.size());
    for (char ch : email)
    {
        attemptKey.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
    }

    // 先检查是否处于锁定期
    {
        lock_guard<mutex> lock(_attemptMutex);
        auto it = _loginAttempts.find(attemptKey);
        if (it != _loginAttempts.end())
        {
            const auto now = chrono::steady_clock::now();
            if (now < it->second.lockUntil)
            {
                const int remain = static_cast<int>(
                                       chrono::duration_cast<chrono::seconds>(it->second.lockUntil - now).count()) + 1;
                reply(3, "account locked, retry after " + to_string(remain) + " seconds");
                return;
            }
        }
    }

    User user;
    if (isValidEmail(email))
    {
        user = _userModel.queryByEmail(email);
    }

    // 邮箱不存在或密码错误，统一提示，避免暴露账号是否存在
    if (user.getId() == -1 || !pwd::verifyPassword(user.getPwd(), pwd))
    {
        // 记录失败次数，达到上限后临时锁定该账号
        {
            lock_guard<mutex> lock(_attemptMutex);
            LoginAttempt &attempt = _loginAttempts[attemptKey];
            if (++attempt.fails >= MAX_LOGIN_FAILS)
            {
                attempt.lockUntil = chrono::steady_clock::now() + chrono::seconds(LOGIN_LOCK_SECONDS);
                attempt.fails = 0;
            }
        }
        reply(1, "email or password is invalid!");
        return;
    }

    if (user.getState() == "online")
    {
        // 该用户已经登录，不允许重复登录
        reply(2, "this account is using, input another!");
        return;
    }

    const int id = user.getId();

    // 登录成功，清除失败计数
    {
        lock_guard<mutex> lock(_attemptMutex);
        _loginAttempts.erase(attemptKey);
    }

    // 旧明文密码在登录成功后升级为加盐哈希存储
    if (!pwd::isHashed(user.getPwd()))
    {
        _userModel.updatePassword(id, pwd::hashPassword(pwd));
    }

    // 登录成功，记录用户连接信息
    {
        lock_guard<mutex> lock(_connMutex);
        _userConnMap.insert({id, conn});
    }

    // 连接上下文写入用户id，作为该连接的鉴权标记
    conn->setContext(id);

    // 向redis订阅channel(id)，阻塞等待订阅确认，保证跨服务器消息通道就绪
    _redis.subscribe(id);

    // 登录成功，更新用户状态信息 state offline=>online
    user.setState("online");
    _userModel.updateState(user);

    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    response["errno"] = 0;
    response["id"] = user.getId();
    response["name"] = user.getName();
    response["email"] = user.getEmail();

    // 事务方式取出并清空该用户的离线消息，避免查询与删除之间新消息丢失
    vector<string> vec = _offlineMsgModel.take(id);
    if (!vec.empty())
    {
        response["offlinemsg"] = vec;
    }

    // 查询该用户的好友信息并返回
    vector<User> userVec = _friendModel.query(id);
    if (!userVec.empty())
    {
        vector<string> vec2;
        for (User &user : userVec)
        {
            json js;
            js["id"] = user.getId();
            js["name"] = user.getName();
            js["email"] = user.getEmail();
            js["state"] = user.getState();
            vec2.push_back(js.dump());
        }
        response["friends"] = vec2;
    }

    // 查询用户的群组信息
    vector<Group> groupuserVec = _groupModel.queryGroups(id);
    if (!groupuserVec.empty())
    {
        // group:[{groupid:[xxx, xxx, xxx, xxx]}]
        vector<string> groupV;
        for (Group &group : groupuserVec)
        {
            json grpjson;
            grpjson["id"] = group.getId();
            grpjson["groupname"] = group.getName();
            grpjson["groupdesc"] = group.getDesc();
            vector<string> userV;
            for (GroupUser &user : group.getUsers())
            {
                json js;
                js["id"] = user.getId();
                js["name"] = user.getName();
                js["state"] = user.getState();
                js["role"] = user.getRole();
                userV.push_back(js.dump());
            }
            grpjson["users"] = userV;
            groupV.push_back(grpjson.dump());
        }

        response["groups"] = groupV;
    }

    sendJson(conn, response);
}

// 处理注册业务  name  email  password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = jsonString(js, "name");
    string email = jsonString(js, "email");
    string pwd = jsonString(js, "password");

    auto reply = [&](int errno_, const string &msg) {
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = errno_;
        response["errmsg"] = msg;
        sendJson(conn, response);
    };

    if (name.empty() || pwd.empty())
    {
        reply(2, "username and password can not be empty");
        return;
    }
    if (!isValidEmail(email))
    {
        reply(2, "email format is invalid");
        return;
    }

    User user;
    user.setName(name);
    user.setEmail(email);
    // 密码加盐哈希后入库，不保存明文
    user.setPwd(pwd::hashPassword(pwd));

    if (!_userModel.insert(user))
    {
        // 用户名或邮箱与已有账号冲突
        reply(1, "username or email already exists");
        return;
    }

    json response;
    response["msgid"] = REG_MSG_ACK;
    response["errno"] = 0;
    response["id"] = user.getId();
    response["email"] = email;
    sendJson(conn, response);
}

// 注销公共逻辑：移除连接映射、取消订阅、更新离线状态
void ChatService::doLogout(const TcpConnectionPtr &conn, int userid)
{
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end() && it->second == conn)
        {
            _userConnMap.erase(it);
        }
    }
    conn->setContext(boost::any());

    // 先确认取消订阅，再更新离线状态：注销返回后不会再有该通道的消息到达
    _redis.unsubscribe(userid);

    User user(userid, "", "", "offline");
    _userModel.updateState(user);
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 发送者身份以连接上下文为准，不信任请求体中的id
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "loginout rejected: unauthenticated connection";
        return;
    }
    doLogout(conn, userid);
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    // 连接上下文直接给出userid，O(1)定位，无需遍历反查
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        return; // 未登录过的连接无需清理
    }
    doLogout(conn, userid);
}

// 一对一聊天业务：优先使用内部 toid，也支持按邮箱/用户名（toaccount）定位
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // 未登录的连接不允许发送任何消息，防止伪造他人身份
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "oneChat rejected: unauthenticated connection";
        return;
    }

    // 消息内容长度限制，防止超大消息拖垮存储与转发链路
    const string text = jsonString(js, "msg");
    if (text.empty() || text.size() > MAX_TEXT_LEN)
    {
        sendChatAck(conn, 2, "message is empty or exceeds length limit");
        return;
    }

    int toid = -1;
    if (js.contains("toid") && js["toid"].is_number_integer())
    {
        toid = js["toid"].get<int>();
    }
    else
    {
        const string account = jsonString(js, "toaccount");
        User target;
        if (isValidEmail(account))
        {
            target = _userModel.queryByEmail(account);
        }
        else if (!account.empty())
        {
            target = _userModel.queryByName(account);
        }
        toid = target.getId();
    }

    if (toid <= 0)
    {
        sendChatAck(conn, 1, "target user not found");
        return;
    }

    // 发送者身份以连接上下文为准，覆盖请求体中的id/name，防止伪造
    js["id"] = userid;
    js["toid"] = toid;

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            sendJson(it->second, js);
            return;
        }
    }

    // 查询toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

// 添加好友业务 msgid account（account 为邮箱或用户名）
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "addFriend rejected: unauthenticated connection";
        return;
    }

    const string account = jsonString(js, "account");

    auto reply = [&](int errno_, const string &msg) {
        json response;
        response["msgid"] = ADD_FRIEND_MSG_ACK;
        response["errno"] = errno_;
        response["errmsg"] = msg;
        sendJson(conn, response);
    };

    if (account.empty())
    {
        reply(3, "email or username can not be empty");
        return;
    }

    User target;
    if (isValidEmail(account))
    {
        target = _userModel.queryByEmail(account);
    }
    else
    {
        target = _userModel.queryByName(account);
    }

    if (target.getId() == -1)
    {
        reply(1, "user not found");
        return;
    }
    if (target.getId() == userid)
    {
        reply(2, "can not add yourself as friend");
        return;
    }

    // 存储好友信息
    _friendModel.insert(userid, target.getId());

    json response;
    response["msgid"] = ADD_FRIEND_MSG_ACK;
    response["errno"] = 0;
    response["name"] = target.getName();
    response["email"] = target.getEmail();
    sendJson(conn, response);
}

// 创建群组业务 msgid groupname [groupdesc]
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "createGroup rejected: unauthenticated connection";
        return;
    }

    const string name = jsonString(js, "groupname");
    const string desc = jsonString(js, "groupdesc");

    auto reply = [&](int errno_, const string &msg) {
        json response;
        response["msgid"] = CREATE_GROUP_MSG_ACK;
        response["errno"] = errno_;
        response["errmsg"] = msg;
        sendJson(conn, response);
    };

    if (name.empty())
    {
        reply(2, "group name can not be empty");
        return;
    }
    if (name.size() > 64 || desc.size() > 200)
    {
        reply(2, "group name or desc too long");
        return;
    }

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");

        json response;
        response["msgid"] = CREATE_GROUP_MSG_ACK;
        response["errno"] = 0;
        response["groupname"] = name;
        sendJson(conn, response);
    }
    else
    {
        reply(1, "create group failed");
    }
}

// 加入群组业务 msgid groupname [candidate]
// candidate 为同名群的序号（0 起），仅用于区分同名群，不是群 ID；缺省且同名时返回候选列表
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "addGroup rejected: unauthenticated connection";
        return;
    }

    const string groupname = jsonString(js, "groupname");

    auto reply = [&](int errno_, const string &msg) {
        json response;
        response["msgid"] = ADD_GROUP_MSG_ACK;
        response["errno"] = errno_;
        response["errmsg"] = msg;
        sendJson(conn, response);
    };

    if (groupname.empty())
    {
        reply(3, "group name can not be empty");
        return;
    }

    vector<Group> matches = _groupModel.queryGroupsByName(groupname);
    if (matches.empty())
    {
        reply(1, "group not found");
        return;
    }

    int candidate = -1;
    if (js.contains("candidate") && js["candidate"].is_number_integer())
    {
        candidate = js["candidate"].get<int>();
    }

    if (candidate < 0 && matches.size() > 1)
    {
        // 同名群较多：返回候选列表，由客户端选择序号后重发
        json response;
        response["msgid"] = ADD_GROUP_MSG_ACK;
        response["errno"] = 2;
        response["errmsg"] = "multiple groups found, choose one";
        vector<string> vec;
        for (Group &group : matches)
        {
            json jsGroup;
            jsGroup["name"] = group.getName();
            jsGroup["desc"] = group.getDesc();
            vec.push_back(jsGroup.dump());
        }
        response["groups"] = vec;
        sendJson(conn, response);
        return;
    }

    if (candidate < 0)
    {
        candidate = 0;
    }
    if (candidate >= static_cast<int>(matches.size()))
    {
        reply(2, "invalid group index");
        return;
    }

    Group &group = matches[candidate];
    _groupModel.addGroup(userid, group.getId(), "normal");

    json response;
    response["msgid"] = ADD_GROUP_MSG_ACK;
    response["errno"] = 0;
    response["groupname"] = group.getName();
    sendJson(conn, response);
}

// 群组聊天业务：优先使用内部 groupid，也支持按群名称（groupname）定位
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = -1;
    if (!getAuthUserId(conn, userid))
    {
        LOG_ERROR << "groupChat rejected: unauthenticated connection";
        return;
    }

    // 消息内容长度限制
    const string text = jsonString(js, "msg");
    if (text.empty() || text.size() > MAX_TEXT_LEN)
    {
        sendChatAck(conn, 2, "message is empty or exceeds length limit");
        return;
    }

    int groupid = -1;
    if (js.contains("groupid") && js["groupid"].is_number_integer())
    {
        groupid = js["groupid"].get<int>();
    }
    else
    {
        const string groupname = jsonString(js, "groupname");
        vector<Group> matches = _groupModel.queryGroupsByName(groupname);
        int candidate = 0;
        if (js.contains("candidate") && js["candidate"].is_number_integer())
        {
            candidate = js["candidate"].get<int>();
        }
        if (!matches.empty() && candidate >= 0 && candidate < static_cast<int>(matches.size()))
        {
            groupid = matches[candidate].getId();
        }
    }

    if (groupid <= 0)
    {
        sendChatAck(conn, 1, "group not found");
        return;
    }

    // 仅群组成员可以发言
    if (!_groupModel.isMember(userid, groupid))
    {
        sendChatAck(conn, 3, "you are not a member of this group");
        return;
    }

    // 发送者身份以连接上下文为准
    js["id"] = userid;
    js["groupid"] = groupid;

    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            sendJson(it->second, js);
        }
        else
        {
            // 查询toid是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(encodeMessage(msg));
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}
