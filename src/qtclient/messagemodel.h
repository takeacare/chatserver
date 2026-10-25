#ifndef MESSAGEMODEL_H
#define MESSAGEMODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// 一个会话的消息列表模型，供 QML 的 ListView 展示
class MessageModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles
    {
        SenderIdRole = Qt::UserRole + 1,
        SenderNameRole,
        TextRole,
        TimeRole,
        MineRole,
        OfflineRole
    };

    struct Message
    {
        int senderId = -1;
        QString senderName;
        QString text;
        QString time;
        bool mine = false;
        bool offline = false;
    };

    explicit MessageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendMessage(const Message &msg);

private:
    QVector<Message> _messages;
};

#endif // MESSAGEMODEL_H
