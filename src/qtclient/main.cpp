#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QStyleFactory>

#include "chatclient.h"
#include "logindialog.h"
#include "mainwindow.h"

namespace
{
// 加载统一主题样式表；缺失时保持 Qt 默认外观，不影响功能
void applyTheme(QApplication &app)
{
    QFile file(QStringLiteral(":/assets/theme.qss"));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
    }
}
}

int main(int argc, char *argv[])
{
    // 非整数缩放（125% / 150%）不做取整，避免控件错位与字体模糊
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ChatGui"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/chat.svg")));
    // Fusion 在 Windows / Linux 上表现一致，便于统一自绘样式
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    applyTheme(app);

    ChatClient client;
    MainWindow window(&client);
    LoginDialog loginDialog(&client);

    // 登录成功：隐藏登录框，建立会话并展示主窗口
    QObject::connect(&loginDialog, &LoginDialog::loggedIn, &app,
                     [&](const nlohmann::json &ack, const QString &email, const QString &password) {
                         loginDialog.hide();
                         window.startSession(ack, email, password);
                         window.show();
                         window.raise();
                     });

    // 注销或断线：回到登录对话框
    QObject::connect(&window, &MainWindow::loggedOut, &app, [&]() {
        window.hide();
        loginDialog.prepareForLogin();
        loginDialog.show();
        loginDialog.raise();
        loginDialog.activateWindow();
    });

    loginDialog.show();
    return app.exec();
}
