#ifndef PUBLIC_H
#define PUBLIC_H

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <string>

/*
server和client的公共文件
*/
enum EnMsgType
{
    LOGIN_MSG = 1, // 登录消息
    LOGIN_MSG_ACK, // 登录响应消息
    LOGINOUT_MSG, // 注销消息
    REG_MSG, // 注册消息
    REG_MSG_ACK, // 注册响应消息
    ONE_CHAT_MSG, // 聊天消息
    ADD_FRIEND_MSG, // 添加好友消息

    CREATE_GROUP_MSG, // 创建群组
    ADD_GROUP_MSG, // 加入群组
    GROUP_CHAT_MSG, // 群聊天
    ADD_FRIEND_MSG_ACK, // 添加好友响应
    ADD_GROUP_MSG_ACK, // 加入群组响应
    CHAT_SEND_ACK, // 私聊/群聊发送失败响应（仅失败时由服务端返回）
    CREATE_GROUP_MSG_ACK, // 创建群组响应
    ADD_FRIEND_RESP_MSG, // 处理好友请求（同意/拒绝，请求接收方发出）
    ADD_FRIEND_RESP_MSG_ACK, // 好友请求处理结果响应
    DEL_FRIEND_MSG, // 删除好友（下行同号为「被移除」通知）
    DEL_FRIEND_MSG_ACK, // 删除好友结果响应
};

// 邮箱格式校验：客户端与服务端共用，保证双方规则一致
inline bool isValidEmail(const std::string &email)
{
    if (email.empty() || email.size() > 120)
    {
        return false;
    }

    const std::string::size_type at = email.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= email.size())
    {
        return false;
    }
    if (email.find('@', at + 1) != std::string::npos)
    {
        return false;
    }
    if (email.find(' ') != std::string::npos || email.find('\t') != std::string::npos)
    {
        return false;
    }

    // 域名部分必须含有点，且点后还有字符
    const std::string::size_type dot = email.find('.', at + 1);
    return dot != std::string::npos && dot + 1 < email.size();
}

// 单帧最大长度 4MB，防止非法长度头耗尽内存
constexpr uint32_t MAX_MSG_LEN = 4 * 1024 * 1024;

// 单条聊天消息内容长度上限（字节），防止超大消息拖垮数据库存储与转发链路
constexpr size_t MAX_TEXT_LEN = 4096;

// 分帧规则：4字节网络序长度 + JSON负载
inline std::string encodeMessage(const std::string &payload)
{
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::string frame(reinterpret_cast<const char *>(&len), 4);
    frame.append(payload);
    return frame;
}

#endif