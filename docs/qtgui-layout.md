# Qt 图形客户端布局与集成说明

## 1. 概述

`ChatGui` 是现有聊天服务器的图形客户端，采用 **QWidget（窗口/控件） + QML（消息气泡视图）** 混合方案，Qt 6.4 / C++17，复用服务端既有的「4 字节网络序长度 + JSON」分帧协议，服务端零改动。

| 目标 | 说明 |
| --- | --- |
| `bin/ChatServer` | 服务端（未改动） |
| `bin/ChatClient` | 终端客户端（未改动） |
| `bin/ChatGui` | 新增图形客户端 |

## 2. 文件清单

```
src/qtclient/
├── CMakeLists.txt          # Qt6 目标定义（AUTOMOC / AUTORCC）
├── main.cpp                # 入口：创建 ChatClient / MainWindow / LoginDialog 并连接信号
├── chatclient.h/.cpp       # 通信层：QTcpSocket + 分帧 + JSON + 信号
├── logindialog.h/.cpp      # 登录 / 注册对话框（含服务器地址端口）
├── mainwindow.h/.cpp       # 主窗口：布局、会话管理、消息路由
├── messagemodel.h/.cpp     # QAbstractListModel，供 QML ListView 使用
├── conversation.h          # 会话结构体（Conversation / MemberInfo）
├── qml/MessageView.qml     # 消息气泡视图（纯展示）
├── assets/chat.svg         # 窗口图标（占位资源）
├── assets/theme.qss        # 全局主题样式（配色 / 圆角 / 交互状态）
└── resources.qrc           # 打包 QML、主题与图标，运行时路径 qrc:/...
```

## 3. 界面布局

### 3.1 登录对话框（LoginDialog）

```
QDialog（背景 #f2f5f9，宽 480，自适应高度）
└── QVBoxLayout(margin 16)
    └── QFrame#LoginCard（白底 / 14px 圆角 / 投影）
        └── QVBoxLayout(margin 28,26,28,22；spacing 16)
            ├── 品牌区：QLabel#AppTitle「聊天室」+ QLabel#AppSubtitle「登录后与好友开始聊天」
            ├── QGroupBox「服务器连接」
            │   └── QFormLayout：地址 QLineEdit(127.0.0.1) / 端口 QSpinBox(6000, 1–65535)
            ├── QTabWidget（胶囊式页签）
            │   ├── Tab「登录」：邮箱 QLineEdit / 密码 QLineEdit(Password) / 登录 QPushButton#PrimaryButton
            │   │   └── 底部留白 + 提示 QLabel#HintLabel（引导到注册页）
            │   └── Tab「注册」：用户名 / 邮箱 / 密码 / 注册 QPushButton#PrimaryButton
            │       └── 底部提示 QLabel#HintLabel（邮箱作为登录账号说明）
            ├── QFrame#Divider（1px 分隔线）
            └── QLabel#StatusLabel（连接中 / 登录失败 / 注册成功，按 state 属性着色）
```

### 3.2 主窗口（MainWindow）

```
QMainWindow
├── QMenuBar「会话」→「注销登录」
├── 中央 QWidget（背景 #f2f5f9，外边距 16,12,16,12）
│   └── QSplitter(Horizontal, handle 12, 不可折叠)
│       ├── QFrame#Card 左侧会话卡（minWidth 250 / maxWidth 380，投影）
│       │   └── QVBoxLayout(margin 14,16,14,12；spacing 14)
│       │       ├── 品牌区：AppTitle「聊天室」/ AppSubtitle「与好友保持联系」
│       │       ├── 标题行：SectionTitle「会话列表」+ CardCaption「好友 · 群组」
│       │       ├── QGridLayout（2×2）：加好友 / 创建群 / 加入群 / 刷新列表（SoftButton）
│       │       └── QTabWidget（stretch 1）
│       │           ├── 「好友」QListWidget：36px 首字母头像 + 在线状态点 + 名称 · 在线状态
│       │           └── 「群组」QListWidget：36px 圆角群头像 + 群名 · 已加入/未加入
│       └── 右侧列 QWidget（stretch 1）
│           └── QVBoxLayout(margin 0；spacing 12)
│               ├── QFrame#Card 会话标题卡：CardCaption「当前会话」+ ConversationTitle +「成员」GhostButton
│               ├── QFrame#MessageCard（stretch 1，圆角 14 + 投影）
│               │   └── QQuickWidget（透明背景）→ qrc:/qml/MessageView.qml
│               └── QFrame#Card 输入卡：QLineEdit#ComposerInput + 发送 QPushButton#PrimaryButton
└── QStatusBar：连接状态 / 操作反馈（QLabel#StatusLabel，state = idle/ok/info/error）
```

### 3.3 QML 消息视图（MessageView.qml）

```
Item（透明背景，圆角与描边由外层 MessageCard 提供）
├── ListView(model = messageModel, clip = true, spacing 16)
│   ├── add 动画：新消息淡入 200ms；displaced 动画：列表重排 160ms
│   └── delegate：Item
│       ├── 对方头像：36px 圆形 + 首字母 + 按名称哈希取色
│       └── Column：名称（对方可见，含离线标记）/ 气泡 / 时间
│           └── Rectangle（圆角 16；最大文本宽 min(500, 视宽 68%)）
│               · 本人：右对齐、蓝色渐变（#3b82f6 → #2563eb）、白字
│               · 对方：左对齐、白底描边、深色字、末尾时间
├── 自定义细滚动条（不依赖 QtQuick Controls）
└── 空状态：圆形图标 +「选择左侧好友或群组 / 开始一段新的对话」
```

## 4. 响应式策略

- 所有容器使用 `QLayout`，不使用绝对坐标；控件通过 `QSizePolicy` 与 stretch 因子分配空间。
- `QSplitter` 可拖动，初始比例 280:760，左面板宽度限制在 250–380 px。
- 窗口最小尺寸 800×600，默认 1080×720；缩放时消息区与输入框自动伸缩。
- 气泡宽度按视口比例（`row.width * 0.68`，上限 500 px）计算，文本自动换行，因此在不同分辨率/缩放下不会溢出。
- QML 使用 `anchors` + `ListView.anchors.fill`，随 QQuickWidget 尺寸变化自动重排。
- 消息列表 `clip: true`，新消息到达时 `positionViewAtEnd()` 自动滚到底部。
- 应用启动时设置 `HighDpiScaleFactorRoundingPolicy::PassThrough`，125% / 150% 等非整数缩放不再取整。
- 样式表长度使用逻辑像素、字号使用 `pt`，布局不使用固定像素高度，避免高 DPI 下模糊、错位与截断。

### 4.1 视觉与交互规范

- **主题文件**：`assets/theme.qss` 打包进 `resources.qrc`，`main.cpp` 启动时通过 `QApplication::setStyleSheet` 全局加载；同时使用 `Fusion` 风格，保证 Linux/Windows 表现一致。
- **配色**：主色 `#2563eb`（悬停 `#3b82f6` / 按下 `#1d4ed8`）；页面背景 `#f2f5f9`；卡片 `#ffffff`；描边 `#e4e8ef`；正文 `#1f2430`、次级 `#5b6472`、弱化 `#98a1ad`；成功 `#16a34a`、错误 `#dc2626`。
- **层级**：AppTitle（17pt 粗）/ ConversationTitle（12pt 粗）/ 正文（10pt）/ CardCaption（8.5pt）；主要操作使用 `#PrimaryButton`（蓝色渐变），次要操作使用 `#SoftButton`，低干扰操作使用 `#GhostButton`。
- **形状与阴影**：卡片统一 14 px 圆角、控件 8–9 px 圆角；主卡片通过 `QGraphicsDropShadowEffect` 添加低透明度投影，形成层次。
- **交互反馈**：所有按钮、输入框、页签、列表项均提供 hover / pressed / focus / disabled 状态；状态栏文本通过 `state` 属性（`idle` / `ok` / `info` / `error`）切换语义色。
- **过渡动画**：切换会话时消息区 180 ms 淡入；QML 新消息 200 ms 淡入、列表重排 160 ms 平滑位移。
- **列表呈现**：好友/群组使用按名称哈希取色的首字母头像，好友头像右下角以绿色/灰色圆点表示在线状态。

## 5. 信号槽连接关系

| 发送方 | 信号 | 接收方 | 槽 / 行为 |
| --- | --- | --- | --- |
| QTcpSocket | `connected` | ChatClient | 转发为 `connected` |
| QTcpSocket | `readyRead` | ChatClient | `onReadyRead` 拆帧并分发 |
| QTcpSocket | `disconnected` | ChatClient | 转发为 `disconnected` |
| QTcpSocket | `errorOccurred` | ChatClient | `onSocketError` → `socketError(QString)` |
| ChatClient | `loginResult` | LoginDialog | 登录成功发 `loggedIn(ack, email, password)` 或显示错误 |
| ChatClient | `registerResult` | LoginDialog | 注册成功提示并回填登录页邮箱 |
| ChatClient | `addFriendResult` | MainWindow | 好友请求发送结果反馈（未找到 / 不能加自己 / 已是好友 / 已有待处理请求 / 对方已同意） |
| ChatClient | `friendRequest` | MainWindow | 收到实时好友请求（id 7 下行），弹窗选择同意/拒绝 |
| ChatClient | `friendRespResult` | MainWindow | 同意/拒绝好友请求的处理结果反馈 |
| ChatClient | `friendRemoved` | MainWindow | 被对方删除好友的通知（id 17 下行），自动刷新列表 |
| ChatClient | `delFriendResult` | MainWindow | 删除好友结果反馈，成功后自动刷新列表 |
| ChatClient | `addGroupResult` | MainWindow | 加群结果反馈，同名群返回候选列表供选择 |
| ChatClient | `loginResult` | MainWindow | 刷新列表时的静默重登 ACK |
| ChatClient | `chatMessage` | MainWindow | `appendIncoming` 按会话路由 |
| ChatClient | `disconnected` | MainWindow | 断线提示并回到登录框 |
| LoginDialog | `loggedIn` | main.cpp lambda | 隐藏登录框，`startSession` 并显示主窗口 |
| MainWindow | `loggedOut` | main.cpp lambda | 隐藏主窗口，`prepareForLogin` 并显示登录框 |
| QPushButton(发送) / QLineEdit(returnPressed) | `clicked` | MainWindow | `onSendClicked` |
| QListWidget(好友/群组) | `itemClicked` | MainWindow | 切换会话 |
| QListWidget(好友) | `customContextMenuRequested` | MainWindow | `onFriendContextMenu`：右键删除好友（陌生人/非好友无入口） |
| 菜单 QAction | `triggered` | MainWindow | `onLogout` |

## 6. 协议映射

所有请求复用 `include/public.hpp` 的 `EnMsgType` 与 `encodeMessage()`：

| 操作 | msgid | 请求字段 | 下行 |
| --- | --- | --- | --- |
| 登录 | 1 | `email, password` | 2 `LOGIN_MSG_ACK`（errno/id/name/email/friends/groups/offlinemsg；errno 3 = 账号因连续失败被临时锁定） |
| 注册 | 4 | `name, email, password` | 5 `REG_MSG_ACK`（errno/id/email） |
| 注销 | 3 | `id`（服务端以连接上下文为准） | 无 ACK（服务端不关闭连接） |
| 私聊 | 6 | `id, name, toid` 或 `toaccount, msg, time` | 6 转发给对端；失败时下行 13 `CHAT_SEND_ACK`（errno 1 目标不存在 / 2 内容为空或超长 / 3 非群成员） |
| 加好友（发请求） | 7 | `id, account`（邮箱或用户名） | 11 `ADD_FRIEND_MSG_ACK`（errno/name/email；errno 0 请求已发送 / 1 未找到 / 2 加自己 / 3 账号为空 / 4 已是好友 / 5 已有待处理请求）；请求制：对方同意后双方才成为好友 |
| 处理好友请求 | 15 | `account`（发起方邮箱）, `accept`（true 同意 / false 拒绝） | 16 `ADD_FRIEND_RESP_MSG_ACK`（errno/errmsg/agreed；errno 1 无对应待处理请求）；同意后实时通知发起方（下行复用 11 + `agreed:true`），拒绝后发起方可重新发起 |
| 删除好友 | 17 | `id, account`（邮箱或用户名） | 18 `DEL_FRIEND_MSG_ACK`（errno/name/email；errno 0 成功（双向解除）/ 1 未找到 / 2 非好友 / 3 账号为空）；被移除方在线时实时收到下行 17 通知（name/email） |
| 创建群 | 8 | `id, groupname, groupdesc` | 14 `CREATE_GROUP_MSG_ACK`（errno/groupname） |
| 加群 | 9 | `id, groupname`（可选 `candidate` 为同名群序号） | 12 `ADD_GROUP_MSG_ACK`（errno/groupname/groups） |
| 群聊 | 10 | `id, name, groupid` 或 `groupname, msg, time` | 10 转发给群成员；失败时下行 13 `CHAT_SEND_ACK` |

说明：客户端界面不展示、也不要求用户输入用户 ID 或群 ID；加好友使用邮箱或用户名，加群使用群名称，同名群由服务端返回候选列表、客户端按序号选择。`friends`、`groups`（含 `users`）、`offlinemsg`、`pendingrequests` 均为 **JSON 字符串数组**，客户端逐项 `json::parse` 后使用。

好友请求为**请求-审批制**：发起后写入 `friend_request` 表（userid+friendid 联合主键），接收方在线时实时弹窗、离线则登录时通过 `pendingrequests` 补发；同意即双向落库并删除请求，拒绝仅删除请求（可重新发起）。登录 ACK 新增 `pendingrequests` 字段（`[{userid,name,email}]`），仅存在待处理请求时携带。GUI 在好友列表右键菜单提供「删除好友」。

### 6.1 安全与账号规则

- `user` 表已增加 `email` 列（唯一索引）；邮箱仅作为登录账号，不发送验证码。
- 注册只需用户名 + 邮箱 + 密码；登录使用邮箱 + 密码；连续 5 次失败锁定 5 分钟（errno 3）。
- 密码以 **SHA-256 + 随机盐** 存储（`sha256$<salt>$<digest>`，`user.password` 为 varchar(128)）；旧明文数据在登录成功后自动升级为哈希。
- 未登录连接发送的任何业务消息（含伪造他人 id 的私聊/群聊）一律丢弃；已登录连接的发送者身份以连接上下文为准，请求体中的 id/name 会被覆盖。
- 单条消息内容上限 4096 字节，超长由服务端拒绝并返回 `CHAT_SEND_ACK`（errno 2）。

### 6.2 运行配置

- 数据库表结构见 `docs/schema.sql`，新环境执行 `mysql -uroot -p < docs/schema.sql` 初始化（含 `friend_request` 表）。
- 服务端从 `config/server.conf`（或工作目录 `server.conf`）读取 `db_host/db_port/db_user/db_password/db_name/db_pool_size/redis_host/redis_port`；配置文件缺失时服务端拒绝启动。该文件含数据库凭据，已加入 `.gitignore`，禁止入库。
- `SIGINT`/`SIGTERM` 退出时自动将在线用户重置为 offline，避免异常退出后状态残留。

## 7. 构建与运行

```bash
# 依赖（Ubuntu/Debian）
sudo apt-get install -y qt6-base-dev qt6-declarative-dev

# 构建（在项目根目录）
./autobuild.sh

# 运行
./bin/ChatServer 127.0.0.1 6000
./bin/ChatGui
```

若 Qt6 Quick 配置未被 CMake 找到，可显式指定：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr
```

## 8. 已知限制

- 登录/注册/加好友/处理好友请求/删好友/建群/加群均有结果 ACK（注销无 ACK，私聊/群聊仅失败时返回 `CHAT_SEND_ACK`）；好友请求的同意与删除操作成功后客户端会自动刷新列表。
- 刷新列表通过同一连接上顺序发送 `LOGINOUT` + `LOGIN` 实现（已实测服务端不关闭连接、且能正确返回新的好友/群/离线数据）；离线消息在登录时已事务性取走，刷新不会重复下发，刷新也不会重复弹出待处理好友请求。
- 好友在线状态只在登录/刷新时取快照，之后上下线不会实时推送。
- 聊天记录仅保存在内存中，注销或退出即丢失；会话历史按会话（好友/群）隔离。
- 若收到非好友的私聊消息，会在好友列表生成「陌生人」临时条目（仅本次会话有效）。
