#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QVector>

#include "conversation.h"
#include "json.hpp"

class ChatClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QQuickWidget;
class QSplitter;
class QTabWidget;

// 聊天主窗口：左侧好友/群组，右侧消息区（QML）与输入区
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ChatClient *client, QWidget *parent = nullptr);

    // 登录成功后建立会话
    void startSession(const nlohmann::json &ack, const QString &email, const QString &password);

signals:
    // 注销或连接断开后需要回到登录对话框
    void loggedOut();

private slots:
    void onFriendClicked(QListWidgetItem *item);
    void onGroupClicked(QListWidgetItem *item);
    void onSendClicked();
    void onAddFriend();
    void onCreateGroup();
    void onJoinGroup();
    void onRefreshLists();
    void onShowMembers();
    void onLogout();
    void onLoginResult(const nlohmann::json &js);
    void onAddFriendResult(const nlohmann::json &js);
    void onAddGroupResult(const nlohmann::json &js);
    void onCreateGroupResult(const nlohmann::json &js);
    void onChatSendResult(const nlohmann::json &js);
    void onChatMessage(const nlohmann::json &js);
    void onDisconnected();
    void onSocketError(const QString &message);

private:
    struct FriendEntry
    {
        int id = -1;
        QString name;
        QString email;
        QString state;
        bool stranger = false;
    };

    struct GroupEntry
    {
        int id = -1;
        QString name;
        QString desc;
        QVector<MemberInfo> members;
        bool stranger = false;
    };

    void buildUi();
    void resetSession();
    void clearConversations();
    void applyLoginAck(const nlohmann::json &ack, bool refresh);
    void rebuildListsFromSession();
    Conversation *ensureConversation(int id, bool isGroup, const QString &fallbackName);
    void switchConversation(Conversation *conv);
    void appendIncoming(const nlohmann::json &js, bool offline);
    void addFriendItem(int id, const QString &name, const QString &email, const QString &state,
                       bool stranger);
    void addGroupItem(int id, const QString &name, const QString &desc,
                      const QVector<MemberInfo> &members, bool stranger);
    void updateActiveHeader();
    // 状态栏文本与语义色（idle / ok / info / error），样式由主题统一控制
    void setStatusText(const QString &text, const QString &state);
    QString nowText() const;

    ChatClient *_client = nullptr;

    // 会话与数据
    int _myId = -1;
    QString _myName;
    QString _email;
    QString _password;
    bool _sessionActive = false;
    bool _refreshPending = false;
    // 防重复点击：请求在途时忽略同类按钮点击
    bool _addFriendPending = false;
    bool _createGroupPending = false;
    bool _joinGroupPending = false;

    QVector<FriendEntry> _friends;
    QVector<GroupEntry> _groups;
    QHash<QString, Conversation *> _conversations;
    Conversation *_active = nullptr;
    MessageModel *_emptyModel = nullptr;
    QSet<int> _friendItemIds;
    QSet<int> _groupItemIds;

    // 控件
    QSplitter *_splitter = nullptr;
    QTabWidget *_tabs = nullptr;
    QListWidget *_friendList = nullptr;
    QListWidget *_groupList = nullptr;
    QLabel *_titleLabel = nullptr;
    QPushButton *_membersBtn = nullptr;
    QPushButton *_addFriendBtn = nullptr;
    QPushButton *_createGroupBtn = nullptr;
    QPushButton *_joinGroupBtn = nullptr;
    QQuickWidget *_messageView = nullptr;
    QLineEdit *_inputEdit = nullptr;
    QPushButton *_sendBtn = nullptr;
    QLabel *_statusLabel = nullptr;
};

#endif // MAINWINDOW_H
