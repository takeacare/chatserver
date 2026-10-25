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

// 事务方式取出并清空用户的离线消息，避免查询与删除之间新写入的消息丢失
vector<string> OfflineMsgModel::take(int userid)
{
    vector<string> vec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return vec;
    }

    if (!mysql.update("start transaction"))
    {
        return vec;
    }

    string sql = "select message from offlinemessage where userid = " + to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        mysql.update("rollback");
        return vec;
    }

    // 把userid用户的所有离线消息放入vec中返回
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        vec.push_back(row[0] ? row[0] : "");
    }
    mysql_free_result(res);

    // 同一事务内删除已取出的消息，要么全部生效要么全部保留
    if (!mysql.update("delete from offlinemessage where userid=" + to_string(userid)))
    {
        mysql.update("rollback");
        return vector<string>();
    }
    mysql.update("commit");

    return vec;
}
