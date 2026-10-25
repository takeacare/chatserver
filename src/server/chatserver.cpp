#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include "public.hpp"

#include <muduo/base/Logging.h>
#include <iostream>
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开链接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    // 按「4字节网络序长度 + JSON负载」循环解帧，处理粘包与拆包
    while (buffer->readableBytes() >= 4)
    {
        uint32_t netLen = 0;
        memcpy(&netLen, buffer->peek(), 4);
        uint32_t len = ntohl(netLen);

        if (len == 0 || len > MAX_MSG_LEN)
        {
            LOG_ERROR << "invalid message length: " << len << ", close connection";
            conn->shutdown();
            return;
        }

        // 不足一整帧，剩余数据留在Buffer中等待下一次回调
        if (buffer->readableBytes() < 4 + len)
        {
            return;
        }

        buffer->retrieve(4);
        string payload = buffer->retrieveAsString(len);

        json js;
        try
        {
            js = json::parse(payload);
        }
        catch (const exception &e)
        {
            LOG_ERROR << "json parse failed: " << e.what();
            continue;
        }

        // msgid缺失或类型非法时仅丢弃该帧，不断开连接
        const auto msgidIt = js.find("msgid");
        if (msgidIt == js.end() || !msgidIt->is_number_integer())
        {
            LOG_ERROR << "message without valid msgid";
            continue;
        }

        // 数据的反序列化
        // 达到的目的：完全解耦网络模块的代码和业务模块的代码
        // 通过js["msgid"] 获取=》业务handler=》conn  js  time
        auto msgHandler = ChatService::instance()->getHandler(msgidIt->get<int>());
        // 回调消息绑定好的事件处理器，来执行相应的业务处理
        // 业务处理中的异常（如字段缺失、类型错误）不能把整个服务端线程带崩
        try
        {
            msgHandler(conn, js, time);
        }
        catch (const exception &e)
        {
            LOG_ERROR << "handle message failed: " << e.what();
            conn->shutdown();
            return;
        }
    }
}