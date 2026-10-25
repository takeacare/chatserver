#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;

// 维护好友信息的操作接口方法
class FriendModel
{
public:
    // 添加好友的结果
    enum class AddResult
    {
        ADDED,   // 新建立（或补齐了缺失方向）
        ALREADY, // 双向好友关系已存在
        FAILED   // 数据库操作失败
    };

    // 添加好友关系：双向落库（A->B 与 B->A），已存在的方向不重复插入
    AddResult addFriend(int userid, int friendid);

    // 双向好友关系是否已存在（任一方向存在即为好友）
    bool isFriend(int userid, int friendid);

    // 删除好友：双向移除
    bool removeFriend(int userid, int friendid);

    // 返回用户好友列表
    vector<User> query(int userid);
};

#endif