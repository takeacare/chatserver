#include "chatclient.h"

#include <cstring>

#include "public.hpp"

ChatClient::ChatClient(QObject *parent)
    : QObject(parent),
      _socket(new QTcpSocket(this))
{
    connect(_socket, &QTcpSocket::readyRead, this, &ChatClient::onReadyRead);
    connect(_socket, &QTcpSocket::connected, this, &ChatClient::connected);
    connect(_socket, &QTcpSocket::disconnected, this, &ChatClient::disconnected);
    connect(_socket, &QAbstractSocket::errorOccurred, this, &ChatClient::onSocketError);
}

void ChatClient::connectToServer(const QString &host, quint16 port)
{
    if (_socket->state() != QAbstractSocket::UnconnectedState)
    {
        _socket->abort();
    }
    _buffer.clear();
    _socket->connectToHost(host, port);
}

void ChatClient::disconnectFromServer()
{
    _socket->disconnectFromHost();
}

bool ChatClient::isConnected() const
{
    return _socket->state() == QAbstractSocket::ConnectedState;
}

void ChatClient::sendLogin(const QString &email, const QString &password)
{
    nlohmann::json js;
    js["msgid"] = LOGIN_MSG;
    js["email"] = email.toStdString();
    js["password"] = password.toStdString();
    sendJson(js);
}

void ChatClient::sendRegister(const QString &name, const QString &email, const QString &password)
{
    nlohmann::json js;
    js["msgid"] = REG_MSG;
    js["name"] = name.toStdString();
    js["email"] = email.toStdString();
    js["password"] = password.toStdString();
    sendJson(js);
}

void ChatClient::sendOneChat(int id, const QString &name, int toid, const QString &msg, const QString &time)
{
    nlohmann::json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = id;
    js["name"] = name.toStdString();
    js["toid"] = toid;
    js["msg"] = msg.toStdString();
    js["time"] = time.toStdString();
    sendJson(js);
}

void ChatClient::sendGroupChat(int id, const QString &name, int groupid, const QString &msg, const QString &time)
{
    nlohmann::json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = id;
    js["name"] = name.toStdString();
    js["groupid"] = groupid;
    js["msg"] = msg.toStdString();
    js["time"] = time.toStdString();
    sendJson(js);
}

void ChatClient::sendAddFriend(int id, const QString &account)
{
    nlohmann::json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = id;
    js["account"] = account.toStdString();
    sendJson(js);
}

void ChatClient::sendCreateGroup(int id, const QString &groupname, const QString &groupdesc)
{
    nlohmann::json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = id;
    js["groupname"] = groupname.toStdString();
    js["groupdesc"] = groupdesc.toStdString();
    sendJson(js);
}

void ChatClient::sendAddGroup(int id, const QString &groupname, int candidate)
{
    nlohmann::json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = id;
    js["groupname"] = groupname.toStdString();
    if (candidate >= 0)
    {
        js["candidate"] = candidate;
    }
    sendJson(js);
}

void ChatClient::sendLoginout(int id)
{
    nlohmann::json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = id;
    sendJson(js);
}

void ChatClient::sendJson(const nlohmann::json &js)
{
    if (!isConnected())
    {
        return;
    }
    // 复用 public.hpp 的分帧规则：4 字节网络序长度 + JSON
    const std::string frame = encodeMessage(js.dump());
    _socket->write(frame.data(), static_cast<qint64>(frame.size()));
}

void ChatClient::onReadyRead()
{
    _buffer.append(_socket->readAll());

    // 循环拆帧，正确处理半包与粘包
    while (_buffer.size() >= 4)
    {
        quint32 len = 0;
        std::memcpy(&len, _buffer.constData(), sizeof(quint32));
        len = ntohl(len);

        if (len == 0 || len > MAX_MSG_LEN)
        {
            // 非法长度：立即断开，避免内存被恶意帧耗尽
            emit socketError(QStringLiteral("收到非法帧长度，连接已断开"));
            _socket->abort();
            _buffer.clear();
            return;
        }

        if (static_cast<quint32>(_buffer.size()) < len + 4)
        {
            break; // 半包，等待后续数据
        }

        const QByteArray payload = _buffer.mid(4, static_cast<int>(len));
        _buffer.remove(0, static_cast<int>(len) + 4);
        processPayload(payload);
    }
}

void ChatClient::processPayload(const QByteArray &payload)
{
    nlohmann::json js;
    try
    {
        js = nlohmann::json::parse(payload.toStdString());
    }
    catch (const std::exception &)
    {
        return; // 非法 JSON 直接忽略
    }

    if (!js.contains("msgid"))
    {
        return;
    }

    switch (js["msgid"].get<int>())
    {
    case LOGIN_MSG_ACK:
        emit loginResult(js);
        break;
    case REG_MSG_ACK:
        emit registerResult(js);
        break;
    case ADD_FRIEND_MSG_ACK:
        emit addFriendResult(js);
        break;
    case ADD_GROUP_MSG_ACK:
        emit addGroupResult(js);
        break;
    case CREATE_GROUP_MSG_ACK:
        emit createGroupResult(js);
        break;
    case CHAT_SEND_ACK:
        emit chatSendResult(js);
        break;
    case ONE_CHAT_MSG:
    case GROUP_CHAT_MSG:
        emit chatMessage(js);
        break;
    default:
        break;
    }
}

void ChatClient::onSocketError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    emit socketError(_socket->errorString());
}
