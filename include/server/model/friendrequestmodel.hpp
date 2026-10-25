#ifndef FRIENDREQUESTMODEL_H
#define FRIENDREQUESTMODEL_H

#include <string>
#include <vector>
using namespace std;

// 维护待处理好友请求的操作接口方法
class FriendRequestModel
{
public:
    // 发起好友请求（拒绝后的重复发起会覆盖原记录）
    bool upsert(int userid, int friendid);

    // 是否存在待处理请求（userid → friendid）
    bool existsPending(int userid, int friendid);

    // 查询发给指定用户的全部待处理请求，返回 JSON 字符串数组 [{userid, name, email}]
    vector<string> queryPending(int friendid);

    // 删除请求记录（同意/拒绝处理完成后调用），返回是否确有删除
    bool remove(int userid, int friendid);
};

#endif
