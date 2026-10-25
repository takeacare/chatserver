#include "groupmodel.hpp"
#include "db.h"
#include <cstdlib>
#include <string>
using namespace std;

// 事务化创建群组：群信息与创建者成员关系同时落库
bool GroupModel::createGroupWithCreator(Group &group, int creatorId)
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

    string sql = "insert into allgroup(groupname, groupdesc) values('" +
                 mysql.escape(group.getName()) + "', '" +
                 mysql.escape(group.getDesc()) + "')";
    if (!mysql.update(sql))
    {
        mysql.update("rollback");
        return false;
    }

    const long long groupId = mysql_insert_id(mysql.getConnection());
    string memberSql = "insert into groupuser(groupid, userid, grouprole) values(" +
                       to_string(groupId) + ", " + to_string(creatorId) + ", 'creator')";
    if (!mysql.update(memberSql) || !mysql.update("commit"))
    {
        mysql.update("rollback");
        return false;
    }

    group.setId(static_cast<int>(groupId));
    return true;
}

// 加入群组；重复加入视为成功
bool GroupModel::addGroup(int userid, int groupid, string role)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    // on duplicate key 保证重复加入不报主键冲突，同时仍会暴露外键等真实错误
    string sql = "insert into groupuser(groupid, userid, grouprole) values(" +
                 to_string(groupid) + ", " + to_string(userid) + ", '" +
                 mysql.escape(role) + "') on duplicate key update userid = userid";
    return mysql.update(sql);
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

    if (groupVec.empty())
    {
        return groupVec;
    }

    // 一次性联查所有群的成员，避免逐群查询（N+1）
    string ids;
    for (size_t i = 0; i < groupVec.size(); ++i)
    {
        if (i > 0)
        {
            ids += ",";
        }
        ids += to_string(groupVec[i].getId());
    }

    string userSql = "select b.groupid, a.id, a.name, a.state, b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid in (" +
                     ids + ") order by b.groupid asc";

    MYSQL_RES *userRes = mysql.query(userSql);
    if (userRes == nullptr)
    {
        return groupVec;
    }

    unordered_map<int, vector<GroupUser>> membersByGroup;
    while ((row = mysql_fetch_row(userRes)) != nullptr)
    {
        GroupUser user;
        user.setId(atoi(row[1]));
        user.setName(row[2] ? row[2] : "");
        user.setState(row[3] ? row[3] : "");
        user.setRole(row[4] ? row[4] : "");
        membersByGroup[atoi(row[0])].push_back(std::move(user));
    }
    mysql_free_result(userRes);

    for (Group &group : groupVec)
    {
        auto it = membersByGroup.find(group.getId());
        if (it != membersByGroup.end())
        {
            group.getUsers() = it->second;
        }
    }
    return groupVec;
}

// 根据群名称查询群组（同名群按 id 升序返回）
vector<Group> GroupModel::queryGroupsByName(const string &groupname)
{
    vector<Group> groupVec;

    MySQL mysql;
    if (!mysql.connect())
    {
        return groupVec;
    }

    string sql = "select id, groupname, groupdesc from allgroup where groupname = '" +
                 mysql.escape(groupname) + "' order by id asc";

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return groupVec;
    }

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr)
    {
        Group group;
        group.setId(atoi(row[0]));
        group.setName(row[1] ? row[1] : "");
        group.setDesc(row[2] ? row[2] : "");
        groupVec.push_back(group);
    }
    mysql_free_result(res);

    return groupVec;
}

// 判断用户是否为群组成员
bool GroupModel::isMember(int userid, int groupid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }

    string sql = "select 1 from groupuser where groupid = " + to_string(groupid) +
                 " and userid = " + to_string(userid) + " limit 1";

    MYSQL_RES *res = mysql.query(sql);
    if (res == nullptr)
    {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    const bool member = (row != nullptr);
    mysql_free_result(res);
    return member;
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
