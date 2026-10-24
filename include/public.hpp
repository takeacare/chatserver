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
};

// 单帧最大长度 4MB，防止非法长度头耗尽内存
constexpr uint32_t MAX_MSG_LEN = 4 * 1024 * 1024;

// 分帧规则：4字节网络序长度 + JSON负载
inline std::string encodeMessage(const std::string &payload)
{
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::string frame(reinterpret_cast<const char *>(&len), 4);
    frame.append(payload);
    return frame;
}

#endif