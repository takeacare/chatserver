#ifndef CHATCLIENT_H
#define CHATCLIENT_H

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QByteArray>
#include <QAbstractSocket>

#include "json.hpp"

// 网络通信层：QTcpSocket + 4 字节网络序长度前缀分帧 + JSON
// 全部异步，由 Qt 事件循环驱动，无阻塞调用
class ChatClient : public QObject
{
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;

    void sendLogin(const QString &email, const QString &password);
    void sendRegister(const QString &name, const QString &email, const QString &password);
    void sendOneChat(int id, const QString &name, int toid, const QString &msg, const QString &time);
    void sendGroupChat(int id, const QString &name, int groupid, const QString &msg, const QString &time);
    void sendAddFriend(int id, const QString &account);
    void sendCreateGroup(int id, const QString &groupname, const QString &groupdesc);
    void sendAddGroup(int id, const QString &groupname, int candidate = -1);
    void sendLoginout(int id);

signals:
    void connected();
    void disconnected();
    void socketError(const QString &message);
    void loginResult(const nlohmann::json &js);
    void registerResult(const nlohmann::json &js);
    void addFriendResult(const nlohmann::json &js);
    void addGroupResult(const nlohmann::json &js);
    void createGroupResult(const nlohmann::json &js);
    // 私聊/群聊发送失败响应（CHAT_SEND_ACK，仅失败时到达）
    void chatSendResult(const nlohmann::json &js);
    // ONE_CHAT_MSG 与 GROUP_CHAT_MSG 的统一入口，上层按字段区分
    void chatMessage(const nlohmann::json &js);

private slots:
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    void sendJson(const nlohmann::json &js);
    void processPayload(const QByteArray &payload);

    QTcpSocket *_socket = nullptr;
    QByteArray _buffer;
};

#endif // CHATCLIENT_H
