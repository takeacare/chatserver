#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;

// 维护好友信息的操作接口方法
class FriendModel
{
public:
    // 同意好友请求的结果
    enum class AcceptResult
    {
        ACCEPTED,   // 已建立双向好友关系并删除待处理请求
        NO_REQUEST, // 没有对应的待处理请求
        FAILED      // 数据库操作失败
    };

    // 事务化同意好友请求：校验请求、双向落库、清理请求在同一事务内完成
    AcceptResult acceptRequest(int requester, int receiver);

    // 双向好友关系是否已存在（任一方向存在即为好友）
    bool isFriend(int userid, int friendid);

    // 删除好友：双向移除好友关系，并清理双方残留的好友请求
    bool removeFriend(int userid, int friendid);

    // 返回用户好友列表
    vector<User> query(int userid);
};

#endif