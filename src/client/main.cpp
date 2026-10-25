#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <unordered_map>
#include <functional>
using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

// 控制主菜单页面程序
bool isMainMenuRunning = false;

// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态
atomic_bool g_isLoginSuccess{false};


// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 循环发送直到数据全部写完，处理短写与EINTR
static bool sendAll(int fd, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (n == 0)
        {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// 按分帧协议发送一条JSON
static bool sendFrame(int fd, const std::string &json)
{
    return sendAll(fd, encodeMessage(json));
}

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程通信用的信号量
    sem_init(&rwsem, 0, 0);

    // 连接服务器成功，启动接收子线程
    std::thread readTask(readTaskHandler, clientfd); // pthread_create
    readTask.detach();                               // pthread_detach

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            char email[120] = {0};
            char pwd[50] = {0};
            cout << "email:";
            cin.getline(email, sizeof(email));
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["email"] = email;
            js["password"] = pwd;
            string request = js.dump();

            g_isLoginSuccess = false;

            if (!sendFrame(clientfd, request))
            {
                cerr << "send login msg error:" << request << endl;
            }

            sem_wait(&rwsem); // 等待信号量，由子线程处理完登录的响应消息后，通知这里
                
            if (g_isLoginSuccess) 
            {
                // 进入聊天主菜单页面
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char email[120] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "email:";
            cin.getline(email, sizeof(email));
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["email"] = email;
            js["password"] = pwd;
            string request = js.dump();

            if (!sendFrame(clientfd, request))
            {
                cerr << "send reg msg error:" << request << endl;
            }
            
            sem_wait(&rwsem); // 等待信号量，子线程处理完注册消息会通知
        }
        break;
        case 3: // quit业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

// 处理注册的响应逻辑
void doRegResponse(json &responsejs)
{
    if (0 != responsejs.value("errno", 1)) // 注册失败
    {
        int errno_ = responsejs.value("errno", 1);
        if (errno_ == 1)
        {
            cerr << "username or email is already exist, register error!" << endl;
        }
        else
        {
            cerr << "register error: " << responsejs.value("errmsg", string("invalid input")) << endl;
        }
    }
    else // 注册成功
    {
        cout << "register success, login with email " << responsejs.value("email", string()) << endl;
    }
}

// 处理登录的响应逻辑
void doLoginResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // 登录失败
    {
        cerr << responsejs.value("errmsg", string("login failed")) << endl;
        g_isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 记录当前用户的id和name、邮箱
        g_currentUser.setId(responsejs["id"].get<int>());
        g_currentUser.setName(responsejs["name"]);
        g_currentUser.setEmail(responsejs.value("email", string()));

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            // 初始化
            g_currentUserFriendList.clear();

            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setEmail(js.value("email", string()));
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }

        // 记录当前用户的群组列表信息
        if (responsejs.contains("groups"))
        {
            // 初始化
            g_currentUserGroupList.clear();

            vector<string> vec1 = responsejs["groups"];
            for (string &groupstr : vec1)
            {
                json grpjs = json::parse(groupstr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string &userstr : vec2)
                {
                    GroupUser user;
                    json js = json::parse(userstr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);
                    group.getUsers().push_back(user);
                }

                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();

        // 显示当前用户的离线消息  个人聊天信息或者群组消息
        if (responsejs.contains("offlinemsg"))
        {
            vector<string> vec = responsejs["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + name + " said: " + xxx
                if (ONE_CHAT_MSG == js["msgid"].get<int>())
                {
                    cout << js["time"].get<string>() << " " << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
                else
                {
                    cout << "群消息: " << js["time"].get<string>() << " " << js["name"].get<string>()
                            << " said: " << js["msg"].get<string>() << endl;
                }
            }
        }

        // 显示离线期间收到的待处理好友请求
        if (responsejs.contains("pendingrequests"))
        {
            vector<string> vec = responsejs["pendingrequests"];
            if (!vec.empty())
            {
                cout << "------ pending friend requests ------" << endl;
                for (string &str : vec)
                {
                    json js = json::parse(str);
                    cout << js.value("name", string()) << " (" << js.value("email", string())
                         << ")  -> resp:" << js.value("email", string()) << " y|n" << endl;
                }
            }
        }

        g_isLoginSuccess = true;
    }
}

// 处理添加好友的响应逻辑（含发起方收到的「对方已同意」通知：agreed=true）
void doAddFriendResponse(json &responsejs)
{
    // 对方同意了你的好友请求（服务端复用 ACK 号推送，agreed=true）
    if (responsejs.value("agreed", false) && responsejs.value("errno", 1) == 0)
    {
        cout << responsejs.value("name", string()) << " accepted your friend request, "
             << "you are friends now (relogin to refresh the list)" << endl;
        return;
    }

    const int errno_ = responsejs.value("errno", 1);
    if (0 == errno_)
    {
        cout << "friend request sent to " << responsejs.value("name", string())
             << " (" << responsejs.value("email", string()) << "), waiting for approval" << endl;
        return;
    }
    if (4 == errno_)
    {
        cout << "you are already friends with " << responsejs.value("name", string()) << endl;
        return;
    }
    if (5 == errno_)
    {
        cout << "friend request already sent, waiting for approval" << endl;
        return;
    }
    cerr << "add friend failed: " << responsejs.value("errmsg", string("unknown error")) << endl;
}

// 收到好友请求通知（实时推送）
void doFriendRequestNotify(json &js)
{
    cout << js.value("name", string()) << " (" << js.value("email", string())
         << ") wants to add you as a friend." << endl;
    cout << "reply with: resp:" << js.value("email", string()) << " y|n" << endl;
}

// 处理好友请求处理结果的响应（同意/拒绝提交后的回执）
void doFriendRespAck(json &js)
{
    if (0 != js.value("errno", 1))
    {
        cerr << "friend request resp failed: " << js.value("errmsg", string("unknown error")) << endl;
        return;
    }
    cout << (js.value("agreed", false) ? "accepted" : "declined")
         << " friend request" << endl;
}

// 从本地好友缓存中移除指定邮箱的用户
void removeFriendFromList(const string &email)
{
    for (auto it = g_currentUserFriendList.begin(); it != g_currentUserFriendList.end(); ++it)
    {
        if (it->getEmail() == email)
        {
            g_currentUserFriendList.erase(it);
            return;
        }
    }
}

// 被好友移除通知（实时推送）
void doFriendRemovedNotify(json &js)
{
    const string email = js.value("email", string());
    cout << js.value("name", string()) << " has removed you from their friend list" << endl;
    removeFriendFromList(email);
}

// 处理删除好友的响应逻辑
void doDelFriendResponse(json &js)
{
    const int errno_ = js.value("errno", 1);
    if (0 == errno_)
    {
        const string email = js.value("email", string());
        cout << "friend removed: " << js.value("name", string())
             << " (" << email << ")" << endl;
        removeFriendFromList(email);
        return;
    }
    if (1 == errno_)
    {
        cerr << "delete friend failed: user not found" << endl;
        return;
    }
    if (2 == errno_)
    {
        cerr << "delete friend failed: you are not friends with this user" << endl;
        return;
    }
    cerr << "delete friend failed: " << js.value("errmsg", string("unknown error")) << endl;
}

// 处理加入群组的响应逻辑
void doAddGroupResponse(json &responsejs)
{
    const int errno_ = responsejs.value("errno", 1);
    if (0 == errno_)
    {
        cout << "join group success: " << responsejs.value("groupname", string()) << endl;
        return;
    }

    if (2 == errno_ && responsejs.contains("groups"))
    {
        cout << "multiple groups with the same name, retry with groupname#index:" << endl;
        vector<string> vec = responsejs["groups"];
        int index = 1;
        for (string &str : vec)
        {
            json group = json::parse(str);
            cout << "  " << index << ". " << group.value("name", string());
            const string desc = group.value("desc", string());
            if (!desc.empty())
            {
                cout << " - " << desc;
            }
            cout << endl;
            ++index;
        }
        return;
    }

    cerr << "join group failed: " << responsejs.value("errmsg", string("unknown error")) << endl;
}

// 处理私聊/群聊发送失败响应（仅失败时返回）
void doChatSendAck(json &responsejs)
{
    const int errno_ = responsejs.value("errno", 1);
    if (errno_ == 1)
    {
        cerr << "message send failed: target user or group not found" << endl;
    }
    else if (errno_ == 2)
    {
        cerr << "message send failed: content empty or exceeds " << MAX_TEXT_LEN << " bytes" << endl;
    }
    else if (errno_ == 3)
    {
        cerr << "message send failed: you are not a member of this group" << endl;
    }
    else
    {
        cerr << "message send failed: " << responsejs.value("errmsg", string("unknown error")) << endl;
    }
}

// 处理创建群组的响应逻辑
void doCreateGroupResponse(json &responsejs)
{
    if (0 == responsejs.value("errno", 1))
    {
        cout << "create group success: " << responsejs.value("groupname", string()) << endl;
        return;
    }
    cerr << "create group failed: " << responsejs.value("errmsg", string("unknown error")) << endl;
}

// 子线程 - 接收线程
void readTaskHandler(int clientfd)
{
    string recvBuf;
    char chunk[4096];

    for (;;)
    {
        ssize_t len = recv(clientfd, chunk, sizeof(chunk), 0);  // 阻塞了
        if (0 == len)
        {
            cerr << "server closed connection" << endl;
            close(clientfd);
            exit(-1);
        }
        if (len < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            cerr << "recv error: " << strerror(errno) << endl;
            close(clientfd);
            exit(-1);
        }

        recvBuf.append(chunk, len);

        // 按「4字节网络序长度 + JSON负载」循环解帧
        while (recvBuf.size() >= 4)
        {
            uint32_t netLen = 0;
            memcpy(&netLen, recvBuf.data(), 4);
            uint32_t msgLen = ntohl(netLen);

            if (0 == msgLen || msgLen > MAX_MSG_LEN)
            {
                cerr << "invalid message length: " << msgLen << endl;
                close(clientfd);
                exit(-1);
            }

            // 不足一整帧，等下一次recv
            if (recvBuf.size() < 4 + msgLen)
            {
                break;
            }

            string payload = recvBuf.substr(4, msgLen);
            recvBuf.erase(0, 4 + msgLen);

            // 接收ChatServer转发的数据，反序列化生成json数据对象
            json js;
            try
            {
                js = json::parse(payload);
            }
            catch (const exception &e)
            {
                cerr << "json parse error: " << e.what() << endl;
                continue;
            }

            int msgtype = js["msgid"].get<int>();
            if (ONE_CHAT_MSG == msgtype)
            {
                cout << js["time"].get<string>() << " " << js["name"].get<string>()
                     << " said: " << js["msg"].get<string>() << endl;
                continue;
            }

            if (GROUP_CHAT_MSG == msgtype)
            {
                cout << "群消息: " << js["time"].get<string>() << " " << js["name"].get<string>()
                     << " said: " << js["msg"].get<string>() << endl;
                continue;
            }

            if (LOGIN_MSG_ACK == msgtype)
            {
                doLoginResponse(js); // 处理登录响应的业务逻辑
                sem_post(&rwsem);    // 通知主线程，登录结果处理完成
                continue;
            }

            if (REG_MSG_ACK == msgtype)
            {
                doRegResponse(js);
                sem_post(&rwsem);    // 通知主线程，注册结果处理完成
                continue;
            }

            if (ADD_FRIEND_MSG_ACK == msgtype)
            {
                doAddFriendResponse(js);
                continue;
            }

            if (ADD_FRIEND_MSG == msgtype)
            {
                // 下行复用：好友请求通知
                doFriendRequestNotify(js);
                continue;
            }

            if (ADD_FRIEND_RESP_MSG_ACK == msgtype)
            {
                doFriendRespAck(js);
                continue;
            }

            if (DEL_FRIEND_MSG == msgtype)
            {
                // 下行复用：被好友移除通知
                doFriendRemovedNotify(js);
                continue;
            }

            if (DEL_FRIEND_MSG_ACK == msgtype)
            {
                doDelFriendResponse(js);
                continue;
            }

            if (ADD_GROUP_MSG_ACK == msgtype)
            {
                doAddGroupResponse(js);
                continue;
            }

            if (CREATE_GROUP_MSG_ACK == msgtype)
            {
                doCreateGroupResponse(js);
                continue;
            }

            if (CHAT_SEND_ACK == msgtype)
            {
                doChatSendAck(js);
                continue;
            }
        }
    }
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => name:" << g_currentUser.getName()
         << " email:" << g_currentUser.getEmail() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getName() << " " << user.getEmail() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << "  " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "resp" command handler（处理好友请求）
void resp(int, string);
// "delfriend" command handler（删除好友）
void delfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:email或用户名:message"},
    {"addfriend", "添加好友（发请求），格式addfriend:email或用户名"},
    {"resp", "处理好友请求，格式resp:对方邮箱:y或n"},
    {"delfriend", "删除好友，格式delfriend:email或用户名"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupname（同名群用 groupname#序号）"},
    {"groupchat", "群聊，格式groupchat:groupname:message（同名群用 groupname#序号）"},
    {"loginout", "注销，格式loginout"}};

// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"resp", resp},
    {"delfriend", delfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

// 主聊天页面程序
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command; // 存储命令
        int idx = commandbuf.find(":");
        if (-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加新功能不需要修改该函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}
// 解析群名称与可选序号：groupname#2 -> groupname + candidate(1)
// 返回 false 表示格式非法
bool parseGroupTarget(const string &target, string &groupname, int &candidate)
{
    groupname = target;
    candidate = -1;

    const string::size_type pos = target.rfind('#');
    if (pos == string::npos)
    {
        return !groupname.empty();
    }

    const string num = target.substr(pos + 1);
    if (num.empty())
    {
        return false;
    }
    for (char ch : num)
    {
        if (!isdigit(static_cast<unsigned char>(ch)))
        {
            return false;
        }
    }

    groupname = target.substr(0, pos);
    candidate = atoi(num.c_str()) - 1;
    return !groupname.empty() && candidate >= 0;
}

// "addfriend" command handler
void addfriend(int clientfd, string str)
{
    const string account = str;
    if (account.empty())
    {
        cerr << "addfriend command invalid!" << endl;
        return;
    }

    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["account"] = account;
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}
// "resp" command handler  对方邮箱:y|n（同意/拒绝好友请求）
void resp(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "resp command invalid! format: resp:email:y|n" << endl;
        return;
    }

    const string email = str.substr(0, idx);
    string answer = str.substr(idx + 1, str.size() - idx);
    if (email.empty() || answer.empty())
    {
        cerr << "resp command invalid! format: resp:email:y|n" << endl;
        return;
    }

    // 只取首字符判定，容忍 "y "/yes/YES 等输入
    const char first = static_cast<char>(tolower(static_cast<unsigned char>(answer[0])));
    if (first != 'y' && first != 'n')
    {
        cerr << "resp answer must be y or n!" << endl;
        return;
    }

    json js;
    js["msgid"] = ADD_FRIEND_RESP_MSG;
    js["account"] = email;
    js["accept"] = (first == 'y');
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send resp msg error -> " << buffer << endl;
    }
}
// "delfriend" command handler  email或用户名
void delfriend(int clientfd, string str)
{
    const string account = str;
    if (account.empty())
    {
        cerr << "delfriend command invalid! format: delfriend:email或用户名" << endl;
        return;
    }

    json js;
    js["msgid"] = DEL_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["account"] = account;
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send delfriend msg error -> " << buffer << endl;
    }
}
// "chat" command handler
void chat(int clientfd, string str)
{
    int idx = str.find(":"); // email或用户名:message
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    const string account = str.substr(0, idx);
    string message = str.substr(idx + 1, str.size() - idx);
    if (account.empty())
    {
        cerr << "chat account can not be empty!" << endl;
        return;
    }
    if (message.empty() || message.size() > MAX_TEXT_LEN)
    {
        cerr << "message is empty or too long (max " << MAX_TEXT_LEN << " bytes)!" << endl;
        return;
    }

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toaccount"] = account;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}
// "creategroup" command handler  groupname:groupdesc
void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}
// "addgroup" command handler   groupname[#index]
void addgroup(int clientfd, string str)
{
    string groupname;
    int candidate = -1;
    if (!parseGroupTarget(str, groupname, candidate))
    {
        cerr << "addgroup command invalid! format: addgroup:groupname（同名群用 groupname#序号）" << endl;
        return;
    }

    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    if (candidate >= 0)
    {
        js["candidate"] = candidate;
    }
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}
// "groupchat" command handler   groupname[#index]:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    string groupname;
    int candidate = -1;
    if (!parseGroupTarget(str.substr(0, idx), groupname, candidate))
    {
        cerr << "groupchat command invalid! format: groupchat:groupname:message" << endl;
        return;
    }
    string message = str.substr(idx + 1, str.size() - idx);
    if (message.empty() || message.size() > MAX_TEXT_LEN)
    {
        cerr << "message is empty or too long (max " << MAX_TEXT_LEN << " bytes)!" << endl;
        return;
    }

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupname"] = groupname;
    if (candidate >= 0)
    {
        js["candidate"] = candidate;
    }
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}
// "loginout" command handler
void loginout(int clientfd, string)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    if (!sendFrame(clientfd, buffer))
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }   
}

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}