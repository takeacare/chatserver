# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

A clustered chat server based on muduo, with MySQL persistence and Redis pub/sub for cross-node message routing. Three executables are produced:

- `bin/ChatServer` — server (`src/server/`)
- `bin/ChatClient` — terminal client (`src/client/`)
- `bin/ChatGui` — Qt 6 QWidget + QML client (`src/qtclient/`)

All clients speak the same framed JSON protocol defined in `include/public.hpp`. Comments and user-facing text are in Chinese; match that style.

## Build

```bash
./autobuild.sh                  # wipes build/*, runs cmake + make
# or incrementally:
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

If Qt6 Quick is not found: `cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr`.

Dependencies: CMake ≥ 3.16, C++17-capable compiler, muduo (`muduo_net`, `muduo_base`; this machine has static libs in `/usr/local/lib`), `libmysqlclient`, `libhiredis`, OpenSSL, Qt 6 (`qt6-base-dev`, `qt6-declarative-dev`). There is no package-manager setup committed for muduo.

Build layout notes:
- Root `CMakeLists.txt` sets global include dirs (`include`, `include/server`, its `db`/`model`/`redis` subdirs, `thirdparty`) and `EXECUTABLE_OUTPUT_PATH=bin`. Add a new include directory there if you create one.
- Server and terminal client use `aux_source_directory`, so new `.cpp` files in their source dirs are picked up automatically. The Qt client uses an explicit source list in `src/qtclient/CMakeLists.txt` — new Qt sources must be added there.
- Server links `muduo_net muduo_base mysqlclient hiredis pthread OpenSSL::Crypto`; only the Qt target is explicitly C++17.
- `bin/ChatServer`, `bin/ChatClient`, and `bin/ChatGui` are tracked in git, so rebuilding shows them as modified.

## Run

```bash
./bin/ChatServer 127.0.0.1 6000     # run from repo root
./bin/ChatClient 127.0.0.1 6000
./bin/ChatGui                        # host/port set in the login dialog
```

The server refuses to start without a config file: it probes `config/server.conf` / `server.conf` in the working directory, then `<exe>/../config/server.conf` and `<exe>/config/server.conf` (so running `./ChatServer` from `bin/` also works). The file is `key=value` (see `config/server.conf` for keys) and contains DB credentials; it is gitignored and must not be committed. `ServerConfig` exposes the values; defaults exist for ports/pool size but the password defaults to empty.

Runtime prerequisites: MySQL with a `chat` database and Redis at the configured address. If Redis is unavailable the server still runs and local delivery works, but cross-node routing does not.

`SIGINT`/`SIGTERM` run `ChatService::reset()` to set all `online` users back to `offline`.

## Wire protocol

Every TCP message is: **4-byte big-endian length L + L bytes of JSON**. Max frame is 4 MB (`MAX_MSG_LEN`); max chat text is 4096 bytes (`MAX_TEXT_LEN`). Use `encodeMessage()` for TCP sends.

`msgid` values are the sequential `EnMsgType` enum in `include/public.hpp`:

| id | message | id | message |
|---|---|---|---|
| 1 | `LOGIN_MSG` | 8 | `CREATE_GROUP_MSG` |
| 2 | `LOGIN_MSG_ACK` | 9 | `ADD_GROUP_MSG` |
| 3 | `LOGINOUT_MSG` | 10 | `GROUP_CHAT_MSG` |
| 4 | `REG_MSG` | 11 | `ADD_FRIEND_MSG_ACK` |
| 5 | `REG_MSG_ACK` | 12 | `ADD_GROUP_MSG_ACK` |
| 6 | `ONE_CHAT_MSG` | 13 | `CHAT_SEND_ACK` (failure only) |
| 7 | `ADD_FRIEND_MSG` | 14 | `CREATE_GROUP_MSG_ACK` |
| 15 | `ADD_FRIEND_RESP_MSG` | 16 | `ADD_FRIEND_RESP_MSG_ACK` |
| 17 | `DEL_FRIEND_MSG` | 18 | `DEL_FRIEND_MSG_ACK` |

Framing is a boundary concern only: messages stored in `offlinemessage` and published through Redis are **raw JSON**, and are re-framed only when written to a TCP connection.

Friend requests are approval-based: `ADD_FRIEND_MSG` only records a row in `friend_request` and notifies the recipient; the two users become friends only after the recipient sends `ADD_FRIEND_RESP_MSG` with `accept: true`. Pending requests are delivered in `LOGIN_MSG_ACK.pendingrequests` when the recipient next logs in. Some ids are reused on the downlink: id 7 pushes a new request to the recipient (fields `userid/name/email`), id 11 with `agreed: true` tells the requester the request was accepted, and id 17 tells the removed side of a `DEL_FRIEND_MSG`.

Protocol gotchas:
- Server: invalid frame length closes the connection; JSON parse failure or missing/invalid `msgid` drops that frame; an exception in a handler closes the connection. Unknown `msgid` logs and does nothing.
- After login, the user id is stored in `TcpConnection::setContext`. Handlers for authenticated operations must call `getAuthUserId()` and use that id; request-body `id`/`name` fields are untrusted and overwritten.
- Login is by email. Adding friends and terminal-client chat address users by email or username; group operations use group name (with optional `candidate` index for duplicate names).

## Server architecture

Request flow: `ChatServer::onMessage` (muduo, 4 worker threads) → deframe → `ChatService::getHandler(msgid)` → handler → model/Redis.

- `src/server/chatserver.cpp` — muduo callbacks, stream deframing, `msgid` dispatch, connection teardown.
- `src/server/chatservice.cpp` — singleton business layer: login/reg, chat, friend/group operations, login throttling, online-connection map (`_userConnMap`, guarded by `_connMutex`). New handlers are registered in the `ChatService` constructor and declared in the header.
- `src/server/model/` — one model per table, including `FriendRequestModel` for pending requests. Access MySQL through the RAII `MySQL` handle (`include/server/db/db.h`), which borrows from `ConnectionPool` and returns it on destruction. Always escape strings with `mysql.escape(...)` and free every `MYSQL_RES*`. `FriendModel::acceptRequest` (request check + both friendship rows + pending-request cleanup) and `removeFriend` are single transactions; `GroupModel::createGroupWithCreator` creates the group and creator membership atomically; `OfflineMsgModel::take` deletes only the ids it selected so concurrent inserts survive for the next login.
- `src/server/db/connectionpool.cpp` — lazy-growing pool; `acquire()` blocks when exhausted.
- `src/server/redis/redis.cpp` — one publish context plus one subscribe context owned by a dedicated reader thread; subscribe/unsubscribe commands are queued and the reader is woken via a pipe. `subscribe()` waits for Redis confirmation on login; `unsubscribe()` is fire-and-forget. Redis channel = user id.
- `src/server/main.cpp` — parses `ip port`, loads config, initializes pool, installs signal handlers.

Message routing:
- Direct chat: if the recipient is in the local `_userConnMap`, send directly; else if their DB state is `online` (they are on another node), publish to their Redis channel; else insert into `offlinemessage`.
- Group chat: same local/Redis/offline decision per member; only group members may send.
- On login, offline messages are taken transactionally and returned in `LOGIN_MSG_ACK.offlinemsg`; `friends`, `groups`, and `offlinemsg` entries are **JSON strings** that clients parse individually.

Security invariants to preserve: passwords are stored as `sha256$<salt>$<digest>` (`include/server/password.hpp`) with legacy plaintext upgraded on successful login; five consecutive failed logins lock the account for 5 minutes (errno 3); unauthenticated connections are rejected for every business operation; login/register errors avoid leaking account existence.

## Database schema

`docs/schema.sql` contains the full DDL; provision with `mysql -uroot -p < docs/schema.sql`. The models assume:

- `user(id, name UNIQUE, email UNIQUE, password, state)` — `UserModel` uses `select *` and interprets columns in exactly that order.
- `friend(userid, friendid)` — both directions are stored; primary key `(userid, friendid)`, reverse index `(friendid, userid)`, both columns FK to `user.id` with `ON DELETE CASCADE`.
- `friend_request(userid, friendid)` — pending requests only (userid = requester, friendid = recipient); PK `(userid, friendid)`, target index `(friendid, userid)`, FKs with cascade; deleted on accept or decline.
- `allgroup(id, groupname, groupdesc)` — index `(groupname, id)` for name lookup.
- `groupuser(groupid, userid, grouprole)` — PK `(groupid, userid)`, user index `(userid, groupid)`, FKs to group/user with cascade.
- `offlinemessage(id, userid, message, created_at)` — raw JSON; ordered by auto-increment `id`, index `(userid, id)`, FK to user with cascade. Inserts must name `(userid, message)` explicitly.

`docs/schema.sql` is the source of truth for new environments. Existing databases need the one-time migrations:
1. `docs/migrate_repair_one_way_friends.sql` — backfill legacy one-way friend rows.
2. `docs/migrate_indexes_constraints.sql` — add offline `id/created_at`, indexes and foreign keys. It changes the `offlinemessage` column layout, so apply it while the server is stopped and start the new server afterwards (the old binary's position-based insert will not fit the new table).

## Terminal client

`src/client/main.cpp` is a two-thread cin/cout client: main thread sends, detached thread deframes and dispatches. After login it supports:

```
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

`#index` is 1-based on the command line (`groupname#1` → candidate 0).

## Qt GUI client

- `chatclient.h/.cpp` — `QTcpSocket` wrapper; all async. It reuses `encodeMessage`, deframes into `QByteArray`, and emits typed signals (`loginResult`, `registerResult`, `addFriendResult`, `friendRequest`, `friendRespResult`, `friendRemoved`, `delFriendResult`, `addGroupResult`, `createGroupResult`, `chatSendResult`, `chatMessage`, ...). Add a signal here and a `msgid` case in `processPayload` for new server responses.
- `logindialog.*` — email/password login, registration, and server host/port.
- `mainwindow.*` — owns friend/group lists and conversation routing. Conversations are keyed `u:<id>` / `g:<id>` (`conversation.h`); incoming `ONE_CHAT_MSG`/`GROUP_CHAT_MSG` are routed from `id`/`groupid`, with stranger peer entries created on demand. Pending friend requests from the login ACK pop up sequentially; live requests arrive as `ADD_FRIEND_MSG`. Right-clicking a friend opens a delete-friend menu (`DEL_FRIEND_MSG`).
- `messagemodel.*` — one `QAbstractListModel` per conversation, injected into `MessageView.qml` through a `QQuickWidget` context property. QML is display-only.
- `assets/theme.qss` and the QML are bundled by `resources.qrc`; `main.cpp` loads `:/assets/theme.qss` and uses the Fusion style.
- The message view is a `QQuickWidget`. Never set a `QGraphicsEffect` (shadow) on it or any of its ancestors (unsupported by Qt). Also do not use opacity animations in the ListView `add` transition: the animation can stall while the QQuickWidget window is not rendering, leaving new message delegates at `opacity=0` (messages look missing). New messages render instantly; only the `displaced` animation remains.
- "Refresh lists" is implemented by sending `LOGINOUT` then `LOGIN` on the same connection and rebuilding from the new ACK (the server does not close the connection on logout). Keep this in mind when changing logout semantics.

`docs/qtgui-layout.md` documents the full layout, signal table, and protocol mapping. Its "known limitations" section is stale regarding ACKs (friend/group results now have ACKs), so verify against code. `.trae/documents/修复聊天服务器现存缺陷.md` records the rationale for the framing, SQL-safety, pooling, and Redis threading design.

## Tests

`tests/integration/test_chat.py` is an isolated Python `unittest` suite. Each run creates a temporary MySQL database from `docs/schema.sql`, starts a private `redis-server` on a random port, launches two `ChatServer` nodes on random ports, and covers schema constraints/cascades, framing and auth, cross-node routing, offline message ordering, transactional friend/group operations, and immediate-relogin subscriptions. It never uses the `db_name`/Redis from `config/server.conf`, and drops the test database at the end.

Prerequisites: `config/server.conf` with MySQL credentials that can create/drop databases, `mysql` and `redis-server` on `PATH`, and a built `bin/ChatServer`. Run:

```bash
python3 tests/integration/test_chat.py -v
# or through CTest after building
ctest --test-dir build -R integration_chat --output-on-failure
```

The suite skips with a reason when prerequisites are missing. `test/testjson/` and `test/testmuduo/` remain standalone experiments. The GUI can be smoke-started headlessly with:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./bin/ChatGui
```
