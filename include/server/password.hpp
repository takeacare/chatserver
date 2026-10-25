#ifndef PASSWORD_H
#define PASSWORD_H

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <fstream>
#include <string>
using namespace std;

/*
密码哈希工具：SHA-256 + 随机盐，使用 OpenSSL EVP 接口。
存储格式：sha256$<salt hex>$<digest hex>
兼容旧明文数据：verifyPassword 对明文直接比对，登录成功后由调用方升级为哈希存储。
*/
namespace pwd
{

inline string toHex(const unsigned char *data, size_t len)
{
    static const char *digits = "0123456789abcdef";
    string out(len * 2, '0');
    for (size_t i = 0; i < len; ++i)
    {
        out[2 * i] = digits[data[i] >> 4];
        out[2 * i + 1] = digits[data[i] & 0x0f];
    }
    return out;
}

inline string sha256Hex(const string &data)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    EVP_Digest(data.data(), data.size(), md, &mdLen, EVP_sha256(), nullptr);
    return toHex(md, mdLen);
}

// 16 字节随机盐，来源 /dev/urandom
inline string randomSalt()
{
    unsigned char buf[16];
    ifstream urandom("/dev/urandom", ios::binary);
    if (urandom.is_open())
    {
        urandom.read(reinterpret_cast<char *>(buf), sizeof(buf));
        if (urandom.gcount() == static_cast<long>(sizeof(buf)))
        {
            return toHex(buf, sizeof(buf));
        }
    }
    // 兜底：极端环境下 /dev/urandom 不可用时使用伪随机
    for (unsigned char &b : buf)
    {
        b = static_cast<unsigned char>(rand() & 0xff);
    }
    return toHex(buf, sizeof(buf));
}

inline string hashPassword(const string &pwd)
{
    const string salt = randomSalt();
    return "sha256$" + salt + "$" + sha256Hex(salt + pwd);
}

// 常量时间比较，防止时序侧信道
inline bool constantTimeEqual(const string &a, const string &b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

inline bool isHashed(const string &stored)
{
    return stored.rfind("sha256$", 0) == 0;
}

// 校验密码；兼容旧的明文存储（登录成功后由调用方升级为哈希）
inline bool verifyPassword(const string &stored, const string &input)
{
    if (isHashed(stored))
    {
        const string::size_type second = stored.find('$', 7);
        if (second == string::npos)
        {
            return false;
        }
        const string salt = stored.substr(7, second - 7);
        const string digest = stored.substr(second + 1);
        return constantTimeEqual(sha256Hex(salt + input), digest);
    }
    return stored == input;
}

} // namespace pwd

#endif
