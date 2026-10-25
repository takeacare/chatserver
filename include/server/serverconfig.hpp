#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <fstream>
#include <iostream>
#include <string>
using namespace std;

/*
服务器运行配置：从 key=value 配置文件加载，避免在代码中硬编码数据库凭据。
默认探测路径：config/server.conf、server.conf（相对工作目录）。
配置文件包含数据库凭据，不应提交到公共仓库。
*/
class ServerConfig
{
public:
    static ServerConfig &instance()
    {
        static ServerConfig cfg;
        return cfg;
    }

    // 解析 key=value 配置文件；# 或 ; 开头视为注释
    bool load(const string &path)
    {
        ifstream in(path);
        if (!in.is_open())
        {
            return false;
        }

        string line;
        while (getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            // 去掉行内注释
            const auto comment = line.find_first_of("#;");
            if (comment != string::npos)
            {
                line = line.substr(0, comment);
            }
            const auto pos = line.find('=');
            if (pos == string::npos)
            {
                continue;
            }
            const string key = trim(line.substr(0, pos));
            const string value = trim(line.substr(pos + 1));
            if (key.empty())
            {
                continue;
            }
            if (key == "db_host")
            {
                _dbHost = value;
            }
            else if (key == "db_port")
            {
                _dbPort = atoi(value.c_str());
            }
            else if (key == "db_user")
            {
                _dbUser = value;
            }
            else if (key == "db_password")
            {
                _dbPassword = value;
            }
            else if (key == "db_name")
            {
                _dbName = value;
            }
            else if (key == "db_pool_size")
            {
                _dbPoolSize = static_cast<size_t>(atoi(value.c_str()));
            }
            else if (key == "redis_host")
            {
                _redisHost = value;
            }
            else if (key == "redis_port")
            {
                _redisPort = atoi(value.c_str());
            }
        }

        if (_dbPort <= 0) _dbPort = 3306;
        if (_redisPort <= 0) _redisPort = 6379;
        if (_dbPoolSize == 0) _dbPoolSize = 8;
        return true;
    }

    // 按常见路径探测配置文件；全部失败返回 false
    bool loadDefault()
    {
        const char *candidates[] = {"config/server.conf", "server.conf"};
        for (const char *path : candidates)
        {
            ifstream test(path);
            if (test.is_open())
            {
                return load(path);
            }
        }
        return false;
    }

    const string &dbHost() const { return _dbHost; }
    int dbPort() const { return _dbPort; }
    const string &dbUser() const { return _dbUser; }
    const string &dbPassword() const { return _dbPassword; }
    const string &dbName() const { return _dbName; }
    size_t dbPoolSize() const { return _dbPoolSize; }
    const string &redisHost() const { return _redisHost; }
    int redisPort() const { return _redisPort; }

private:
    ServerConfig() = default;

    static string trim(const string &s)
    {
        const auto begin = s.find_first_not_of(" \t");
        if (begin == string::npos)
        {
            return "";
        }
        const auto end = s.find_last_not_of(" \t");
        return s.substr(begin, end - begin + 1);
    }

    string _dbHost = "127.0.0.1";
    int _dbPort = 3306;
    string _dbUser = "root";
    string _dbPassword; // 默认留空，必须由配置文件提供
    string _dbName = "chat";
    size_t _dbPoolSize = 8;
    string _redisHost = "127.0.0.1";
    int _redisPort = 6379;
};

#endif
