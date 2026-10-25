#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
using namespace std;

/*
redis作为集群服务器通信的基于发布-订阅消息队列。

线程模型（修复原实现的订阅上下文并发访问缺陷）：
- 订阅上下文 _subcribe_context 仅由订阅读线程访问；
- 业务线程通过「请求队列 + wakeup管道」提交订阅/退订命令，读线程被唤醒后统一发送；
- 订阅/退订阻塞等待 redis 确认回包后才返回，保证通道就绪/摘除的时序确定。
publish 使用独立的上下文连接，与订阅通道互不影响。
*/
class Redis
{
public:
    Redis();
    ~Redis();

    // 连接redis服务器（host/port由配置文件提供）
    bool connect(const string &host, int port);

    // 向redis指定的通道channel发布消息
    bool publish(int channel, string message);

    // 订阅通道：阻塞直到redis确认订阅成功（或超时）
    bool subscribe(int channel);

    // 取消订阅：阻塞直到redis确认取消（或超时）；未订阅的通道直接返回成功
    bool unsubscribe(int channel);

    // 在独立线程中监听订阅通道消息与订阅命令确认
    void observer_channel_message();

    // 初始化向业务层上报通道消息的回调对象
    void init_notify_handler(function<void(int, string)> fn);

private:
    // 订阅命令请求：业务线程投递，读线程消费
    struct SubRequest
    {
        int channel;
        bool subscribe;
    };

    // 由读线程在订阅上下文上发送 SUBSCRIBE/UNSUBSCRIBE
    bool appendAndFlush(int channel, bool subscribe);

    // 处理订阅连接上的一条回复（命令确认或通道消息）
    void handleReply(redisReply *reply);

    // hiredis同步上下文对象，负责publish消息
    redisContext *_publish_context = nullptr;

    // hiredis同步上下文对象，负责subscribe消息（仅读线程访问）
    redisContext *_subcribe_context = nullptr;

    // 回调操作，收到订阅的消息，给service层上报
    function<void(int, string)> _notify_message_handler;

    // publish上下文的锁
    mutex _publishMutex;

    // 订阅命令请求队列与唤醒管道
    vector<SubRequest> _pendingSubs;
    mutex _subReqMutex;
    int _wakeWriteFd = -1;
    int _wakeReadFd = -1;

    // 已被redis确认订阅的通道集合，用于同步等待确认
    set<int> _subscribed;
    mutex _subscribedMutex;
    condition_variable _subscribedCv;

    // 订阅读线程存活标记
    atomic_bool _readerAlive{false};
};

#endif
