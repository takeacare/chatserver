# Checklist

- [x] qt6-declarative-dev 已安装，`find_package(Qt6 COMPONENTS Qml Quick QuickWidgets)` 可用
- [x] 根 CMakeLists 最低版本升到 3.16，`src/CMakeLists.txt` 含 qtclient 子目录，`bash autobuild.sh` 全量编译通过并产出 `bin/ChatGui`
- [x] ChatClient 类复用 `public.hpp` 的 `encodeMessage()` 分帧，接收侧支持半包/粘包，帧超 4MB 断开
- [x] 网络层全异步（QTcpSocket 信号驱动），无阻塞调用、无额外线程
- [x] 登录/注册对话框：地址端口配置、输入校验、errno=1/2 区分提示、注册成功显示 userID、等待期禁用按钮
- [x] 主窗口为 QSplitter + QLayout 响应式布局，最小 800×600，缩放与拖动分隔条无控件错乱（800×600、1400×900 截图验证）
- [x] 好友/群组列表正确解析 LOGIN_ACK 嵌套 JSON 字符串（friends/groups），好友显示在线状态
- [x] QML 气泡视图：本人靠右、对方靠左、显示名称与时间、新消息自动滚动，模型经 context property 注入
- [x] 私聊/群聊发送与接收路由正确（含 time 字段、输入非空校验、防重复提交）
- [x] 陌生人私聊生成「陌生人」临时会话条目，且刷新列表后仍保留
- [x] 离线消息逐条解析路由并带 [离线] 标记
- [x] 添加好友/创建群/加入群均有输入对话框与结果反馈（服务端无 ACK，提示刷新生效）
- [x] 刷新列表通过静默重登（同连接 LOGINOUT+LOGIN）生效，已用协议脚本与 GUI 集成测试实测服务端不关闭连接
- [x] 注销返回登录对话框；连接断开有状态栏与消息框反馈
- [x] resources.qrc 正确打包 QML 与占位图标；`docs/qtgui-layout.md` 布局配置说明已输出
- [x] 本次改动范围确认：仅根/`src` 的 CMakeLists、新增 `src/qtclient/**` 与 `docs/qtgui-layout.md`，`src/server/**`、`src/client/**`、`include/**`、`thirdparty/**` 零改动
- [x] ChatGui offscreen 冒烟启动不崩溃；服务端协议回归（python 脚本）全部通过
