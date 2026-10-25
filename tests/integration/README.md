# 集成测试

`test_chat.py` 会为每次运行创建：

- 一个独立的 MySQL 测试库（名称 `chat_it_<pid>_<随机串>`），从 `docs/schema.sql` 初始化；
- 一个独立的 Redis 实例（随机端口、不持久化）；
- 两个 ChatServer 测试节点（随机端口，共享上述测试库与 Redis）。

测试结束后会自动停止节点/Redis 并删除测试库，不会读写 `config/server.conf` 指向的正式数据库。

## 前置条件

```bash
# 构建服务端
cmake --build build -j"$(nproc)"    # 或使用你已有的构建目录

# 工具
mysql --version
redis-server --version
python3 --version
```

测试会读取 `config/server.conf` 中的 MySQL 连接信息（需要建库/删库权限），但不会使用其中的 `db_name`。
Redis 由测试自行启动，不使用正式 Redis。

## 运行

```bash
python3 tests/integration/test_chat.py -v
```

## 覆盖范围

- 表索引/外键存在性，以及删除用户时的级联清理
- 分帧、未登录请求丢弃、非法帧长度断连
- 双节点私聊、Redis 跨节点转发
- 离线消息按写入顺序补发（依赖 `offlinemessage.id`）
- 好友请求事务：同意、重复同意、删除好友并清理残留请求
- 建群事务（群 + 创建者成员）、重复加群、跨节点群聊、离线群消息
- 注销后立即登录时跨节点消息仍可达

如果缺少 `config/server.conf`、`bin/ChatServer`、`mysql` 或 `redis-server`，测试会以 skipped 结束并给出原因。
