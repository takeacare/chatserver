#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>
using namespace std;
using namespace muduo;
using namespace muduo::net;

#include "redis.hpp"
#include "groupmodel.hpp"
#include "friendmodel.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "json.hpp"
using json = nlohmann::json;

// 表示处理消息的事件回调方法类型
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;

// 聊天服务器业务类
class ChatService
{
public:
    // 获取单例对象的接口函数
    static ChatService *instance();
    // 处理登录业务
    void login(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理注册业务
    void reg(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 一对一聊天业务
    void oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 添加好友业务
    void addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 创建群组业务
    void createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 加入群组业务
    void addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 群组聊天业务
    void groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理注销业务
    void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time);
    // 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);
    // 服务器异常，业务重置方法
    void reset();
    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);
    // 从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int, string);

private:
    ChatService();

    // 按分帧协议向连接发送JSON
    void sendJson(const TcpConnectionPtr &conn, const json &js);

    // 从连接上下文取出登录后写入的用户id；未登录返回false（防伪造请求）
    bool getAuthUserId(const TcpConnectionPtr &conn, int &userid);

    // 发送私聊/群聊发送失败ACK（CHAT_SEND_ACK，仅失败时返回）
    void sendChatAck(const TcpConnectionPtr &conn, int errno_, const string &msg);

    // 注销公共逻辑：移除连接映射、取消订阅、更新离线状态
    void doLogout(const TcpConnectionPtr &conn, int userid);

    // 存储消息id和其对应的业务处理方法
    unordered_map<int, MsgHandler> _msgHandlerMap;
    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;
    // 定义互斥锁，保证_userConnMap的线程安全
    mutex _connMutex;

    // 登录失败锁定：key为小写化邮箱，连续失败达到上限后临时锁定
    struct LoginAttempt
    {
        int fails = 0;
        chrono::steady_clock::time_point lockUntil{};
    };
    unordered_map<string, LoginAttempt> _loginAttempts;
    mutex _attemptMutex;
    static constexpr int MAX_LOGIN_FAILS = 5;
    static constexpr int LOGIN_LOCK_SECONDS = 300;

    // 数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    // redis操作对象
    Redis _redis;
};

#endif