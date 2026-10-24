# Qt 图形化聊天客户端（ChatGui）Spec

## Why
当前客户端（`src/client/main.cpp`）是纯终端 cin/cout 交互，可用性差。需要一个遵循 Qt 官方规范、可直接融入现有构建体系的图形客户端，完整复用现有服务端与「4 字节网络序长度前缀 + JSON」分帧协议，**服务端零改动**。

## What Changes
- 新增 `src/qtclient/` 子项目：**QWidget + QML 混合**（Qt 6.4 / C++17），可执行文件 `ChatGui` 输出到 `bin/`
  - QWidget 侧：登录/注册对话框、主窗口框架、好友/群组列表、输入区、各操作对话框
  - QML 侧：聊天消息气泡视图（`qml/MessageView.qml`），经 `QQuickWidget` 嵌入主窗口，数据由 C++ `MessageModel`（QAbstractListModel）以 context property 注入
- 新增文件清单：
  ```
  src/qtclient/CMakeLists.txt
  src/qtclient/main.cpp                 # QApplication 入口，先弹登录对话框
  src/qtclient/chatclient.h/.cpp        # QTcpSocket 通信类：分帧收发 + JSON + Qt 信号
  src/qtclient/logindialog.h/.cpp       # 登录/注册对话框（含服务器地址端口配置）
  src/qtclient/mainwindow.h/.cpp        # 主窗口：QSplitter + 列表 + 会话区 + 输入区
  src/qtclient/messagemodel.h/.cpp      # 每个会话一个 QAbstractListModel
  src/qtclient/conversation.h           # 会话结构（peerId/isGroup/title/model）
  src/qtclient/qml/MessageView.qml      # 气泡列表视图（仅展示，无业务逻辑）
  src/qtclient/resources.qrc            # QML 与占位图标资源
  docs/qtgui-layout.md                  # 布局配置说明（用户明确要求输出）
  ```
- 修改现有构建（仅两处，各一行级改动）：
  - 根 `CMakeLists.txt`：`cmake_minimum_required(VERSION 3.0)` → `3.16`（Qt6 硬性最低要求）
  - `src/CMakeLists.txt`：追加 `add_subdirectory(qtclient)`
- 依赖：需安装 `qt6-declarative-dev`（当前机器仅有 QML 运行时模块 `qml6-module-qtquick*`，缺 `Qt6Qml/Qt6Quick/Qt6QuickWidgets` 的 CMake 配置）；`qt6-base-dev` 已装

## Impact
- 受影响代码：根 `CMakeLists.txt`、`src/CMakeLists.txt`、新增 `src/qtclient/**`、新增 `docs/qtgui-layout.md`
- 明确不受影响：`src/server/**`、`src/client/**`（终端客户端保留）、`include/**`、`thirdparty/**`
- 协议兼容：完全复用 `include/public.hpp` 的 `EnMsgType` 与 `encodeMessage()`；GUI 只使用服务端已有的下行消息（LOGIN_MSG_ACK / REG_MSG_ACK / ONE_CHAT_MSG / GROUP_CHAT_MSG，均经确认无其它 ACK 类型）

## ADDED Requirements

### Requirement: 网络通信层（ChatClient 类）
系统 SHALL 提供以 `QObject` 封装的 `QTcpSocket` 通信类：
- `connectToServer(host, port)`，默认 `127.0.0.1:6000`
- 发送侧：构造 JSON → 复用 `encodeMessage()` 加帧 → `write`
- 接收侧：`QByteArray` 缓冲累积，按 4 字节网络序长度前缀循环拆帧，正确处理半包/粘包；帧长超过 `MAX_MSG_LEN`（4MB）时主动断开
- 按 `msgid` 解析后以 Qt 信号发射：`loginResult(json)`、`regResult(json)`、`chatMessage(json)`（ONE_CHAT_MSG 与 GROUP_CHAT_MSG 统一入口，由上层按字段区分）、`connected/disconnected/socketError`
- 全异步（Qt 事件循环驱动），无阻塞调用、无额外线程

#### Scenario: 粘包
- WHEN 两个完整帧在一次 TCP 到达
- THEN 两帧各自解析并触发对应信号
#### Scenario: 半包
- WHEN 一帧分多次到达
- THEN 累积完整后才解析一次

### Requirement: 登录/注册对话框（LoginDialog）
- 顶部"服务器"设置组：地址 QLineEdit（默认 127.0.0.1）、端口 QSpinBox（默认 6000，范围 1–65535）
- QTabWidget 两个页：「登录」（ID + 密码，ID 用 QIntValidator）/「注册」（用户名 + 密码，密码 EchoMode=Password）
- 登录成功（errno=0）→ 关闭对话框进入主窗口；errno=1 显示"账号或密码错误"、errno=2 显示"该账号已在线"
- 注册成功 → 提示分配的 userID（"请牢记"）；重名失败 → 提示用户名已存在
- 连接失败/断开 → 对话框内提示并恢复按钮可用；等待响应期间禁用提交按钮防重复提交

### Requirement: 主窗口布局（响应式）
- `QMainWindow`，中央 `QSplitter` 水平分割：左侧功能面板（最小宽约 220px），右侧会话区
- 左面板：`QTabWidget`（好友 / 群组 两页，各一个 QListWidget）；好友项显示 `name` + 在线状态（● 在线 / ○ 离线）；顶部按钮组：添加好友、创建群、加入群、刷新列表
- 右侧自上而下：会话标题 QLabel、`QQuickWidget`（消息视图，占据主空间）、输入行（QLineEdit + 发送 QPushButton）
- 状态栏：连接状态 + 当前用户（id/name）
- 全部使用 QLayout + SizePolicy；窗口最小 800×600、默认 1000×700；Splitter 可拖动

#### Scenario: 缩放适配
- WHEN 拖拽窗口边缘或 Splitter 分隔条改变尺寸
- THEN 布局随之伸缩，无控件重叠、无错乱

### Requirement: QML 消息视图
- `MessageView.qml`：ListView，`model` 为注入的 `MessageModel`；delegate 气泡：本人消息靠右（主题色）、对方靠左（灰色），显示发送者名称与时间；模型数据变化时视图自动滚动到底部
- 消息数据角色：senderId、senderName、text、time、mine（是否本人）、offline（是否离线消息，显示 `[离线]` 前缀）
- QML 仅做展示，不含任何业务逻辑

### Requirement: 私聊
- 单击好友项 → 切换到与该好友的会话（无记录则新建空会话）；会话标题「与 name (ID x) 聊天」
- 输入框回车或点击发送 → `ONE_CHAT_MSG {msgid, id, name, toid, msg, time}`
- 收到 `ONE_CHAT_MSG` → 按发送者 id 路由到对应会话；若发送者不在好友列表，创建「陌生人」临时会话条目（仅本次登录有效，列表顶部标识）

### Requirement: 群聊
- 群组页单击群 → 切换群会话，标题「群：groupname (ID x)」
- 发送 → `GROUP_CHAT_MSG {msgid, id, name, groupid, msg, time}`
- 收到 `GROUP_CHAT_MSG` → 按 groupid 路由
- 会话标题旁「成员」按钮弹出对话框，QTableWidget 列出成员（ID/名称/角色/状态），数据来自登录响应 groups 中的 users

### Requirement: 离线消息
- 登录响应 `offlinemsg` 数组：逐条 `json::parse`（已确认服务端仅存入完整聊天 JSON），按条目内部 msgid（ONE_CHAT_MSG / GROUP_CHAT_MSG）路由到对应会话，气泡标记 `[离线]`

### Requirement: 好友/群操作与列表刷新
- 添加好友：输入 friendid 对话框（QIntValidator）→ `ADD_FRIEND_MSG`；因服务端无 ACK，发送后提示「请求已提交，刷新列表后生效」
- 创建群：群名 + 群描述输入对话框 → `CREATE_GROUP_MSG`，同样提示刷新生效
- 加入群：输入 groupid → `ADD_GROUP_MSG`
- 刷新列表：在同一连接上依次发送 `LOGINOUT_MSG` + `LOGIN_MSG` 静默重登（实施时先核实服务端 loginout 不关闭连接——从现有代码看仅移除映射/退订/置离线；若实际会关连接则改为断开后重连再登录），以最新 LOGIN_ACK 重建好友/群列表与离线消息
- 注销（菜单项）：发送 `LOGINOUT_MSG` → 返回登录对话框（保留服务器地址配置）

### Requirement: 状态反馈与防误操作
- 非法输入（空密码、非数字 ID/群号）即时提示，不发起网络请求
- 登录/注册/发送等待期间禁用对应按钮，防止重复提交
- 连接断开（服务端关闭或网络异常）→ 状态栏红色提示 + 非模态消息框，自动返回登录对话框

## MODIFIED Requirements

### Requirement: 项目构建（根 CMakeLists.txt）
原：`cmake_minimum_required(VERSION 3.0)`，仅构建 ChatServer/ChatClient。
现：`cmake_minimum_required(VERSION 3.16)`（Qt6 最低要求）；`EXECUTABLE_OUTPUT_PATH`、include 路径等其余配置不变；`src/CMakeLists.txt` 追加 `add_subdirectory(qtclient)`，一次 `./autobuild.sh` 同时产出 ChatServer / ChatClient / ChatGui 三个可执行文件。需回归验证 server/client 两目标编译行为不变。

## REMOVED Requirements
无。终端客户端 `ChatClient` 完整保留，不做任何改动。

## 非功能要求
- 流畅性：无阻塞系统调用，消息路由与模型更新均在 UI 线程内轻量完成（单帧 ≤4MB 上限，实际聊天消息远小于此）
- 中文显示：UTF-8 全链路（JSON ↔ QString::fromStdString）
- 环境：本机已有 Qt 6.4.2（qt6-base-dev）、Qt Creator 13.0.0、qml6-module-qtquick*；仅缺 qt6-declarative-dev
