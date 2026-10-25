#include "redis.hpp"

#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <unistd.h>
using namespace std;

// 等待redis确认订阅/退订的超时时间
static constexpr int SUB_CONFIRM_TIMEOUT_MS = 3000;

Redis::Redis() = default;

Redis::~Redis()
{
    _readerAlive = false;

    if (_publish_context != nullptr)
    {
        redisFree(_publish_context);
    }

    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context);
    }

    if (_wakeWriteFd != -1)
    {
        close(_wakeWriteFd);
    }
    if (_wakeReadFd != -1)
    {
        close(_wakeReadFd);
    }
}

bool Redis::connect(const string &host, int port)
{
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect(host.c_str(), port);
    if (nullptr == _publish_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }
    if (_publish_context->err)
    {
        cerr << "connect redis failed: " << _publish_context->errstr << endl;
        redisFree(_publish_context);
        _publish_context = nullptr;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subcribe_context = redisConnect(host.c_str(), port);
    if (nullptr == _subcribe_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }
    if (_subcribe_context->err)
    {
        cerr << "connect redis failed: " << _subcribe_context->errstr << endl;
        redisFree(_subcribe_context);
        _subcribe_context = nullptr;
        return false;
    }

    // wakeup管道：业务线程用它唤醒读线程去发送排队中的订阅命令
    int fds[2];
    if (pipe(fds) != 0)
    {
        cerr << "create wakeup pipe failed: " << strerror(errno) << endl;
        return false;
    }
    _wakeReadFd = fds[0];
    _wakeWriteFd = fds[1];

    // 读端设为非阻塞，便于一次性排空唤醒字节
    const int flags = fcntl(_wakeReadFd, F_GETFL, 0);
    fcntl(_wakeReadFd, F_SETFL, flags | O_NONBLOCK);

    _subscribed.clear();
    _readerAlive = true;

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    thread t([&]() {
        observer_channel_message();
    });
    t.detach();

    cout << "connect redis-server success!" << endl;

    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    if (_publish_context == nullptr)
    {
        return false;
    }

    lock_guard<mutex> lock(_publishMutex);

    // %b按长度发送消息内容，避免消息中的特殊字符破坏命令格式
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %b",
                                                   channel, message.c_str(), message.size());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return false;
    }

    // 正确的PUBLISH响应为整数（接收到消息的订阅者数量）
    const bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);
    return ok;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel)
{
    if (_subcribe_context == nullptr || !_readerAlive)
    {
        return false;
    }

    // 已确认订阅的通道直接返回，避免重复订阅导致等待超时
    {
        lock_guard<mutex> lock(_subscribedMutex);
        if (_subscribed.count(channel) > 0)
        {
            return true;
        }
    }

    // 投递请求并唤醒读线程，由读线程（订阅上下文的唯一访问者）发送命令
    {
        lock_guard<mutex> lock(_subReqMutex);
        _pendingSubs.push_back({channel, true});
    }
    const char wake = 'w';
    if (write(_wakeWriteFd, &wake, 1) != 1)
    {
        return false;
    }

    // 阻塞等待redis的subscribe确认回包
    unique_lock<mutex> lock(_subscribedMutex);
    return _subscribedCv.wait_for(lock, chrono::milliseconds(SUB_CONFIRM_TIMEOUT_MS),
                                  [&] { return _subscribed.count(channel) > 0; });
}

// 向redis指定的通道unsubscribe取消订阅消息（发即忘，不等待确认）
bool Redis::unsubscribe(int channel)
{
    if (_subcribe_context == nullptr || !_readerAlive)
    {
        return false;
    }

    // 未订阅的通道无需发送命令
    {
        lock_guard<mutex> lock(_subscribedMutex);
        if (_subscribed.count(channel) == 0)
        {
            return true;
        }
    }

    {
        lock_guard<mutex> lock(_subReqMutex);
        _pendingSubs.push_back({channel, false});
    }
    const char wake = 'w';
    return write(_wakeWriteFd, &wake, 1) == 1;
}

// 由读线程在订阅上下文上发送订阅命令并冲刷缓冲区
bool Redis::appendAndFlush(int channel, bool subscribe)
{
    if (REDIS_ERR == (subscribe ? redisAppendCommand(_subcribe_context, "SUBSCRIBE %d", channel)
                                : redisAppendCommand(_subcribe_context, "UNSUBSCRIBE %d", channel)))
    {
        cerr << "subscribe command append failed!" << endl;
        return false;
    }

    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(_subcribe_context, &done))
        {
            cerr << "subscribe command flush failed!" << endl;
            return false;
        }
    }
    return true;
}

// 处理订阅连接上的一条回复：命令确认（subscribe/unsubscribe）或通道消息（message）
void Redis::handleReply(redisReply *reply)
{
    // 订阅连接上的回复均为三元素数组 [type, channel, payload]
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY || reply->elements < 3)
    {
        return;
    }

    const redisReply *type = reply->element[0];
    const redisReply *channel = reply->element[1];
    const redisReply *payload = reply->element[2];
    if (type == nullptr || channel == nullptr || type->type != REDIS_REPLY_STRING ||
        channel->type != REDIS_REPLY_STRING)
    {
        return;
    }

    const string typeStr(type->str, type->len);

    if (typeStr == "message")
    {
        // 通道消息：给业务层上报
        if (payload != nullptr && payload->type == REDIS_REPLY_STRING && _notify_message_handler)
        {
            _notify_message_handler(atoi(channel->str), string(payload->str, payload->len));
        }
        return;
    }

    if (typeStr == "subscribe" || typeStr == "unsubscribe")
    {
        // 命令确认：更新已订阅集合并唤醒等待的业务线程
        const int ch = atoi(channel->str);
        lock_guard<mutex> lock(_subscribedMutex);
        if (typeStr == "subscribe")
        {
            _subscribed.insert(ch);
        }
        else
        {
            _subscribed.erase(ch);
        }
        _subscribedCv.notify_all();
    }
}

// 在独立线程中接收订阅通道中的消息与订阅命令确认
// 本线程是订阅上下文的唯一访问者，从根本上避免多线程并发使用hiredis上下文
void Redis::observer_channel_message()
{
    while (true)
    {
        struct pollfd fds[2];
        fds[0].fd = _subcribe_context->fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = _wakeReadFd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        const int rc = poll(fds, 2, -1);
        if (rc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            cerr << "observer poll failed: " << strerror(errno) << endl;
            break;
        }

        // 唤醒信号：处理排队的订阅命令
        if (fds[1].revents & POLLIN)
        {
            char buf[64];
            ssize_t n = 0;
            do
            {
                n = read(_wakeReadFd, buf, sizeof(buf));
            } while (n > 0 || (n < 0 && errno == EINTR));

            vector<SubRequest> reqs;
            {
                lock_guard<mutex> lock(_subReqMutex);
                reqs.swap(_pendingSubs);
            }
            for (const SubRequest &req : reqs)
            {
                if (!appendAndFlush(req.channel, req.subscribe))
                {
                    break;
                }
            }
        }

        // redis fd可读：读取并解析回复（确认或通道消息）
        if (fds[0].revents & POLLIN)
        {
            redisReply *reply = nullptr;
            if (REDIS_OK != redisGetReply(_subcribe_context, (void **)&reply))
            {
                cerr << "redis subscribe connection error: " << _subcribe_context->errstr << endl;
                break;
            }
            if (reply != nullptr)
            {
                handleReply(reply);
                freeReplyObject(reply);
            }
        }
    }

    _readerAlive = false;

    // 唤醒所有等待确认的线程，避免其永久阻塞
    {
        lock_guard<mutex> lock(_subscribedMutex);
        _subscribedCv.notify_all();
    }

    cerr << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int,string)> fn)
{
    this->_notify_message_handler = std::move(fn);
}
