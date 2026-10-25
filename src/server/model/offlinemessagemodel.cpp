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

    // 显式列名：表中有自增 id 与 created_at 列
    string sql = "insert into offlinemessage(userid, message) values(" + to_string(userid) +
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

    // 按自增 id 保证补发顺序，并记录本次取出的行 id
    string sql = "select id, message from offlinemessage where userid = " + to_string(userid) +
                 " order by id asc";

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        mysql.update("rollback");
        return vec;
    }

    vector<unsigned long long> ids;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        ids.push_back(strtoull(row[0] ? row[0] : "0", nullptr, 10));
        vec.push_back(row[1] ? row[1] : "");
    }
    mysql_free_result(res);

    // 只删除本次取出的 id：事务期间新写入的消息不会被误删，留待下次登录补发
    if (!ids.empty())
    {
        string idList;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i > 0)
            {
                idList += ",";
            }
            idList += to_string(ids[i]);
        }
        if (!mysql.update("delete from offlinemessage where id in (" + idList + ")"))
        {
            mysql.update("rollback");
            return vector<string>();
        }
    }
    mysql.update("commit");

    return vec;
}
