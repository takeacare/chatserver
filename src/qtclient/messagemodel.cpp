#include "messagemodel.h"

MessageModel::MessageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MessageModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return _messages.size();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _messages.size())
    {
        return QVariant();
    }

    const Message &msg = _messages.at(index.row());
    switch (role)
    {
    case SenderIdRole:
        return msg.senderId;
    case SenderNameRole:
        return msg.senderName;
    case TextRole:
        return msg.text;
    case TimeRole:
        return msg.time;
    case MineRole:
        return msg.mine;
    case OfflineRole:
        return msg.offline;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> MessageModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SenderIdRole] = "senderId";
    roles[SenderNameRole] = "senderName";
    roles[TextRole] = "text";
    roles[TimeRole] = "time";
    roles[MineRole] = "mine";
    roles[OfflineRole] = "offline";
    return roles;
}

void MessageModel::appendMessage(const Message &msg)
{
    beginInsertRows(QModelIndex(), _messages.size(), _messages.size());
    _messages.push_back(msg);
    endInsertRows();
}
