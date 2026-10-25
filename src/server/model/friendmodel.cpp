#include "friendmodel.hpp"
#include "db.h"
#include <cstdlib>
#include <string>
using namespace std;

// 双向好友关系是否已存在（任一方向存在即为好友，兼容历史单向数据）
bool FriendModel::isFriend(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    string sql = "select 1 from friend where (userid=" + to_string(userid) +
                 " and friendid=" + to_string(friendid) + ") or (userid=" +
                 to_string(friendid) + " and friendid=" + to_string(userid) + ") limit 1";

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    const bool exists = (row != nullptr);
    mysql_free_result(res);
    return exists;
}

// 删除好友：双向移除好友关系，并清理双方残留的好友请求
bool FriendModel::removeFriend(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    if (!mysql.update("start transaction"))
    {
        return false;
    }

    const string a = to_string(userid);
    const string b = to_string(friendid);
    bool ok = mysql.update("delete from friend where (userid=" + a + " and friendid=" + b +
                           ") or (userid=" + b + " and friendid=" + a + ")");
    ok = ok && mysql.update("delete from friend_request where (userid=" + a +
                            " and friendid=" + b + ") or (userid=" + b +
                            " and friendid=" + a + ")");

    if (!ok || !mysql.update("commit"))
    {
        mysql.update("rollback");
        return false;
    }
    return true;
}

// 事务化同意好友请求：请求校验、双向好友落库、请求清理同一事务
FriendModel::AcceptResult FriendModel::acceptRequest(int requester, int receiver)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return AcceptResult::FAILED;
    }

    if (!mysql.update("start transaction"))
    {
        return AcceptResult::FAILED;
    }

    const string a = to_string(requester);
    const string b = to_string(receiver);

    // 锁定待处理请求，避免与重复处理并发
    string checkSql = "select 1 from friend_request where userid=" + a + " and friendid=" + b +
                      " for update";
    MYSQL_RES *res = mysql.query(checkSql);
    if (res == nullptr)
    {
        mysql.update("rollback");
        return AcceptResult::FAILED;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    const bool pending = (row != nullptr);
    mysql_free_result(res);
    if (!pending)
    {
        mysql.update("rollback");
        return AcceptResult::NO_REQUEST;
    }

    bool ok = mysql.update("insert ignore into friend(userid, friendid) values(" + a + ", " + b +
                           ")");
    ok = ok && mysql.update("insert ignore into friend(userid, friendid) values(" + b + ", " +
                            a + ")");
    // 双向请求都清掉，避免另一方向的残留请求继续弹窗
    ok = ok && mysql.update("delete from friend_request where (userid=" + a +
                            " and friendid=" + b + ") or (userid=" + b +
                            " and friendid=" + a + ")");

    if (!ok || !mysql.update("commit"))
    {
        mysql.update("rollback");
        return AcceptResult::FAILED;
    }

    return AcceptResult::ACCEPTED;
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid)
{
    vector<User> vec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return vec;
    }

    // 1.组装sql语句
    string sql = "select a.id,a.name,a.email,a.state from user a inner join friend b on b.friendid = a.id where b.userid=" +
                 to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return vec;
    }

    // 把userid用户的所有好友信息放入vec中返回
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        User user;
        user.setId(atoi(row[0]));
        user.setName(row[1] ? row[1] : "");
        user.setEmail(row[2] ? row[2] : "");
        user.setState(row[3] ? row[3] : "");
        vec.push_back(user);
    }
    mysql_free_result(res);

    return vec;
}
