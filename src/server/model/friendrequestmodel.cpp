#include "friendrequestmodel.hpp"
#include "db.h"
#include <json.hpp>
#include <cstdlib>
#include <string>
using namespace std;
using json = nlohmann::json;

bool FriendRequestModel::upsert(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    // 主键冲突即视为重新发起，覆盖为新的待处理请求
    string sql = "replace into friend_request values(" + to_string(userid) + ", " +
                 to_string(friendid) + ")";
    return mysql.update(sql);
}

bool FriendRequestModel::existsPending(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    string sql = "select 1 from friend_request where userid = " + to_string(userid) +
                 " and friendid = " + to_string(friendid) + " limit 1";

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

vector<string> FriendRequestModel::queryPending(int friendid)
{
    vector<string> vec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return vec;
    }

    // 关联 user 表返回发起方的展示信息（不含敏感字段）
    string sql = "select a.userid, b.name, b.email from friend_request a \
            inner join user b on b.id = a.userid where a.friendid = " +
                 to_string(friendid) + " order by a.userid asc";

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return vec;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        json js;
        js["userid"] = row[0] ? atoi(row[0]) : 0;
        js["name"] = row[1] ? row[1] : "";
        js["email"] = row[2] ? row[2] : "";
        vec.push_back(js.dump());
    }
    mysql_free_result(res);
    return vec;
}

bool FriendRequestModel::remove(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    string sql = "delete from friend_request where userid = " + to_string(userid) +
                 " and friendid = " + to_string(friendid);
    if (!mysql.update(sql))
    {
        return false;
    }

    // 影响0行说明请求本就不存在（重复处理/已撤销），不能视为删除成功
    return mysql_affected_rows(mysql.getConnection()) > 0;
}
