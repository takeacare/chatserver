# chatserver

基于 [muduo](https://github.com/chenshuo/muduo) 的集群聊天服务器：MySQL 持久化，Redis Pub/Sub 负责跨节点消息路由。提供终端客户端和 Qt 6 图形客户端，可部署在 nginx TCP 负载均衡之后。

## 功能

- 邮箱注册/登录，密码以 `sha256$<salt>$<digest>` 存储，连续 5 次失败锁定 5 分钟
- 好友请求-审批制、双向好友关系、删除好友实时通知
- 创建群组、按名称加入群组、群成员权限校验、群聊
- 私聊/群聊：本机直投 → Redis 跨节点转发 → 离线消息按顺序补发
- 客户端身份以连接上下文为准，伪造 `id`/`name` 会被服务端覆盖
- 终端客户端 `ChatClient` 与 Qt 6 QWidget + QML 客户端 `ChatGui`
- 统一协议：4 字节大端长度 + JSON，单帧上限 4 MB，单条聊天内容上限 4096 字节

## 架构

```text
ChatClient / ChatGui
        │  TCP: 4 字节长度 + JSON
        ▼
ChatServer（muduo，多 IO 线程）
        ├── ChatService：登录、好友、群组、聊天业务
        ├── Model 层 ── MySQL（连接池）
        └── Redis Pub/Sub（channel = 用户 id）
```

- 多节点共享同一套 MySQL 与 Redis；用户登录时服务端订阅自己的 id 通道。
- 私聊优先查本机在线连接；目标在其它节点时通过 Redis 发布；离线则写入离线表。
- 群聊对每个群成员执行同样的「本机 / Redis / 离线」路由。
- `SIGINT`/`SIGTERM` 退出时把在线用户状态重置为 offline。

## 目录结构

```text
include/public.hpp          协议、消息号、分帧/校验工具
include/server/             服务端头文件（业务、模型、连接池、Redis、配置）
src/server/                 服务端实现
src/client/                 终端客户端
src/qtclient/               Qt 6 GUI（QWidget + QML）
docs/schema.sql             数据库 DDL
docs/migrate_*.sql          既有数据库迁移脚本
tests/integration/          独立 MySQL/Redis/双节点集成测试
config/server.conf          运行配置（含凭据，已 gitignore）
bin/                        构建产物输出目录
```

## 依赖

- CMake ≥ 3.16，支持 C++17 的编译器
- muduo：`muduo_net`、`muduo_base`（需自行编译安装，项目不提供包管理配置）
- MySQL 8 与 `libmysqlclient`
- `hiredis`、OpenSSL（密码哈希）
- Qt 6：`qt6-base-dev`、`qt6-declarative-dev`（仅 GUI）
- 集成测试：Python 3、`mysql` 客户端、`redis-server`

Ubuntu 依赖示例：

```bash
sudo apt-get install -y build-essential cmake default-libmysqlclient-dev \
    libhiredis-dev libssl-dev qt6-base-dev qt6-declarative-dev
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

产物：

- `bin/ChatServer`
- `bin/ChatClient`
- `bin/ChatGui`

如果 CMake 找不到 Qt 6，可指定前缀：`cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr`。
也可用 `bash autobuild.sh`（会先清空 `build/`）。

## 数据库

新环境执行：

```bash
mysql -uroot -p < docs/schema.sql
```

默认库名为 `chat`；如果修改库名，请同时修改 `docs/schema.sql`、迁移脚本和 `config/server.conf` 中的 `db_name`。

已有旧库升级（按顺序执行，先备份）：

```bash
# 1. 修复旧版单向好友数据（可重复执行）
mysql -uroot -p < docs/migrate_repair_one_way_friends.sql

# 2. 停掉 ChatServer 后执行索引/外键/离线消息 id 迁移（一次性）
mysql -uroot -p < docs/migrate_indexes_constraints.sql

# 3. 启动新 ChatServer（新二进制要求第 2 步的新表结构）
```

`docs/schema.sql` 包含外键级联、好友/请求反向索引、群名索引，以及离线消息的 `id`/`created_at`。离线消息按 `id` 顺序补发，服务端只删除本次取出的行。

## 配置

服务端读取 `config/server.conf`（`key=value`，`#`/`;` 开头为注释）。查找顺序：

1. 当前工作目录 `config/server.conf`
2. 当前工作目录 `server.conf`
3. 可执行文件目录的 `../config/server.conf`
4. 可执行文件目录的 `config/server.conf`

因此从仓库根目录或 `bin/` 启动都可以。示例：

```ini
db_host=127.0.0.1
db_port=3306
db_user=root
db_password=your_password
db_name=chat
db_pool_size=8
redis_host=127.0.0.1
redis_port=6379
```

该文件包含数据库凭据，已加入 `.gitignore`，不要提交。

## 运行

```bash
# 仓库根目录
./bin/ChatServer 127.0.0.1 6000
./bin/ChatClient 127.0.0.1 6000
./bin/ChatGui

# 或从 bin/ 目录启动（配置查找支持可执行文件相对路径）
cd bin
./ChatServer 127.0.0.1 6000
```

Qt 客户端在登录对话框填写服务地址和端口。Redis 不可用时服务端仍可启动，单节点本地投递正常，跨节点路由不可用。

## 协议摘要

所有 TCP 消息：**4 字节大端长度 L + L 字节 JSON**。离线消息和 Redis 中存储/传输的是裸 JSON，仅写入连接时重新加帧。

| id | 消息 | id | 消息 |
|---|---|---|---|
| 1 | `LOGIN_MSG` | 10 | `GROUP_CHAT_MSG` |
| 2 | `LOGIN_MSG_ACK` | 11 | `ADD_FRIEND_MSG_ACK` |
| 3 | `LOGINOUT_MSG` | 12 | `ADD_GROUP_MSG_ACK` |
| 4 | `REG_MSG` | 13 | `CHAT_SEND_ACK`（仅失败时） |
| 5 | `REG_MSG_ACK` | 14 | `CREATE_GROUP_MSG_ACK` |
| 6 | `ONE_CHAT_MSG` | 15 | `ADD_FRIEND_RESP_MSG` |
| 7 | `ADD_FRIEND_MSG` | 16 | `ADD_FRIEND_RESP_MSG_ACK` |
| 8 | `CREATE_GROUP_MSG` | 17 | `DEL_FRIEND_MSG` |
| 9 | `ADD_GROUP_MSG` | 18 | `DEL_FRIEND_MSG_ACK` |

登录成功后的身份保存在连接上下文；好友/聊天/群组操作必须已登录。好友请求待处理列表随登录 ACK 的 `pendingrequests` 下发；id 7/11/17 在下行方向也会复用作实时通知。完整字段与错误码见 `docs/qtgui-layout.md`。

## 终端客户端命令

```text
help
chat:<email_or_username>:<message>
addfriend:<email_or_username>
resp:<requester_email>:y|n
delfriend:<email_or_username>
creategroup:<groupname>:<groupdesc>
addgroup:<groupname>[#index]
groupchat:<groupname>[#index]:<message>
loginout
```

`#index` 从 1 开始，用于同名群候选；群组操作也可以直接使用群名称。

## 测试

集成测试会为每次运行创建独立的 MySQL 测试库、独立 Redis 和两个测试服务节点，不会读写正式库或正式 Redis：

```bash
# 需先构建 bin/ChatServer，并保证 config/server.conf 的 MySQL 账号可建库/删库
python3 tests/integration/test_chat.py -v

# 或通过 CTest
ctest --test-dir build -R integration_chat --output-on-failure
```

覆盖范围：表索引/外键与级联删除、分帧/鉴权/非法请求、双节点私聊、离线消息顺序、好友请求事务、建群/加群、跨节点群聊、注销后立即重登的订阅可用性。缺少 `config/server.conf`、`mysql`、`redis-server` 或 `bin/ChatServer` 时测试会 skip 并说明原因。

GUI 无头冒烟（持续运行到超时即表示启动正常）：

```bash
timeout 3 env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./bin/ChatGui
```

## 已知限制

- 聊天记录只保存在客户端内存，重启/注销后丢失；服务端只持久化离线消息。
- 好友在线状态是登录/刷新时的快照，不实时推送上下线。
- 登录失败锁定在单节点内存中，多节点部署不共享。
- 未启用 TLS；不要在不可信网络上直接暴露服务。
- Redis 负责跨节点路由，Redis 不可用时多节点消息不会互通。
- `config/server.conf` 明文保存数据库凭据，务必限制文件权限。

## 文档

- `CLAUDE.md`：开发约定、架构细节、协议与踩坑记录
- `docs/qtgui-layout.md`：Qt GUI 布局、信号槽与协议映射
- `docs/schema.sql` 与 `docs/migrate_*.sql`：数据库结构与升级
- `tests/integration/README.md`：集成测试说明
