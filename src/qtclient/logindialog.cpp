#include "logindialog.h"

#include <QColor>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>

#include "chatclient.h"
#include "public.hpp"

LoginDialog::LoginDialog(ChatClient *client, QWidget *parent)
    : QDialog(parent),
      _client(client)
{
    buildUi();

    connect(_client, &ChatClient::connected, this, &LoginDialog::onConnected);
    connect(_client, &ChatClient::disconnected, this, &LoginDialog::onDisconnected);
    connect(_client, &ChatClient::socketError, this, &LoginDialog::onSocketError);
    connect(_client, &ChatClient::loginResult, this, &LoginDialog::onLoginResult);
    connect(_client, &ChatClient::registerResult, this, &LoginDialog::onRegisterResult);
}

void LoginDialog::buildUi()
{
    setWindowTitle(QStringLiteral("聊天客户端 - 登录"));
    setModal(true);
    setMinimumWidth(480);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(0);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("LoginCard"));
    auto *cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setBlurRadius(36);
    cardShadow->setOffset(0, 8);
    cardShadow->setColor(QColor(23, 43, 77, 38));
    card->setGraphicsEffect(cardShadow);

    auto *mainLayout = new QVBoxLayout(card);
    mainLayout->setContentsMargins(28, 26, 28, 22);
    mainLayout->setSpacing(16);

    // 品牌区
    auto *title = new QLabel(QStringLiteral("聊天室"), card);
    title->setObjectName(QStringLiteral("AppTitle"));
    auto *subtitle = new QLabel(QStringLiteral("登录后与好友开始聊天"), card);
    subtitle->setObjectName(QStringLiteral("AppSubtitle"));
    auto *brandBox = new QVBoxLayout();
    brandBox->setSpacing(3);
    brandBox->addWidget(title);
    brandBox->addWidget(subtitle);
    mainLayout->addLayout(brandBox);

    // 服务器配置
    auto *serverGroup = new QGroupBox(QStringLiteral("服务器连接"), card);
    auto *serverLayout = new QFormLayout(serverGroup);
    serverLayout->setContentsMargins(4, 4, 4, 4);
    serverLayout->setHorizontalSpacing(14);
    serverLayout->setVerticalSpacing(10);
    serverLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    serverLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    _hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), serverGroup);
    _portSpin = new QSpinBox(serverGroup);
    _portSpin->setRange(1, 65535);
    _portSpin->setValue(6000);
    serverLayout->addRow(QStringLiteral("地址"), _hostEdit);
    serverLayout->addRow(QStringLiteral("端口"), _portSpin);
    mainLayout->addWidget(serverGroup);

    // 登录 / 注册
    _tabs = new QTabWidget(card);

    auto *loginPage = new QWidget(_tabs);
    auto *loginPageLayout = new QVBoxLayout(loginPage);
    loginPageLayout->setContentsMargins(0, 0, 0, 4);
    loginPageLayout->setSpacing(0);
    auto *loginLayout = new QFormLayout();
    loginLayout->setContentsMargins(2, 12, 2, 2);
    loginLayout->setHorizontalSpacing(14);
    loginLayout->setVerticalSpacing(14);
    loginLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    loginLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    _loginEmailEdit = new QLineEdit(loginPage);
    _loginEmailEdit->setPlaceholderText(QStringLiteral("注册时填写的邮箱"));
    _loginPwdEdit = new QLineEdit(loginPage);
    _loginPwdEdit->setEchoMode(QLineEdit::Password);
    _loginPwdEdit->setPlaceholderText(QStringLiteral("登录密码"));
    _loginBtn = new QPushButton(QStringLiteral("登录"), loginPage);
    _loginBtn->setObjectName(QStringLiteral("PrimaryButton"));
    _loginBtn->setDefault(true);
    loginLayout->addRow(QStringLiteral("邮箱"), _loginEmailEdit);
    loginLayout->addRow(QStringLiteral("密码"), _loginPwdEdit);
    loginLayout->addRow(_loginBtn);

    auto *loginHint = new QLabel(QStringLiteral("还没有账号？切换到「注册」，使用邮箱创建账号。"),
                                 loginPage);
    loginHint->setObjectName(QStringLiteral("HintLabel"));
    loginHint->setWordWrap(true);
    loginHint->setAlignment(Qt::AlignCenter);

    loginPageLayout->addLayout(loginLayout);
    loginPageLayout->addStretch(1);
    loginPageLayout->addWidget(loginHint);
    _tabs->addTab(loginPage, QStringLiteral("登录"));

    auto *regPage = new QWidget(_tabs);
    auto *regPageLayout = new QVBoxLayout(regPage);
    regPageLayout->setContentsMargins(0, 0, 0, 4);
    regPageLayout->setSpacing(0);
    auto *regLayout = new QFormLayout();
    regLayout->setContentsMargins(2, 12, 2, 2);
    regLayout->setHorizontalSpacing(14);
    regLayout->setVerticalSpacing(14);
    regLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    regLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    _regNameEdit = new QLineEdit(regPage);
    _regNameEdit->setPlaceholderText(QStringLiteral("用户名（唯一）"));
    _regEmailEdit = new QLineEdit(regPage);
    _regEmailEdit->setPlaceholderText(QStringLiteral("you@example.com（作为登录账号）"));
    _regPwdEdit = new QLineEdit(regPage);
    _regPwdEdit->setEchoMode(QLineEdit::Password);
    _regPwdEdit->setPlaceholderText(QStringLiteral("注册密码"));
    _regBtn = new QPushButton(QStringLiteral("注册"), regPage);
    _regBtn->setObjectName(QStringLiteral("PrimaryButton"));

    regLayout->addRow(QStringLiteral("用户名"), _regNameEdit);
    regLayout->addRow(QStringLiteral("邮箱"), _regEmailEdit);
    regLayout->addRow(QStringLiteral("密码"), _regPwdEdit);
    regLayout->addRow(_regBtn);

    auto *regHint = new QLabel(QStringLiteral("邮箱将作为登录账号，注册后使用邮箱 + 密码登录。"),
                               regPage);
    regHint->setObjectName(QStringLiteral("HintLabel"));
    regHint->setWordWrap(true);
    regHint->setAlignment(Qt::AlignCenter);

    regPageLayout->setContentsMargins(0, 0, 0, 4);
    regPageLayout->addLayout(regLayout);
    regPageLayout->addStretch(1);
    regPageLayout->addSpacing(8);
    regPageLayout->addWidget(regHint);
    _tabs->addTab(regPage, QStringLiteral("注册"));

    mainLayout->addWidget(_tabs);

    auto *divider = new QFrame(card);
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(divider);

    _statusLabel = new QLabel(card);
    _statusLabel->setObjectName(QStringLiteral("StatusLabel"));
    _statusLabel->setWordWrap(true);
    _statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _statusLabel->setText(QStringLiteral("请输入邮箱与密码登录"));
    setStatusState(QStringLiteral("idle"));
    mainLayout->addWidget(_statusLabel);

    outerLayout->addWidget(card);

    // 以较高的“注册”页为基准自适应窗口高度，避免切页时内容被压缩
    _tabs->setCurrentIndex(1);
    adjustSize();
    _tabs->setCurrentIndex(0);

    connect(_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(_regBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(_loginPwdEdit, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
    connect(_regPwdEdit, &QLineEdit::returnPressed, this, &LoginDialog::onRegisterClicked);
}

void LoginDialog::setStatusState(const QString &state)
{
    if (_statusLabel->property("state").toString() == state)
    {
        return;
    }
    _statusLabel->setProperty("state", state);
    _statusLabel->style()->unpolish(_statusLabel);
    _statusLabel->style()->polish(_statusLabel);
    _statusLabel->update();
}

void LoginDialog::showStatus(const QString &text, bool error)
{
    setStatusState(error ? QStringLiteral("error") : QStringLiteral("ok"));
    _statusLabel->setText(text);
}

bool LoginDialog::takeLoginInput(QString *email, QString *password)
{
    const QString value = _loginEmailEdit->text().trimmed();
    if (!isValidEmail(value.toStdString()))
    {
        showStatus(QStringLiteral("请输入正确的邮箱地址"), true);
        return false;
    }
    if (_loginPwdEdit->text().isEmpty())
    {
        showStatus(QStringLiteral("密码不能为空"), true);
        return false;
    }
    *email = value;
    *password = _loginPwdEdit->text();
    return true;
}

bool LoginDialog::takeRegisterInput(QString *name, QString *email, QString *password)
{
    if (_regNameEdit->text().trimmed().isEmpty())
    {
        showStatus(QStringLiteral("用户名不能为空"), true);
        return false;
    }

    const QString emailValue = _regEmailEdit->text().trimmed();
    if (!isValidEmail(emailValue.toStdString()))
    {
        showStatus(QStringLiteral("请输入正确的邮箱地址"), true);
        return false;
    }

    if (_regPwdEdit->text().isEmpty())
    {
        showStatus(QStringLiteral("密码不能为空"), true);
        return false;
    }

    *name = _regNameEdit->text().trimmed();
    *email = emailValue;
    *password = _regPwdEdit->text();
    return true;
}

void LoginDialog::onLoginClicked()
{
    QString email;
    QString password;
    if (!takeLoginInput(&email, &password))
    {
        return;
    }

    if (_client->isConnected())
    {
        doLogin();
        return;
    }

    _pending = Pending::Login;
    setBusy(true, QStringLiteral("正在连接服务器..."));
    _client->connectToServer(_hostEdit->text().trimmed(), static_cast<quint16>(_portSpin->value()));
}

void LoginDialog::onRegisterClicked()
{
    QString name;
    QString email;
    QString password;
    if (!takeRegisterInput(&name, &email, &password))
    {
        return;
    }

    if (_client->isConnected())
    {
        doRegister();
        return;
    }

    _pending = Pending::Register;
    setBusy(true, QStringLiteral("正在连接服务器..."));
    _client->connectToServer(_hostEdit->text().trimmed(), static_cast<quint16>(_portSpin->value()));
}

void LoginDialog::doLogin()
{
    QString email;
    QString password;
    if (!takeLoginInput(&email, &password))
    {
        return;
    }
    _pending = Pending::Login;
    setBusy(true, QStringLiteral("正在登录..."));
    _client->sendLogin(email, password);
}

void LoginDialog::doRegister()
{
    QString name;
    QString email;
    QString password;
    if (!takeRegisterInput(&name, &email, &password))
    {
        return;
    }
    _pending = Pending::Register;
    setBusy(true, QStringLiteral("正在注册..."));
    _client->sendRegister(name, email, password);
}

void LoginDialog::onConnected()
{
    _hostEdit->setEnabled(false);
    _portSpin->setEnabled(false);

    if (_pending == Pending::Login)
    {
        doLogin();
    }
    else if (_pending == Pending::Register)
    {
        doRegister();
    }
}

void LoginDialog::onDisconnected()
{
    _hostEdit->setEnabled(true);
    _portSpin->setEnabled(true);
    if (_pending != Pending::None)
    {
        _pending = Pending::None;
        setBusy(false, QStringLiteral("与服务器的连接已断开"));
        setStatusState(QStringLiteral("error"));
    }
}

void LoginDialog::onSocketError(const QString &message)
{
    _hostEdit->setEnabled(true);
    _portSpin->setEnabled(true);
    if (_pending != Pending::None)
    {
        _pending = Pending::None;
        setBusy(false, QStringLiteral("连接失败：%1").arg(message));
        setStatusState(QStringLiteral("error"));
    }
}

void LoginDialog::onLoginResult(const nlohmann::json &js)
{
    if (_pending != Pending::Login)
    {
        return; // 主窗口刷新列表时的 ACK，由主窗口处理
    }
    _pending = Pending::None;
    setBusy(false);

    const int errno_ = js.value("errno", 1);
    if (errno_ == 0)
    {
        const QString email = _loginEmailEdit->text().trimmed();
        const QString password = _loginPwdEdit->text();
        showStatus(QStringLiteral("登录成功"), false);
        emit loggedIn(js, email, password);
        return;
    }

    setStatusState(QStringLiteral("error"));
    if (errno_ == 2)
    {
        _statusLabel->setText(QStringLiteral("该账号已在线，请先在别处注销"));
    }
    else if (errno_ == 3)
    {
        _statusLabel->setText(QStringLiteral("失败次数过多，账号已临时锁定，请稍后再试"));
    }
    else
    {
        _statusLabel->setText(QStringLiteral("邮箱或密码错误"));
    }
}

void LoginDialog::onRegisterResult(const nlohmann::json &js)
{
    if (_pending != Pending::Register)
    {
        return;
    }
    _pending = Pending::None;
    setBusy(false);

    const int errno_ = js.value("errno", 1);
    if (errno_ == 0)
    {
        const QString email = _regEmailEdit->text().trimmed();
        QMessageBox::information(this, QStringLiteral("注册成功"),
                                 QStringLiteral("注册成功，请使用邮箱 %1 登录。").arg(email));
        _loginEmailEdit->setText(email);
        _loginPwdEdit->setText(_regPwdEdit->text());
        _regNameEdit->clear();
        _regEmailEdit->clear();
        _regPwdEdit->clear();
        _tabs->setCurrentIndex(0);
        showStatus(QStringLiteral("注册成功，请使用邮箱登录"), false);
    }
    else if (errno_ == 1)
    {
        showStatus(QStringLiteral("用户名或邮箱已被注册，请更换后重试"), true);
    }
    else
    {
        showStatus(QStringLiteral("注册信息不完整或邮箱格式错误"), true);
    }
}

void LoginDialog::setBusy(bool busy, const QString &hint)
{
    _loginBtn->setEnabled(!busy);
    _regBtn->setEnabled(!busy);
    _loginEmailEdit->setEnabled(!busy);
    _loginPwdEdit->setEnabled(!busy);
    _regNameEdit->setEnabled(!busy);
    _regEmailEdit->setEnabled(!busy);
    _regPwdEdit->setEnabled(!busy);
    if (!hint.isEmpty())
    {
        setStatusState(QStringLiteral("idle"));
        _statusLabel->setText(hint);
    }
}

void LoginDialog::prepareForLogin()
{
    _pending = Pending::None;
    _loginPwdEdit->clear();
    _regPwdEdit->clear();
    setBusy(false, QStringLiteral("已注销，请重新登录"));
    _hostEdit->setEnabled(!_client->isConnected());
    _portSpin->setEnabled(!_client->isConnected());
    _loginEmailEdit->setFocus();
}
