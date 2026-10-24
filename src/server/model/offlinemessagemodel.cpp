#include "offlinemessagemodel.hpp"
#include "db.h"
#include <cstdlib>
#include <string>
using namespace std;

// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return;
    }

    // 1.组装sql语句（消息内容必须转义）
    string sql = "insert into offlinemessage values(" + to_string(userid) +
                 ", '" + mysql.escape(msg) + "')";
    mysql.update(sql);
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return;
    }

    // 1.组装sql语句
    string sql = "delete from offlinemessage where userid=" + to_string(userid);
    mysql.update(sql);
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    vector<string> vec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return vec;
    }

    // 1.组装sql语句
    string sql = "select message from offlinemessage where userid = " + to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return vec;
    }

    // 把userid用户的所有离线消息放入vec中返回
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        vec.push_back(row[0] ? row[0] : "");
    }
    mysql_free_result(res);

    return vec;
}
