#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

#include "json.hpp"

class ChatClient;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QTabWidget;

// 登录 / 注册对话框，同时负责设置服务器地址与端口
// 登录使用邮箱 + 密码；注册使用用户名 + 邮箱 + 密码，邮箱仅作为账号标识
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(ChatClient *client, QWidget *parent = nullptr);

    // 注销后重新展示登录页时调用
    void prepareForLogin();

signals:
    // 登录成功，携带服务端 ACK、邮箱与密码（密码用于会话内静默重登刷新列表）
    void loggedIn(const nlohmann::json &ack, const QString &email, const QString &password);

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onConnected();
    void onDisconnected();
    void onSocketError(const QString &message);
    void onLoginResult(const nlohmann::json &js);
    void onRegisterResult(const nlohmann::json &js);

private:
    enum class Pending
    {
        None,
        Login,
        Register
    };

    void buildUi();
    void doLogin();
    void doRegister();
    void setBusy(bool busy, const QString &hint = QString());
    void showStatus(const QString &text, bool error);
    void setStatusState(const QString &state);
    bool takeLoginInput(QString *email, QString *password);
    bool takeRegisterInput(QString *name, QString *email, QString *password);

    ChatClient *_client = nullptr;
    Pending _pending = Pending::None;

    QLineEdit *_hostEdit = nullptr;
    QSpinBox *_portSpin = nullptr;
    QTabWidget *_tabs = nullptr;

    QLineEdit *_loginEmailEdit = nullptr;
    QLineEdit *_loginPwdEdit = nullptr;
    QPushButton *_loginBtn = nullptr;

    QLineEdit *_regNameEdit = nullptr;
    QLineEdit *_regEmailEdit = nullptr;
    QLineEdit *_regPwdEdit = nullptr;
    QPushButton *_regBtn = nullptr;

    QLabel *_statusLabel = nullptr;
};

#endif // LOGINDIALOG_H
