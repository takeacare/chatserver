#!/usr/bin/env python3
"""ChatServer 集成测试。

测试使用独立的 MySQL 测试库、独立 Redis 实例和两个测试服务节点，
不会读写 config/server.conf 指向的正式库，也不会影响正在运行的服务。

运行：python3 tests/integration/test_chat.py -v
"""

import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import uuid
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CONFIG_PATH = REPO_ROOT / "config" / "server.conf"
SCHEMA_PATH = REPO_ROOT / "docs" / "schema.sql"
SERVER_BIN = REPO_ROOT / "bin" / "ChatServer"

LOGIN_MSG, LOGIN_ACK, LOGINOUT = 1, 2, 3
REG_MSG, REG_ACK = 4, 5
ONE_CHAT_MSG = 6
ADD_FRIEND_MSG, ADD_FRIEND_ACK = 7, 11
CREATE_GROUP_MSG, CREATE_GROUP_ACK = 8, 14
ADD_GROUP_MSG, ADD_GROUP_ACK = 9, 12
GROUP_CHAT_MSG = 10
CHAT_SEND_ACK = 13
ADD_FRIEND_RESP_MSG, ADD_FRIEND_RESP_ACK = 15, 16
DEL_FRIEND_MSG, DEL_FRIEND_ACK = 17, 18

PASSWORD = "pw123456"


class Client:
    def __init__(self, address, timeout=5.0):
        self.sock = socket.create_connection(address, timeout=timeout)
        self.buf = b""
        self.skipped = []

    def send(self, obj):
        payload = json.dumps(obj).encode()
        self.sock.sendall(struct.pack("!I", len(payload)) + payload)

    def send_split(self, obj):
        payload = json.dumps(obj).encode()
        frame = struct.pack("!I", len(payload)) + payload
        cut = max(5, len(frame) // 2)
        self.sock.sendall(frame[:cut])
        time.sleep(0.05)
        self.sock.sendall(frame[cut:])

    def recv(self, timeout=5.0):
        deadline = time.time() + timeout
        while True:
            if len(self.buf) >= 4:
                size = struct.unpack("!I", self.buf[:4])[0]
                if len(self.buf) >= 4 + size:
                    payload = self.buf[4:4 + size]
                    self.buf = self.buf[4 + size:]
                    return json.loads(payload)
            remain = deadline - time.time()
            if remain <= 0:
                raise TimeoutError("recv timeout")
            self.sock.settimeout(remain)
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed connection")
            self.buf += chunk

    def expect(self, predicate, desc, timeout=5.0):
        deadline = time.time() + timeout
        skipped = []
        while time.time() < deadline:
            try:
                msg = self.recv(max(0.1, deadline - time.time()))
            except TimeoutError:
                break
            if predicate(msg):
                return msg
            skipped.append(msg)
        raise AssertionError(f"timeout waiting for {desc}; skipped={skipped[-5:]}")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def by_msgid(msgid):
    return lambda msg: msg.get("msgid") == msgid


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_port(host, port, proc=None, timeout=8.0, log_path=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            detail = ""
            if log_path is not None and Path(log_path).exists():
                detail = Path(log_path).read_text(errors="replace")[-2000:]
            raise AssertionError(f"process exited before listening on {port}: {detail}")
        try:
            with socket.create_connection((host, port), timeout=0.3):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError(f"timeout waiting for {host}:{port}")


def wait_redis(host, port, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.3) as sock:
                sock.sendall(b"PING\r\n")
                if b"+PONG" in sock.recv(64):
                    return
        except OSError:
            pass
        time.sleep(0.05)
    raise AssertionError("redis did not become ready")


def load_config(path):
    cfg = {}
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].split(";", 1)[0].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        cfg[key.strip()] = value.strip()
    return cfg


class User:
    def __init__(self, name, email, userid):
        self.name = name
        self.email = email
        self.id = userid


@unittest.skipUnless(CONFIG_PATH.exists(), "config/server.conf is required")
@unittest.skipUnless(SERVER_BIN.exists(), "build bin/ChatServer before running integration tests")
class ChatIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host = "127.0.0.1"
        cls.tmp = None
        cls.servers = []
        cls.server_logs = []
        cls.redis = None
        cls.db_name = None
        cls.db_created = False
        cls.suffix = uuid.uuid4().hex[:8]

        for tool in ("mysql", "redis-server"):
            if shutil.which(tool) is None:
                raise unittest.SkipTest(f"{tool} is required")

        cfg = load_config(CONFIG_PATH)
        cls.db_host = cfg.get("db_host", "127.0.0.1")
        cls.db_port = cfg.get("db_port", "3306")
        cls.db_user = cfg.get("db_user", "root")
        cls.db_password = cfg.get("db_password", "")
        cls.mysql = shutil.which("mysql")
        cls.mysql_env = dict(os.environ)
        cls.mysql_env["MYSQL_PWD"] = cls.db_password

        try:
            cls._check_mysql()
            cls.tmp = Path(tempfile.mkdtemp(prefix="chatserver-it-"))
            cls.db_name = f"chat_it_{os.getpid()}_{cls.suffix}"
            cls.redis_port = free_port()
            cls.server_ports = [free_port(), free_port()]
            cls._create_database()
            cls._start_redis()
            cls._write_server_config()
            cls._start_servers()
        except unittest.SkipTest:
            cls._shutdown_infra()
            raise
        except Exception:
            cls._shutdown_infra()
            raise

    @classmethod
    def tearDownClass(cls):
        cls._shutdown_infra()

    # ---------- infrastructure ----------

    @classmethod
    def _mysql_args(cls, with_db=True):
        args = [
            cls.mysql,
            "-h", cls.db_host,
            "-P", cls.db_port,
            "-u", cls.db_user,
        ]
        if with_db:
            args.append(cls.db_name)
        return args

    @classmethod
    def _check_mysql(cls):
        result = subprocess.run(
            cls._mysql_args(False) + ["-N", "-B", "-e", "SELECT 1"],
            env=cls.mysql_env, text=True, capture_output=True,
        )
        if result.returncode != 0:
            raise unittest.SkipTest(f"cannot connect to MySQL: {result.stderr.strip()}")

    @classmethod
    def _create_database(cls):
        cls._mysql_run(
            f"CREATE DATABASE `{cls.db_name}` DEFAULT CHARACTER SET utf8mb4;",
            with_db=False,
        )
        cls.db_created = True
        schema = SCHEMA_PATH.read_text().replace("`chat`", f"`{cls.db_name}`")
        cls._mysql_run(schema, with_db=False)

    @classmethod
    def _mysql_run(cls, sql, with_db=True):
        result = subprocess.run(
            cls._mysql_args(with_db) + ["-N", "-B"],
            input=sql, env=cls.mysql_env, text=True, capture_output=True,
        )
        if result.returncode != 0:
            raise AssertionError(f"mysql failed: {result.stderr.strip()}\nSQL: {sql[:500]}")

    @classmethod
    def _sql(cls, query):
        result = subprocess.run(
            cls._mysql_args(True) + ["-N", "-B"],
            input=query, env=cls.mysql_env, text=True, capture_output=True,
        )
        if result.returncode != 0:
            raise AssertionError(f"mysql query failed: {result.stderr.strip()}\nSQL: {query}")
        return [line.split("\t") for line in result.stdout.splitlines() if line != ""]

    @classmethod
    def _sql_value(cls, query):
        rows = cls._sql(query)
        return rows[0][0] if rows else None

    @classmethod
    def _start_redis(cls):
        redis_dir = cls.tmp / "redis"
        redis_dir.mkdir()
        cls.redis_log = cls.tmp / "redis.log"
        log = open(cls.redis_log, "wb")
        cls.redis = subprocess.Popen(
            [
                "redis-server",
                "--bind", "127.0.0.1",
                "--port", str(cls.redis_port),
                "--save", "",
                "--appendonly", "no",
                "--dir", str(redis_dir),
            ],
            stdout=log, stderr=subprocess.STDOUT,
        )
        cls.redis._log_file = log
        try:
            wait_redis("127.0.0.1", cls.redis_port)
        except Exception:
            if cls.redis.poll() is not None:
                raise AssertionError(
                    f"redis exited: {cls.redis_log.read_text(errors='replace')[-1000:]}")
            raise

    @classmethod
    def _write_server_config(cls):
        config_dir = cls.tmp / "config"
        config_dir.mkdir()
        (config_dir / "server.conf").write_text(
            "\n".join([
                f"db_host={cls.db_host}",
                f"db_port={cls.db_port}",
                f"db_user={cls.db_user}",
                f"db_password={cls.db_password}",
                f"db_name={cls.db_name}",
                "db_pool_size=4",
                "redis_host=127.0.0.1",
                f"redis_port={cls.redis_port}",
                "",
            ])
        )

    @classmethod
    def _start_servers(cls):
        for index, port in enumerate(cls.server_ports, start=1):
            log_path = cls.tmp / f"server{index}.log"
            log = open(log_path, "wb")
            proc = subprocess.Popen(
                [str(SERVER_BIN), "127.0.0.1", str(port)],
                cwd=cls.tmp, stdout=log, stderr=subprocess.STDOUT,
            )
            proc._log_file = log
            cls.servers.append(proc)
            cls.server_logs.append(log_path)
            try:
                wait_port("127.0.0.1", port, proc, log_path=log_path)
            except Exception:
                raise AssertionError(
                    f"server {index} failed to start: "
                    f"{log_path.read_text(errors='replace')[-2000:] if log_path.exists() else ''}")

    @classmethod
    def _shutdown_infra(cls):
        for proc in getattr(cls, "servers", []):
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=2)
            log_file = getattr(proc, "_log_file", None)
            if log_file is not None:
                log_file.close()
        cls.servers = []

        redis = getattr(cls, "redis", None)
        if redis is not None and redis.poll() is None:
            redis.terminate()
            try:
                redis.wait(timeout=5)
            except subprocess.TimeoutExpired:
                redis.kill()
                redis.wait(timeout=2)
        redis_log = getattr(redis, "_log_file", None) if redis is not None else None
        if redis_log is not None:
            redis_log.close()
        cls.redis = None

        if getattr(cls, "db_created", False) and getattr(cls, "db_name", None):
            try:
                subprocess.run(
                    cls._mysql_args(False) + ["-e", f"DROP DATABASE IF EXISTS `{cls.db_name}`"],
                    env=cls.mysql_env, text=True, capture_output=True,
                )
            except Exception:
                pass
        cls.db_created = False

        if getattr(cls, "tmp", None) is not None:
            shutil.rmtree(cls.tmp, ignore_errors=True)
            cls.tmp = None

    def new_client(self, node=0):
        client = Client((self.host, self.server_ports[node]))
        self.addCleanup(client.close)
        return client

    def create_user(self, prefix):
        name = f"{prefix}{self.suffix}"[:50]
        email = f"{name}@test.local"
        reg = self.new_client(0)
        reg.send({"msgid": REG_MSG, "name": name, "email": email, "password": PASSWORD})
        ack = reg.expect(by_msgid(REG_ACK), f"register {name}")
        self.assertEqual(ack.get("errno"), 0, ack)
        return User(name, email, ack["id"])

    def login(self, user, node=0, split=False):
        client = self.new_client(node)
        req = {"msgid": LOGIN_MSG, "email": user.email, "password": PASSWORD}
        if split:
            client.send_split(req)
        else:
            client.send(req)
        ack = client.expect(by_msgid(LOGIN_ACK), f"login {user.email}")
        self.assertEqual(ack.get("errno"), 0, ack)
        return client, ack

    def relogin(self, client, user, expect_ack=True):
        client.send({"msgid": LOGINOUT, "id": user.id})
        client.send({"msgid": LOGIN_MSG, "email": user.email, "password": PASSWORD})
        if not expect_ack:
            return None
        ack = client.expect(by_msgid(LOGIN_ACK), f"relogin {user.email}")
        self.assertEqual(ack.get("errno"), 0, ack)
        return ack

    def wait_for(self, predicate, desc, timeout=5.0):
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            try:
                last = predicate()
                if last:
                    return last
            except Exception as exc:  # 数据库尚未写入时继续轮询
                last = exc
            time.sleep(0.05)
        raise AssertionError(f"timeout waiting for {desc}; last={last}")

    def wait_user_state(self, user_id, state, timeout=5.0):
        self.wait_for(
            lambda: self._sql_value(
                f"SELECT state FROM user WHERE id={user_id}") == state,
            f"user {user_id} state={state}", timeout,
        )

    def wait_offline_count(self, user_id, count, timeout=5.0):
        self.wait_for(
            lambda: int(self._sql_value(
                f"SELECT COUNT(*) FROM offlinemessage WHERE userid={user_id}") or 0) >= count,
            f"offline message count >= {count}", timeout,
        )

    def wait_friend_rows(self, user_a, user_b, count, timeout=5.0):
        self.wait_for(
            lambda: int(self._sql_value(
                "SELECT COUNT(*) FROM friend WHERE (userid={0} AND friendid={1}) "
                "OR (userid={1} AND friendid={0})".format(user_a, user_b)) or 0) == count,
            f"friend rows == {count}", timeout,
        )

    # ---------- tests ----------

    def test_01_schema_constraints_and_cascade(self):
        indexes = {
            row[0] for row in self._sql(
                "SELECT DISTINCT INDEX_NAME FROM information_schema.STATISTICS "
                "WHERE TABLE_SCHEMA = DATABASE()")
        }
        for name in (
            "idx_friend_reverse", "idx_friend_request_target",
            "idx_groupuser_user", "idx_group_name", "idx_offline_user",
        ):
            self.assertIn(name, indexes)

        fks = {
            row[0] for row in self._sql(
                "SELECT CONSTRAINT_NAME FROM information_schema.REFERENTIAL_CONSTRAINTS "
                "WHERE CONSTRAINT_SCHEMA = DATABASE()")
        }
        for name in (
            "fk_friend_user", "fk_friend_friend",
            "fk_request_requester", "fk_request_target",
            "fk_groupuser_group", "fk_groupuser_user", "fk_offline_user",
        ):
            self.assertIn(name, fks)

        columns = {row[0] for row in self._sql("SHOW COLUMNS FROM offlinemessage")}
        self.assertTrue({"id", "created_at"}.issubset(columns), columns)

        name1 = f"itc1_{self.suffix}"
        name2 = f"itc2_{self.suffix}"
        self._mysql_run(
            "INSERT INTO user(name,email,password,state) VALUES"
            f"('{name1}','{name1}@test.local','x','offline'),"
            f"('{name2}','{name2}@test.local','x','offline');"
        )
        uid1 = int(self._sql_value(f"SELECT id FROM user WHERE name='{name1}'"))
        uid2 = int(self._sql_value(f"SELECT id FROM user WHERE name='{name2}'"))
        self._mysql_run(
            f"INSERT INTO friend(userid,friendid) VALUES({uid1},{uid2}),({uid2},{uid1});"
            f"INSERT INTO friend_request(userid,friendid) VALUES({uid1},{uid2});"
            f"INSERT INTO allgroup(groupname,groupdesc) VALUES('itg_{self.suffix}','cascade');"
        )
        gid = int(self._sql_value(f"SELECT id FROM allgroup WHERE groupname='itg_{self.suffix}'"))
        self._mysql_run(
            f"INSERT INTO groupuser(groupid,userid,grouprole) VALUES({gid},{uid1},'creator');"
            f"INSERT INTO offlinemessage(userid,message) VALUES({uid1},'cascade');"
        )

        self._mysql_run(f"DELETE FROM user WHERE id={uid1};")
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM friend WHERE userid={uid1} OR friendid={uid1}"), "0")
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM friend_request WHERE userid={uid1} OR friendid={uid1}"), "0")
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM groupuser WHERE userid={uid1}"), "0")
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM offlinemessage WHERE userid={uid1}"), "0")

        self._mysql_run(f"DELETE FROM allgroup WHERE id={gid}; DELETE FROM user WHERE id={uid2};")

    def test_02_protocol_auth_and_validation(self):
        anon = self.new_client(0)
        anon.send({"msgid": ADD_FRIEND_MSG, "account": "nobody@test.local"})
        with self.assertRaises(TimeoutError):
            anon.recv(0.6)

        user = self.create_user("proto")
        client, _ = self.login(user, 0, split=True)

        client.send({"msgid": ONE_CHAT_MSG, "toaccount": "missing@test.local", "msg": "x",
                     "time": "t"})
        ack = client.expect(by_msgid(CHAT_SEND_ACK), "unknown target ack")
        self.assertEqual(ack.get("errno"), 1, ack)

        client.send({"msgid": ONE_CHAT_MSG, "toaccount": user.email, "msg": "", "time": "t"})
        ack = client.expect(by_msgid(CHAT_SEND_ACK), "self/empty message ack")
        self.assertEqual(ack.get("errno"), 2, ack)

        client.send({"msgid": GROUP_CHAT_MSG, "groupname": "missing-group", "msg": "x",
                     "time": "t"})
        ack = client.expect(by_msgid(CHAT_SEND_ACK), "unknown group ack")
        self.assertEqual(ack.get("errno"), 1, ack)

        client.send({"msgid": ADD_FRIEND_MSG, "account": user.email})
        ack = client.expect(by_msgid(ADD_FRIEND_ACK), "self friend request ack")
        self.assertEqual(ack.get("errno"), 2, ack)

        raw = socket.create_connection((self.host, self.server_ports[0]), timeout=3)
        self.addCleanup(raw.close)
        raw.sendall(struct.pack("!I", 0))
        raw.settimeout(2)
        self.assertEqual(raw.recv(1), b"")

    def test_03_cross_node_chat_and_offline_order(self):
        user_a = self.create_user("dira")
        user_b = self.create_user("dirb")
        a, _ = self.login(user_a, 0)
        b, _ = self.login(user_b, 1)

        a.send({"msgid": ONE_CHAT_MSG, "toaccount": user_b.email, "name": user_a.name,
                "msg": "hello-node2", "time": "t1"})
        msg = b.expect(by_msgid(ONE_CHAT_MSG), "cross-node direct message")
        self.assertEqual(msg.get("msg"), "hello-node2")

        b.send({"msgid": LOGINOUT, "id": user_b.id})
        self.wait_user_state(user_b.id, "offline")
        a.send({"msgid": ONE_CHAT_MSG, "toaccount": user_b.email, "name": user_a.name,
                "msg": "offline-1", "time": "t2"})
        a.send({"msgid": ONE_CHAT_MSG, "toaccount": user_b.email, "name": user_a.name,
                "msg": "offline-2", "time": "t3"})
        self.wait_offline_count(user_b.id, 2)

        b.send({"msgid": LOGIN_MSG, "email": user_b.email, "password": PASSWORD})
        ack = b.expect(by_msgid(LOGIN_ACK), "B login with offline messages")
        offline = [json.loads(item) for item in ack.get("offlinemsg", [])]
        texts = [item.get("msg") for item in offline]
        self.assertEqual(texts, ["offline-1", "offline-2"], texts)

    def test_04_friend_request_transaction(self):
        user_a = self.create_user("fra")
        user_b = self.create_user("frb")
        a, _ = self.login(user_a, 0)
        b, _ = self.login(user_b, 1)

        a.send({"msgid": ADD_FRIEND_MSG, "account": user_b.email})
        ack = a.expect(by_msgid(ADD_FRIEND_ACK), "friend request ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        notify = b.expect(by_msgid(ADD_FRIEND_MSG), "friend request notification")
        self.assertEqual(notify.get("email"), user_a.email)

        a.send({"msgid": ADD_FRIEND_MSG, "account": user_b.email})
        ack = a.expect(by_msgid(ADD_FRIEND_ACK), "duplicate request ack")
        self.assertEqual(ack.get("errno"), 5, ack)

        b.send({"msgid": ADD_FRIEND_RESP_MSG, "account": user_a.email, "accept": True})
        ack = b.expect(by_msgid(ADD_FRIEND_RESP_ACK), "accept ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        self.assertTrue(ack.get("agreed"))
        a.expect(lambda m: m.get("msgid") == ADD_FRIEND_ACK and m.get("agreed") is True,
                 "requester acceptance notification")

        self.wait_friend_rows(user_a.id, user_b.id, 2)
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM friend_request WHERE userid={user_a.id} "
            f"AND friendid={user_b.id}"), "0")

        b.send({"msgid": ADD_FRIEND_RESP_MSG, "account": user_a.email, "accept": True})
        ack = b.expect(by_msgid(ADD_FRIEND_RESP_ACK), "duplicate accept ack")
        self.assertEqual(ack.get("errno"), 1, ack)

        # 人为插入一条残留请求，删除好友事务应同时清理双方请求
        self._mysql_run(
            f"INSERT INTO friend_request(userid,friendid) VALUES({user_a.id},{user_b.id});")
        a.send({"msgid": DEL_FRIEND_MSG, "account": user_b.email})
        ack = a.expect(by_msgid(DEL_FRIEND_ACK), "delete friend ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        b.expect(by_msgid(DEL_FRIEND_MSG), "removed notification")
        self.wait_friend_rows(user_a.id, user_b.id, 0)
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM friend_request WHERE "
            f"(userid={user_a.id} AND friendid={user_b.id}) OR "
            f"(userid={user_b.id} AND friendid={user_a.id})"), "0")

    def test_05_group_transaction_and_cross_node_chat(self):
        user_a = self.create_user("gra")
        user_b = self.create_user("grb")
        user_c = self.create_user("grc")
        a, _ = self.login(user_a, 0)
        b, _ = self.login(user_b, 1)
        c, _ = self.login(user_c, 0)
        group_name = f"itg_{self.suffix}"

        a.send({"msgid": CREATE_GROUP_MSG, "groupname": group_name, "groupdesc": "integration"})
        ack = a.expect(by_msgid(CREATE_GROUP_ACK), "create group ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        group_id = int(self._sql_value(
            f"SELECT id FROM allgroup WHERE groupname='{group_name}'"))
        self.assertEqual(self._sql_value(
            f"SELECT COUNT(*) FROM groupuser WHERE groupid={group_id} "
            f"AND userid={user_a.id} AND grouprole='creator'"), "1")

        b.send({"msgid": GROUP_CHAT_MSG, "groupname": group_name, "msg": "not-member",
                "time": "t"})
        ack = b.expect(by_msgid(CHAT_SEND_ACK), "non-member group chat ack")
        self.assertEqual(ack.get("errno"), 3, ack)

        c.send({"msgid": ADD_GROUP_MSG, "groupname": group_name})
        ack = c.expect(by_msgid(ADD_GROUP_ACK), "C join group ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        b.send({"msgid": ADD_GROUP_MSG, "groupname": group_name})
        ack = b.expect(by_msgid(ADD_GROUP_ACK), "B join group ack")
        self.assertEqual(ack.get("errno"), 0, ack)
        b.send({"msgid": ADD_GROUP_MSG, "groupname": group_name})
        ack = b.expect(by_msgid(ADD_GROUP_ACK), "B duplicate join group ack")
        self.assertEqual(ack.get("errno"), 0, ack)

        a.send({"msgid": GROUP_CHAT_MSG, "groupname": group_name, "msg": "group-live",
                "time": "t"})
        for client, desc in ((c, "C same-node group message"), (b, "B cross-node group message")):
            msg = client.expect(by_msgid(GROUP_CHAT_MSG), desc)
            self.assertEqual(msg.get("msg"), "group-live")
            self.assertEqual(msg.get("id"), user_a.id)

        b.send({"msgid": LOGINOUT, "id": user_b.id})
        self.wait_user_state(user_b.id, "offline")
        a.send({"msgid": GROUP_CHAT_MSG, "groupname": group_name, "msg": "group-offline",
                "time": "t"})
        self.wait_offline_count(user_b.id, 1)
        b.send({"msgid": LOGIN_MSG, "email": user_b.email, "password": PASSWORD})
        ack = b.expect(by_msgid(LOGIN_ACK), "B login with offline group message")
        offline = [json.loads(item) for item in ack.get("offlinemsg", [])]
        self.assertIn("group-offline", [item.get("msg") for item in offline])

    def test_06_immediate_relogin_keeps_subscription(self):
        user_a = self.create_user("rca")
        user_b = self.create_user("rcb")
        a, _ = self.login(user_a, 0)
        b, _ = self.login(user_b, 1)

        for i in range(5):
            b.send({"msgid": LOGINOUT, "id": user_b.id})
            b.send({"msgid": LOGIN_MSG, "email": user_b.email, "password": PASSWORD})
            ack = b.expect(by_msgid(LOGIN_ACK), f"immediate relogin {i}")
            self.assertEqual(ack.get("errno"), 0, ack)
            text = f"race-{i}"
            a.send({"msgid": ONE_CHAT_MSG, "toaccount": user_b.email, "msg": text,
                    "time": "t"})
            msg = b.expect(
                lambda m, expected=text: m.get("msgid") == ONE_CHAT_MSG
                and m.get("msg") == expected,
                f"cross-node message after relogin {i}", timeout=3.0)
            self.assertEqual(msg.get("id"), user_a.id)


if __name__ == "__main__":
    unittest.main(verbosity=2)
