#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickWidget>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "chatclient.h"
#include "public.hpp"

namespace
{
// 统一的卡片投影参数，保持视觉层次一致
void applyShadow(QWidget *widget, int blurRadius, int yOffset, int alpha)
{
    auto *shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setBlurRadius(blurRadius);
    shadow->setOffset(0, yOffset);
    shadow->setColor(QColor(23, 43, 77, alpha));
    widget->setGraphicsEffect(shadow);
}

// 生成列表头像：首字母 + 圆形/圆角色块，好友右下角带在线状态点
QIcon makeAvatarIcon(const QString &name, bool isGroup, const QString &state, qreal dpr)
{
    static const QVector<QColor> palette = {
        QColor(QStringLiteral("#2563eb")), QColor(QStringLiteral("#7c3aed")),
        QColor(QStringLiteral("#0ea5e9")), QColor(QStringLiteral("#059669")),
        QColor(QStringLiteral("#d97706")), QColor(QStringLiteral("#db2777"))};

    int hash = 0;
    for (const QChar ch : name)
    {
        hash = (hash * 31 + ch.unicode()) & 0x7fffffff;
    }
    const QColor base = palette.at(hash % palette.size());

    const int logical = 36;
    const int pixels = qMax(1, static_cast<int>(logical * dpr));
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box(0, 0, logical, logical);
    QPainterPath shape;
    const qreal radius = isGroup ? 10.0 : logical / 2.0;
    shape.addRoundedRect(box, radius, radius);
    painter.fillPath(shape, base);

    const QString trimmed = name.trimmed();
    const QString initial = trimmed.isEmpty() ? QStringLiteral("?")
                                             : trimmed.left(1).toUpper();
    QFont font = painter.font();
    font.setPointSizeF(11.0);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(box, Qt::AlignCenter, initial);

    if (!isGroup)
    {
        const QColor dot = state == QStringLiteral("online")
                               ? QColor(QStringLiteral("#22c55e"))
                               : QColor(QStringLiteral("#cbd5e1"));
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(dot);
        painter.drawEllipse(QPointF(logical - 7.5, logical - 7.5), 4.6, 4.6);
    }

    return QIcon(pixmap);
}
} // namespace

MainWindow::MainWindow(ChatClient *client, QWidget *parent)
    : QMainWindow(parent),
      _client(client)
{
    buildUi();

    connect(_client, &ChatClient::loginResult, this, &MainWindow::onLoginResult);
    connect(_client, &ChatClient::addFriendResult, this, &MainWindow::onAddFriendResult);
    connect(_client, &ChatClient::addGroupResult, this, &MainWindow::onAddGroupResult);
    connect(_client, &ChatClient::createGroupResult, this, &MainWindow::onCreateGroupResult);
    connect(_client, &ChatClient::chatSendResult, this, &MainWindow::onChatSendResult);
    connect(_client, &ChatClient::chatMessage, this, &MainWindow::onChatMessage);
    connect(_client, &ChatClient::disconnected, this, &MainWindow::onDisconnected);
    connect(_client, &ChatClient::socketError, this, &MainWindow::onSocketError);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("聊天室"));
    setMinimumSize(800, 600);
    resize(1080, 720);

    auto *central = new QWidget(this);
    auto *outerLayout = new QHBoxLayout(central);
    outerLayout->setContentsMargins(16, 12, 16, 12);
    outerLayout->setSpacing(0);
    setCentralWidget(central);

    _splitter = new QSplitter(Qt::Horizontal, central);
    _splitter->setHandleWidth(12);
    _splitter->setChildrenCollapsible(false);
    outerLayout->addWidget(_splitter);

    // ---------- 左侧：会话面板 ----------
    auto *leftCard = new QFrame(_splitter);
    leftCard->setObjectName(QStringLiteral("Card"));
    leftCard->setMinimumWidth(250);
    leftCard->setMaximumWidth(380);
    applyShadow(leftCard, 26, 6, 26);

    auto *leftLayout = new QVBoxLayout(leftCard);
    leftLayout->setContentsMargins(14, 16, 14, 12);
    leftLayout->setSpacing(14);

    auto *brandTitle = new QLabel(QStringLiteral("聊天室"), leftCard);
    brandTitle->setObjectName(QStringLiteral("AppTitle"));
    auto *brandSubtitle = new QLabel(QStringLiteral("与好友保持联系"), leftCard);
    brandSubtitle->setObjectName(QStringLiteral("AppSubtitle"));
    auto *brandBox = new QVBoxLayout();
    brandBox->setSpacing(2);
    brandBox->addWidget(brandTitle);
    brandBox->addWidget(brandSubtitle);
    leftLayout->addLayout(brandBox);

    auto *sectionRow = new QHBoxLayout();
    sectionRow->setSpacing(8);
    auto *sectionTitle = new QLabel(QStringLiteral("会话列表"), leftCard);
    sectionTitle->setObjectName(QStringLiteral("SectionTitle"));
    auto *sectionHint = new QLabel(QStringLiteral("好友 · 群组"), leftCard);
    sectionHint->setObjectName(QStringLiteral("CardCaption"));
    sectionRow->addWidget(sectionTitle);
    sectionRow->addStretch(1);
    sectionRow->addWidget(sectionHint);
    leftLayout->addLayout(sectionRow);

    auto *actionLayout = new QGridLayout();
    actionLayout->setSpacing(8);
    _addFriendBtn = new QPushButton(QStringLiteral("加好友"), leftCard);
    _createGroupBtn = new QPushButton(QStringLiteral("创建群"), leftCard);
    _joinGroupBtn = new QPushButton(QStringLiteral("加入群"), leftCard);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新列表"), leftCard);
    for (QPushButton *btn : {_addFriendBtn, _createGroupBtn, _joinGroupBtn, refreshBtn})
    {
        btn->setObjectName(QStringLiteral("SoftButton"));
    }
    actionLayout->addWidget(_addFriendBtn, 0, 0);
    actionLayout->addWidget(_createGroupBtn, 0, 1);
    actionLayout->addWidget(_joinGroupBtn, 1, 0);
    actionLayout->addWidget(refreshBtn, 1, 1);
    leftLayout->addLayout(actionLayout);

    _tabs = new QTabWidget(leftCard);
    _friendList = new QListWidget(_tabs);
    _groupList = new QListWidget(_tabs);
    for (QListWidget *list : {_friendList, _groupList})
    {
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setIconSize(QSize(36, 36));
        list->setMouseTracking(true);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setTextElideMode(Qt::ElideRight);
    }
    _tabs->addTab(_friendList, QStringLiteral("好友"));
    _tabs->addTab(_groupList, QStringLiteral("群组"));
    leftLayout->addWidget(_tabs, 1);

    // ---------- 右侧：会话区 ----------
    auto *rightColumn = new QWidget(_splitter);
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    // 会话标题卡片
    auto *headerCard = new QFrame(rightColumn);
    headerCard->setObjectName(QStringLiteral("Card"));
    applyShadow(headerCard, 18, 3, 18);
    auto *headerRow = new QHBoxLayout(headerCard);
    headerRow->setContentsMargins(18, 10, 14, 10);
    headerRow->setSpacing(10);

    auto *headerText = new QVBoxLayout();
    headerText->setSpacing(1);
    auto *headerCaption = new QLabel(QStringLiteral("当前会话"), headerCard);
    headerCaption->setObjectName(QStringLiteral("CardCaption"));
    _titleLabel = new QLabel(QStringLiteral("未选择会话"), headerCard);
    _titleLabel->setObjectName(QStringLiteral("ConversationTitle"));
    headerText->addWidget(headerCaption);
    headerText->addWidget(_titleLabel);

    _membersBtn = new QPushButton(QStringLiteral("成员"), headerCard);
    _membersBtn->setObjectName(QStringLiteral("GhostButton"));
    _membersBtn->setEnabled(false);
    headerRow->addLayout(headerText, 1);
    headerRow->addWidget(_membersBtn);
    rightLayout->addWidget(headerCard);

    // 消息卡片（QML 视图透明背景，圆角与描边由卡片提供）
    auto *messageCard = new QFrame(rightColumn);
    messageCard->setObjectName(QStringLiteral("MessageCard"));
    applyShadow(messageCard, 26, 6, 26);
    auto *messageLayout = new QVBoxLayout(messageCard);
    messageLayout->setContentsMargins(2, 2, 2, 2);

    _emptyModel = new MessageModel(this);
    _messageView = new QQuickWidget(messageCard);
    _messageView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    _messageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _messageView->setClearColor(Qt::transparent);
    _messageView->setStyleSheet(QStringLiteral("background: transparent;"));
    _messageView->rootContext()->setContextProperty(QStringLiteral("messageModel"), _emptyModel);
    _messageView->setSource(QUrl(QStringLiteral("qrc:/qml/MessageView.qml")));
    messageLayout->addWidget(_messageView, 1);
    rightLayout->addWidget(messageCard, 1);

    // 输入卡片
    auto *composerCard = new QFrame(rightColumn);
    composerCard->setObjectName(QStringLiteral("Card"));
    applyShadow(composerCard, 18, 3, 18);
    auto *inputRow = new QHBoxLayout(composerCard);
    inputRow->setContentsMargins(8, 6, 8, 6);
    inputRow->setSpacing(8);

    _inputEdit = new QLineEdit(composerCard);
    _inputEdit->setObjectName(QStringLiteral("ComposerInput"));
    _inputEdit->setPlaceholderText(QStringLiteral("输入消息，回车发送"));
    _inputEdit->setEnabled(false);
    _sendBtn = new QPushButton(QStringLiteral("发送"), composerCard);
    _sendBtn->setObjectName(QStringLiteral("PrimaryButton"));
    _sendBtn->setMinimumWidth(88);
    _sendBtn->setEnabled(false);
    inputRow->addWidget(_inputEdit, 1);
    inputRow->addWidget(_sendBtn);
    rightLayout->addWidget(composerCard);

    _splitter->addWidget(leftCard);
    _splitter->addWidget(rightColumn);
    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 1);
    _splitter->setSizes({280, 760});

    _statusLabel = new QLabel(this);
    _statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    setStatusText(QStringLiteral("未连接"), QStringLiteral("idle"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(_statusLabel, 1);

    // 会话菜单：注销登录
    auto *sessionMenu = menuBar()->addMenu(QStringLiteral("会话"));
    auto *logoutAction = sessionMenu->addAction(QStringLiteral("注销登录"));
    connect(logoutAction, &QAction::triggered, this, &MainWindow::onLogout);

    connect(_friendList, &QListWidget::itemClicked, this, &MainWindow::onFriendClicked);
    connect(_groupList, &QListWidget::itemClicked, this, &MainWindow::onGroupClicked);
    connect(_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(_inputEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(_addFriendBtn, &QPushButton::clicked, this, &MainWindow::onAddFriend);
    connect(_createGroupBtn, &QPushButton::clicked, this, &MainWindow::onCreateGroup);
    connect(_joinGroupBtn, &QPushButton::clicked, this, &MainWindow::onJoinGroup);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshLists);
    connect(_membersBtn, &QPushButton::clicked, this, &MainWindow::onShowMembers);
}

void MainWindow::startSession(const nlohmann::json &ack, const QString &email, const QString &password)
{
    resetSession();
    _email = email;
    _password = password;
    _sessionActive = true;
    applyLoginAck(ack, false);

    setStatusText(QStringLiteral("已连接 · %1").arg(_myName),
                  QStringLiteral("ok"));
    setWindowTitle(QStringLiteral("聊天室 - %1").arg(_myName));
}

void MainWindow::resetSession()
{
    clearConversations();
    _friends.clear();
    _groups.clear();
    _friendItemIds.clear();
    _groupItemIds.clear();
    _friendList->clear();
    _groupList->clear();
    _tabs->setCurrentIndex(0);

    _myId = -1;
    _myName.clear();
    _email.clear();
    _password.clear();
    _sessionActive = false;
    _refreshPending = false;
    _addFriendPending = false;
    _createGroupPending = false;
    _joinGroupPending = false;
    _addFriendBtn->setEnabled(true);
    _createGroupBtn->setEnabled(true);
    _joinGroupBtn->setEnabled(true);

    _titleLabel->setText(QStringLiteral("未选择会话"));
    _membersBtn->setEnabled(false);
    _inputEdit->clear();
    _inputEdit->setEnabled(false);
    _sendBtn->setEnabled(false);
    setStatusText(QStringLiteral("未连接"), QStringLiteral("idle"));
    setWindowTitle(QStringLiteral("聊天室"));
}

void MainWindow::clearConversations()
{
    if (_active != nullptr)
    {
        _messageView->rootContext()->setContextProperty(QStringLiteral("messageModel"), _emptyModel);
        _active = nullptr;
    }
    for (auto it = _conversations.begin(); it != _conversations.end(); ++it)
    {
        delete it.value()->model;
        delete it.value();
    }
    _conversations.clear();
}

void MainWindow::applyLoginAck(const nlohmann::json &ack, bool refresh)
{
    _myId = ack.value("id", _myId);
    _myName = QString::fromStdString(ack.value("name", std::string()));

    // 刷新时保留会话中出现的陌生人 / 未加入群条目，避免刷新后列表里“丢了”会话
    QVector<FriendEntry> strangerFriends;
    QVector<GroupEntry> strangerGroups;
    if (refresh)
    {
        for (const auto &f : _friends)
        {
            if (f.stranger)
            {
                strangerFriends.push_back(f);
            }
        }
        for (const auto &g : _groups)
        {
            if (g.stranger)
            {
                strangerGroups.push_back(g);
            }
        }
    }

    _friends.clear();
    _groups.clear();

    if (ack.contains("friends"))
    {
        for (const auto &entry : ack["friends"])
        {
            try
            {
                const nlohmann::json f = nlohmann::json::parse(entry.get<std::string>());
                FriendEntry fe;
                fe.id = f.value("id", -1);
                fe.name = QString::fromStdString(f.value("name", std::string()));
                fe.email = QString::fromStdString(f.value("email", std::string()));
                fe.state = QString::fromStdString(f.value("state", std::string()));
                if (fe.id > 0)
                {
                    _friends.push_back(fe);
                }
            }
            catch (const std::exception &)
            {
            }
        }
    }

    if (ack.contains("groups"))
    {
        for (const auto &entry : ack["groups"])
        {
            try
            {
                const nlohmann::json g = nlohmann::json::parse(entry.get<std::string>());
                GroupEntry ge;
                ge.id = g.value("id", -1);
                ge.name = QString::fromStdString(g.value("groupname", std::string()));
                ge.desc = QString::fromStdString(g.value("groupdesc", std::string()));
                if (g.contains("users"))
                {
                    for (const auto &userEntry : g["users"])
                    {
                        const nlohmann::json u = nlohmann::json::parse(userEntry.get<std::string>());
                        MemberInfo mi;
                        mi.id = u.value("id", -1);
                        mi.name = QString::fromStdString(u.value("name", std::string()));
                        mi.state = QString::fromStdString(u.value("state", std::string()));
                        mi.role = QString::fromStdString(u.value("role", std::string()));
                        ge.members.push_back(mi);
                    }
                }
                if (ge.id > 0)
                {
                    _groups.push_back(ge);
                }
            }
            catch (const std::exception &)
            {
            }
        }
    }

    // 把刷新前保留的陌生人/未加入群补回列表（若对方已成为正式好友/群成员则跳过）
    for (const auto &sf : strangerFriends)
    {
        bool exists = false;
        for (const auto &f : _friends)
        {
            if (f.id == sf.id)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            _friends.push_back(sf);
        }
    }
    for (const auto &sg : strangerGroups)
    {
        bool exists = false;
        for (const auto &g : _groups)
        {
            if (g.id == sg.id)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
        {
            _groups.push_back(sg);
        }
    }

    rebuildListsFromSession();
    updateActiveHeader();
    // 离线消息：逐条解析后按 msgid 路由
    if (ack.contains("offlinemsg"))
    {
        for (const auto &entry : ack["offlinemsg"])
        {
            try
            {
                const nlohmann::json m = nlohmann::json::parse(entry.get<std::string>());
                appendIncoming(m, true);
            }
            catch (const std::exception &)
            {
            }
        }
    }

    if (refresh)
    {
        setStatusText(QStringLiteral("列表已刷新 · %1").arg(_myName),
                      QStringLiteral("ok"));
        setWindowTitle(QStringLiteral("聊天室 - %1").arg(_myName));
    }
}

void MainWindow::rebuildListsFromSession()
{
    _friendList->clear();
    _groupList->clear();
    _friendItemIds.clear();
    _groupItemIds.clear();

    for (const auto &f : _friends)
    {
        addFriendItem(f.id, f.name, f.email, f.state, f.stranger);
    }
    for (const auto &g : _groups)
    {
        addGroupItem(g.id, g.name, g.desc, g.members, g.stranger);
    }
}

void MainWindow::addFriendItem(int id, const QString &name, const QString &email,
                               const QString &state, bool stranger)
{
    if (id <= 0 || _friendItemIds.contains(id))
    {
        return;
    }

    const QString displayName = name.isEmpty() ? QStringLiteral("未知用户") : name;
    QString label;
    QColor color;
    QString stateText;
    if (stranger)
    {
        label = QStringLiteral("%1  ·  陌生人").arg(displayName);
        color = QColor(QStringLiteral("#8b5cf6"));
        stateText = QStringLiteral("陌生人");
    }
    else if (state == QStringLiteral("online"))
    {
        label = QStringLiteral("%1  ·  在线").arg(displayName);
        color = QColor(QStringLiteral("#2b3442"));
        stateText = QStringLiteral("在线");
    }
    else
    {
        label = QStringLiteral("%1  ·  离线").arg(displayName);
        color = QColor(QStringLiteral("#8a93a3"));
        stateText = QStringLiteral("离线");
    }

    auto *item = new QListWidgetItem(label, _friendList);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, name);
    item->setForeground(color);
    item->setIcon(makeAvatarIcon(displayName, false, state, _friendList->devicePixelRatioF()));
    item->setSizeHint(QSize(0, 50));
    QString emailText = email.isEmpty() ? QString() : QStringLiteral("\n邮箱：%1").arg(email);
    item->setToolTip(QStringLiteral("%1%2\n状态：%3").arg(displayName).arg(emailText).arg(stateText));
    _friendItemIds.insert(id);
}

void MainWindow::addGroupItem(int id, const QString &name, const QString &desc,
                              const QVector<MemberInfo> &members, bool stranger)
{
    Q_UNUSED(desc);
    Q_UNUSED(members);
    if (id <= 0 || _groupItemIds.contains(id))
    {
        return;
    }

    const QString displayName = name.isEmpty() ? QStringLiteral("未命名群组") : name;
    QString label = stranger
                        ? QStringLiteral("%1  ·  未加入").arg(displayName)
                        : QStringLiteral("%1  ·  已加入").arg(displayName);
    auto *item = new QListWidgetItem(label, _groupList);
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, name);
    item->setIcon(makeAvatarIcon(displayName, true, QString(), _groupList->devicePixelRatioF()));
    item->setSizeHint(QSize(0, 50));
    item->setToolTip(QStringLiteral("%1%2")
                         .arg(displayName)
                         .arg(stranger ? QStringLiteral("\n状态：未加入") : QStringLiteral("\n状态：已加入")));
    if (stranger)
    {
        item->setForeground(QColor(QStringLiteral("#8b5cf6")));
    }
    _groupItemIds.insert(id);
}

Conversation *MainWindow::ensureConversation(int id, bool isGroup, const QString &fallbackName)
{
    const QString key = conversationKey(id, isGroup);
    const auto it = _conversations.constFind(key);
    if (it != _conversations.constEnd())
    {
        return it.value();
    }

    auto *conv = new Conversation();
    conv->peerId = id;
    conv->isGroup = isGroup;
    conv->model = new MessageModel(this);

    if (isGroup)
    {
        bool known = false;
        for (const auto &g : _groups)
        {
            if (g.id == id)
            {
                conv->title = QStringLiteral("群：%1").arg(g.name);
                conv->members = g.members;
                conv->desc = g.desc;
                known = true;
                break;
            }
        }
        if (!known)
        {
            const QString name = fallbackName.isEmpty() ? QStringLiteral("群聊") : fallbackName;
            conv->title = QStringLiteral("群：%1").arg(name);
            if (!_groupItemIds.contains(id))
            {
                GroupEntry entry;
                entry.id = id;
                entry.name = name;
                entry.stranger = true;
                _groups.push_back(entry);
                addGroupItem(id, name, QString(), QVector<MemberInfo>(), true);
            }
        }
    }
    else
    {
        bool known = false;
        for (const auto &f : _friends)
        {
            if (f.id == id)
            {
                conv->title = QStringLiteral("与 %1 聊天").arg(f.name);
                known = true;
                break;
            }
        }
        if (!known)
        {
            const QString name = fallbackName.isEmpty() ? QStringLiteral("未知用户") : fallbackName;
            conv->title = QStringLiteral("与 %1 聊天").arg(name);
            if (!_friendItemIds.contains(id))
            {
                FriendEntry entry;
                entry.id = id;
                entry.name = name;
                entry.state = QStringLiteral("unknown");
                entry.stranger = true;
                _friends.push_back(entry);
                addFriendItem(id, name, QString(), entry.state, true);
            }
        }
    }

    _conversations.insert(key, conv);
    return conv;
}

void MainWindow::switchConversation(Conversation *conv)
{
    if (conv == nullptr)
    {
        return;
    }
    _active = conv;
    _messageView->rootContext()->setContextProperty(QStringLiteral("messageModel"), conv->model);
    updateActiveHeader();
    _inputEdit->setEnabled(true);
    _sendBtn->setEnabled(true);
    _inputEdit->setFocus();

    // 切换会话时的淡入过渡，避免消息区内容突兀跳变
    auto *fade = new QPropertyAnimation(_messageView, "opacity", this);
    fade->setDuration(180);
    fade->setStartValue(0.25);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::updateActiveHeader()
{
    if (_active == nullptr)
    {
        _titleLabel->setText(QStringLiteral("未选择会话"));
        _membersBtn->setEnabled(false);
        return;
    }

    // 群信息可能在刷新列表后更新
    if (_active->isGroup)
    {
        for (const auto &g : _groups)
        {
            if (g.id == _active->peerId)
            {
                _active->members = g.members;
                _active->desc = g.desc;
                if (!g.stranger)
                {
                    _active->title = QStringLiteral("群：%1").arg(g.name);
                }
                break;
            }
        }
    }

    _titleLabel->setText(_active->title);
    _membersBtn->setEnabled(_active->isGroup && !_active->members.isEmpty());
}

void MainWindow::onFriendClicked(QListWidgetItem *item)
{
    if (item == nullptr || !_sessionActive)
    {
        return;
    }
    const int id = item->data(Qt::UserRole).toInt();
    const QString name = item->data(Qt::UserRole + 1).toString();
    switchConversation(ensureConversation(id, false, name));
}

void MainWindow::onGroupClicked(QListWidgetItem *item)
{
    if (item == nullptr || !_sessionActive)
    {
        return;
    }
    const int id = item->data(Qt::UserRole).toInt();
    const QString name = item->data(Qt::UserRole + 1).toString();
    switchConversation(ensureConversation(id, true, name));
}

void MainWindow::onSendClicked()
{
    if (!_sessionActive || _active == nullptr)
    {
        return;
    }
    if (!_client->isConnected())
    {
        setStatusText(QStringLiteral("连接已断开，消息未发送"), QStringLiteral("error"));
        return;
    }

    const QString text = _inputEdit->text().trimmed();
    if (text.isEmpty())
    {
        return;
    }
    // 发送前先做长度预检，与服务端限制保持一致
    if (text.toStdString().size() > MAX_TEXT_LEN)
    {
        setStatusText(QStringLiteral("消息过长（超过 %1 字节），未发送").arg(MAX_TEXT_LEN),
                      QStringLiteral("error"));
        return;
    }

    const QString time = nowText();
    if (_active->isGroup)
    {
        _client->sendGroupChat(_myId, _myName, _active->peerId, text, time);
    }
    else
    {
        _client->sendOneChat(_myId, _myName, _active->peerId, text, time);
    }

    MessageModel::Message msg;
    msg.senderId = _myId;
    msg.senderName = _myName;
    msg.text = text;
    msg.time = time;
    msg.mine = true;
    msg.offline = false;
    _active->model->appendMessage(msg);

    _inputEdit->clear();
    _inputEdit->setFocus();
}

void MainWindow::appendIncoming(const nlohmann::json &js, bool offline)
{
    if (!js.contains("msgid"))
    {
        return;
    }
    const int msgid = js["msgid"].get<int>();
    if (msgid != ONE_CHAT_MSG && msgid != GROUP_CHAT_MSG)
    {
        return;
    }

    const int senderId = js.value("id", -1);
    const QString senderName = QString::fromStdString(js.value("name", std::string()));
    const QString text = QString::fromStdString(js.value("msg", std::string()));
    const QString time = js.contains("time")
                             ? QString::fromStdString(js.value("time", std::string()))
                             : nowText();

    Conversation *conv = nullptr;
    if (msgid == GROUP_CHAT_MSG)
    {
        const int groupId = js.value("groupid", -1);
        if (groupId <= 0)
        {
            return;
        }
        conv = ensureConversation(groupId, true, QString());
    }
    else
    {
        if (senderId <= 0)
        {
            return;
        }
        conv = ensureConversation(senderId, false, senderName);
    }

    MessageModel::Message msg;
    msg.senderId = senderId;
    msg.senderName = senderName;
    msg.text = text;
    msg.time = time;
    msg.mine = (senderId == _myId);
    msg.offline = offline;
    conv->model->appendMessage(msg);

    // 当前没有打开任何会话时，自动切换到新消息所在会话，避免消息“看不见”
    if (_active == nullptr)
    {
        switchConversation(conv);
    }
}

void MainWindow::onAddFriend()
{
    if (!_sessionActive || _addFriendPending)
    {
        return;
    }

    bool ok = false;
    const QString account =
        QInputDialog::getText(this, QStringLiteral("添加好友"),
                              QStringLiteral("请输入对方的邮箱或用户名："), QLineEdit::Normal,
                              QString(), &ok)
            .trimmed();
    if (!ok)
    {
        return;
    }
    if (account.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("添加好友"),
                             QStringLiteral("邮箱或用户名不能为空"));
        return;
    }

    _addFriendPending = true;
    _addFriendBtn->setEnabled(false);
    _client->sendAddFriend(_myId, account);
    setStatusText(QStringLiteral("正在查找用户 %1 ...").arg(account), QStringLiteral("idle"));
}

void MainWindow::onAddFriendResult(const nlohmann::json &js)
{
    _addFriendPending = false;
    _addFriendBtn->setEnabled(true);

    if (!_sessionActive)
    {
        return;
    }

    const int errno_ = js.value("errno", 1);
    if (errno_ == 0)
    {
        const QString name = QString::fromStdString(js.value("name", std::string()));
        setStatusText(QStringLiteral("已添加好友：%1，刷新列表后可见").arg(name),
                      QStringLiteral("ok"));
    }
    else if (errno_ == 1)
    {
        setStatusText(QStringLiteral("未找到该用户，请确认邮箱或用户名"), QStringLiteral("error"));
    }
    else if (errno_ == 2)
    {
        setStatusText(QStringLiteral("不能添加自己为好友"), QStringLiteral("error"));
    }
    else
    {
        setStatusText(QStringLiteral("添加好友失败：%1")
                          .arg(QString::fromStdString(js.value("errmsg", std::string()))),
                      QStringLiteral("error"));
    }
}

void MainWindow::onCreateGroup()
{
    if (!_sessionActive || _createGroupPending)
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("创建群组"));
    auto *layout = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(&dialog);
    auto *descEdit = new QLineEdit(&dialog);
    descEdit->setPlaceholderText(QStringLiteral("可留空"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(QStringLiteral("群名称"), nameEdit);
    layout->addRow(QStringLiteral("群描述"), descEdit);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("创建群组"), QStringLiteral("群名称不能为空"));
        return;
    }

    _createGroupPending = true;
    _createGroupBtn->setEnabled(false);
    _client->sendCreateGroup(_myId, name, descEdit->text().trimmed());
    setStatusText(QStringLiteral("正在创建群组 %1 ...").arg(name), QStringLiteral("idle"));
}

void MainWindow::onCreateGroupResult(const nlohmann::json &js)
{
    _createGroupPending = false;
    _createGroupBtn->setEnabled(true);

    if (!_sessionActive)
    {
        return;
    }

    const int errno_ = js.value("errno", 1);
    if (errno_ == 0)
    {
        const QString name = QString::fromStdString(js.value("groupname", std::string()));
        setStatusText(QStringLiteral("群组「%1」创建成功，刷新列表后可见").arg(name),
                      QStringLiteral("ok"));
    }
    else
    {
        setStatusText(QStringLiteral("创建群组失败：%1")
                          .arg(QString::fromStdString(js.value("errmsg", std::string()))),
                      QStringLiteral("error"));
    }
}

void MainWindow::onJoinGroup()
{
    if (!_sessionActive || _joinGroupPending)
    {
        return;
    }

    bool ok = false;
    const QString groupname =
        QInputDialog::getText(this, QStringLiteral("加入群组"),
                              QStringLiteral("请输入群名称："), QLineEdit::Normal, QString(),
                              &ok)
            .trimmed();
    if (!ok)
    {
        return;
    }
    if (groupname.isEmpty())
    {
        QMessageBox::warning(this, QStringLiteral("加入群组"),
                             QStringLiteral("群名称不能为空"));
        return;
    }

    _joinGroupPending = true;
    _joinGroupBtn->setEnabled(false);
    _client->sendAddGroup(_myId, groupname);
    setStatusText(QStringLiteral("正在查找群组 %1 ...").arg(groupname), QStringLiteral("idle"));
}

void MainWindow::onAddGroupResult(const nlohmann::json &js)
{
    if (!_sessionActive)
    {
        return;
    }

    const int errno_ = js.value("errno", 1);

    // 候选列表场景（errno 2）需要用户二次选择后重发，其余场景直接恢复按钮
    if (errno_ != 2)
    {
        _joinGroupPending = false;
        _joinGroupBtn->setEnabled(true);
    }

    if (errno_ == 0)
    {
        const QString name = QString::fromStdString(js.value("groupname", std::string()));
        setStatusText(QStringLiteral("已加入群组：%1，刷新列表后可见").arg(name),
                      QStringLiteral("ok"));
        return;
    }

    if (errno_ == 2)
    {
        // 同名群组：展示候选列表，按序号选择（不暴露群 ID）
        QStringList items;
        QStringList names;
        if (js.contains("groups") && js["groups"].is_array())
        {
            int index = 0;
            for (const auto &item : js["groups"])
            {
                try
                {
                    const nlohmann::json group =
                        item.is_string() ? nlohmann::json::parse(item.get<std::string>()) : item;
                    const QString name =
                        QString::fromStdString(group.value("name", std::string()));
                    const QString desc =
                        QString::fromStdString(group.value("desc", std::string()));
                    const QString label = desc.isEmpty() ? name : QStringLiteral("%1 — %2").arg(name, desc);
                    items << QStringLiteral("%1（第 %2 个）").arg(label).arg(index + 1);
                    names << name;
                    ++index;
                }
                catch (const std::exception &)
                {
                    // 忽略非法候选项
                }
            }
        }

        if (items.isEmpty())
        {
            setStatusText(QStringLiteral("加入群组失败：候选群组解析异常"),
                          QStringLiteral("error"));
            return;
        }

        bool ok = false;
        const QString chosen =
            QInputDialog::getItem(this, QStringLiteral("选择群组"),
                                  QStringLiteral("存在同名群组，请选择一个："), items, 0, false, &ok);
        if (!ok)
        {
            return;
        }

        const int selected = items.indexOf(chosen);
        if (selected < 0)
        {
            _joinGroupPending = false;
            _joinGroupBtn->setEnabled(true);
            return;
        }

        _client->sendAddGroup(_myId, names.at(selected), selected);
        setStatusText(QStringLiteral("正在加入群组..."), QStringLiteral("idle"));
        return;
    }

    if (errno_ == 1)
    {
        setStatusText(QStringLiteral("未找到该群组，请确认群名称"), QStringLiteral("error"));
    }
    else if (errno_ == 3)
    {
        setStatusText(QStringLiteral("群名称不能为空"), QStringLiteral("error"));
    }
    else
    {
        setStatusText(QStringLiteral("加入群组失败：%1")
                          .arg(QString::fromStdString(js.value("errmsg", std::string()))),
                      QStringLiteral("error"));
    }
}

void MainWindow::onChatSendResult(const nlohmann::json &js)
{
    if (!_sessionActive)
    {
        return;
    }

    const int errno_ = js.value("errno", 1);
    if (errno_ == 1)
    {
        setStatusText(QStringLiteral("消息发送失败：目标不存在"), QStringLiteral("error"));
    }
    else if (errno_ == 2)
    {
        setStatusText(QStringLiteral("消息发送失败：内容为空或超过 %1 字节").arg(MAX_TEXT_LEN),
                      QStringLiteral("error"));
    }
    else if (errno_ == 3)
    {
        setStatusText(QStringLiteral("消息发送失败：你不在该群组中"), QStringLiteral("error"));
    }
    else
    {
        setStatusText(QStringLiteral("消息发送失败：%1")
                          .arg(QString::fromStdString(js.value("errmsg", std::string()))),
                      QStringLiteral("error"));
    }
}

void MainWindow::onRefreshLists()
{
    if (!_sessionActive || _myId <= 0)
    {
        return;
    }

    _refreshPending = true;
    setStatusText(QStringLiteral("正在刷新列表..."), QStringLiteral("idle"));

    // 服务端 loginout 只清理映射与在线状态，不关闭连接：
    // 同一连接上顺序执行 LOGINOUT + LOGIN，用最新的 LOGIN_ACK 重建列表
    _client->sendLoginout(_myId);
    _client->sendLogin(_email, _password);
}

void MainWindow::onLoginResult(const nlohmann::json &js)
{
    if (!_refreshPending)
    {
        return;
    }
    _refreshPending = false;

    if (js.value("errno", 1) != 0)
    {
        setStatusText(QStringLiteral("刷新失败：重新登录未通过"), QStringLiteral("error"));
        return;
    }

    applyLoginAck(js, true);
}

void MainWindow::onChatMessage(const nlohmann::json &js)
{
    appendIncoming(js, false);
}

void MainWindow::onDisconnected()
{
    if (!_sessionActive)
    {
        return;
    }

    resetSession();
    QMessageBox::warning(this, QStringLiteral("连接断开"),
                         QStringLiteral("与服务器的连接已断开，请重新登录。"));
    emit loggedOut();
}

void MainWindow::onSocketError(const QString &message)
{
    if (!_sessionActive)
    {
        return;
    }
    setStatusText(QStringLiteral("网络错误：%1").arg(message), QStringLiteral("error"));
}

void MainWindow::onShowMembers()
{
    if (_active == nullptr || !_active->isGroup)
    {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("群成员 - %1").arg(_active->title));
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(_active->members.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("名称"),
                                      QStringLiteral("角色"), QStringLiteral("状态")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);

    for (int row = 0; row < _active->members.size(); ++row)
    {
        const MemberInfo &member = _active->members.at(row);
        table->setItem(row, 0, new QTableWidgetItem(member.name));
        table->setItem(row, 1, new QTableWidgetItem(member.role));
        table->setItem(row, 2, new QTableWidgetItem(member.state));
    }

    layout->addWidget(table);
    dialog.resize(520, 360);
    dialog.exec();
}

void MainWindow::onLogout()
{
    if (!_sessionActive)
    {
        return;
    }
    _client->sendLoginout(_myId);
    resetSession();
    emit loggedOut();
}

void MainWindow::setStatusText(const QString &text, const QString &state)
{
    _statusLabel->setText(text);
    if (_statusLabel->property("state").toString() == state)
    {
        return;
    }
    _statusLabel->setProperty("state", state);
    _statusLabel->style()->unpolish(_statusLabel);
    _statusLabel->style()->polish(_statusLabel);
    _statusLabel->update();
}

QString MainWindow::nowText() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}
