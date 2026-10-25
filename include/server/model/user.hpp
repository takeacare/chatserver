#ifndef USER_H
#define USER_H

#include <string>
using namespace std;

// User表的ORM类
class User
{
public:
    User(int id = -1, string name = "", string pwd = "", string state = "offline", string email = "")
    {
        this->id = id;
        this->name = name;
        this->password = pwd;
        this->state = state;
        this->email = email;
    }

    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPwd(string pwd) { this->password = pwd; }
    void setState(string state) { this->state = state; }
    void setEmail(string email) { this->email = email; }

    int getId() { return this->id; }
    string getName() { return this->name; }
    string getPwd() { return this->password; }
    string getState() { return this->state; }
    string getEmail() { return this->email; }

protected:
    int id;
    string name;
    string password;
    string state;
    string email;
};

#endif
