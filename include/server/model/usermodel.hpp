#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

// User表的数据操作类
class UserModel {
public:
    // User表的增加方法
    bool insert(User &user);

    // 根据用户号码查询用户信息
    User query(int id);

    // 根据邮箱查询用户信息（邮箱登录）
    User queryByEmail(const string &email);

    // 根据用户名查询用户信息（加好友等按名字定位用户）
    User queryByName(const string &name);

    // 更新用户的状态信息
    bool updateState(User user);

    // 更新用户的密码（传入已哈希的密码串）
    bool updatePassword(int id, const string &pwdHash);

    // 重置用户的状态信息
    void resetState();
};

#endif