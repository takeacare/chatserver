#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

// 维护群组信息的操作接口方法
class GroupModel
{
public:
    // 事务化创建群组：群信息与创建者成员关系同时落库
    bool createGroupWithCreator(Group &group, int creatorId);
    // 加入群组；重复加入视为成功
    bool addGroup(int userid, int groupid, string role);
    // 查询用户所在群组信息
    vector<Group> queryGroups(int userid);
    // 根据群名称查询群组（同名群按 id 升序返回，用于按名称加入群组）
    vector<Group> queryGroupsByName(const string &groupname);
    // 判断用户是否为群组成员（群聊发言权限校验）
    bool isMember(int userid, int groupid);
    // 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
    vector<int> queryGroupUsers(int userid, int groupid);
};

#endif