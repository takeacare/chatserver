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

// 删除好友：双向移除
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

    bool ok = mysql.update("delete from friend where userid=" + to_string(userid) +
                           " and friendid=" + to_string(friendid));
    ok = ok && mysql.update("delete from friend where userid=" + to_string(friendid) +
                            " and friendid=" + to_string(userid));

    if (!ok || !mysql.update("commit"))
    {
        mysql.update("rollback");
        return false;
    }
    return true;
}

// 添加好友关系：双向落库（A->B 与 B->A），任一方向缺失时补齐
FriendModel::AddResult FriendModel::addFriend(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return AddResult::FAILED;
    }

    if (!mysql.update("start transaction"))
    {
        return AddResult::FAILED;
    }

    // 统计两个方向已存在的关系行数
    string countSql = "select count(*) from friend where (userid=" + to_string(userid) +
                      " and friendid=" + to_string(friendid) + ") or (userid=" +
                      to_string(friendid) + " and friendid=" + to_string(userid) + ")";

    long exists = -1;
    MYSQL_RES *res = mysql.query(countSql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        exists = (row != nullptr && row[0] != nullptr) ? atol(row[0]) : -1;
        mysql_free_result(res);
    }
    if (exists < 0)
    {
        mysql.update("rollback");
        return AddResult::FAILED;
    }

    if (exists >= 2)
    {
        mysql.update("commit");
        return AddResult::ALREADY;
    }

    // 补齐缺失方向：insert ignore 保证重复行不报错（防御主键冲突）
    bool ok = mysql.update("insert ignore into friend values(" + to_string(userid) + ", " +
                           to_string(friendid) + ")");
    ok = ok && mysql.update("insert ignore into friend values(" + to_string(friendid) + ", " +
                            to_string(userid) + ")");

    if (!ok || !mysql.update("commit"))
    {
        mysql.update("rollback");
        return AddResult::FAILED;
    }

    return AddResult::ADDED;
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
