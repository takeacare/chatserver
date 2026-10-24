#include "friendmodel.hpp"
#include "db.h"
#include <cstdlib>
#include <string>
using namespace std;

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return;
    }

    // 1.组装sql语句
    string sql = "insert into friend values(" + to_string(userid) + ", " + to_string(friendid) + ")";
    mysql.update(sql);
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
    string sql = "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid=" +
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
        user.setState(row[2] ? row[2] : "");
        vec.push_back(user);
    }
    mysql_free_result(res);

    return vec;
}
