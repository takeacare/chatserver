# chatserver
可以工作在nginx tcp负载均衡环境中的集群聊天服务器和客户端源码  基于muduo实现

## 测试

```bash
# 需先构建 bin/ChatServer，并保证 config/server.conf 的 MySQL 账号可建库
python3 tests/integration/test_chat.py -v
# 或
ctest --test-dir build -R integration_chat --output-on-failure
```

集成测试会使用独立的 MySQL 测试库和独立 Redis，不会影响 `config/server.conf` 指向的正式库。
