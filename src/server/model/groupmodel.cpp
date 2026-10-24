#include "groupmodel.hpp"
#include "db.h"
#include <cstdlib>
#include <string>
using namespace std;

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    // 1.组装sql语句（群名与描述必须转义）
    string sql = "insert into allgroup(groupname, groupdesc) values('" +
                 mysql.escape(group.getName()) + "', '" +
                 mysql.escape(group.getDesc()) + "')";

    if (mysql.update(sql))
    {
        group.setId(mysql_insert_id(mysql.getConnection()));
        return true;
    }

    return false;
}

// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return;
    }

    // 1.组装sql语句
    string sql = "insert into groupuser values(" + to_string(groupid) + ", " +
                 to_string(userid) + ", '" + mysql.escape(role) + "')";
    mysql.update(sql);
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    /*
    1. 先根据userid在groupuser表中查询出该用户所属的群组信息
    2. 在根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表联合查询，查出用户的详细信息
    */
    vector<Group> groupVec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return groupVec;
    }

    string sql = "select a.id,a.groupname,a.groupdesc from allgroup a inner join \
         groupuser b on a.id = b.groupid where b.userid=" +
                 to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return groupVec;
    }

    MYSQL_ROW row;
    // 查出userid所有的群组信息
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        Group group;
        group.setId(atoi(row[0]));
        group.setName(row[1] ? row[1] : "");
        group.setDesc(row[2] ? row[2] : "");
        groupVec.push_back(group);
    }
    mysql_free_result(res);

    // 查询群组的用户信息
    for (Group &group : groupVec)
    {
        string userSql = "select a.id,a.name,a.state,b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid=" +
                          to_string(group.getId());

        MYSQL_RES *userRes = mysql.query(userSql);
        if (userRes == nullptr)
        {
            continue;
        }

        while ((row = mysql_fetch_row(userRes)) != nullptr)
        {
            GroupUser user;
            user.setId(atoi(row[0]));
            user.setName(row[1] ? row[1] : "");
            user.setState(row[2] ? row[2] : "");
            user.setRole(row[3] ? row[3] : "");
            group.getUsers().push_back(user);
        }
        mysql_free_result(userRes);
    }
    return groupVec;
}

// 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    vector<int> idVec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return idVec;
    }

    string sql = "select userid from groupuser where groupid = " + to_string(groupid) +
                 " and userid != " + to_string(userid);

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return idVec;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        idVec.push_back(atoi(row[0]));
    }
    mysql_free_result(res);

    return idVec;
}
