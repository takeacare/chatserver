#ifndef CONVERSATION_H
#define CONVERSATION_H

#include <QString>
#include <QVector>

#include "messagemodel.h"

// 群成员信息（来自登录响应 groups[].users）
struct MemberInfo
{
    int id = -1;
    QString name;
    QString state;
    QString role;
};

// 一个聊天会话：私聊按好友 id 区分，群聊按群 id 区分
struct Conversation
{
    int peerId = -1;
    bool isGroup = false;
    QString title;
    MessageModel *model = nullptr;
    QVector<MemberInfo> members;
    QString desc;
};

// 会话索引键：u:<id> 或 g:<id>
inline QString conversationKey(int id, bool isGroup)
{
    return (isGroup ? QStringLiteral("g:") : QStringLiteral("u:")) + QString::number(id);
}

#endif // CONVERSATION_H
