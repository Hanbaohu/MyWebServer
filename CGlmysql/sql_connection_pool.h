#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <mysql/mysql.h>
#include <list>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();              // 获取数据库连接
    bool ReleaseConnection(MYSQL *conn); // 释放连接
    int GetFreeConn();                   // 获取空闲连接数
    void DestroyPool();                  // 销毁所有连接

    // 单例模式，确保一个类只有一个实例，并提供一个全局访问点以获取该实例
    // 单例类必须自己创建自己的唯一实例
    // 需要构造函数私有化，确保外部无法直接实例化对象
    static connection_pool *GetInstance();

    // 初始化函数
    void init(string url, string User, string Passward, string DataBaseName, int Port, int MaxConn, int close_log);

private:
    // 单例模式下的初始化函数和析构函数
    connection_pool();
    ~connection_pool();

    // 参数声明
public:
    string m_url;          // 主机地址
    string m_Port;         // 数据库的端口号
    string m_User;         // 登录数据库的用户名
    string m_PassWord;     // 登录数据库的密码
    string m_DatabaseName; // 使用的数据库名
    int m_close_log;       // 日志开关

private:
    list<MYSQL *> connList; // 连接池
    sem reverse;            // 定义一个信号类
    locker lock;            // 定义一个锁的类
    int m_CurConn;          // 当前已使用的连接数
    int m_FreeConn;         // 当前空闲的连接数
    int m_MaxConn;          // 最大连接数
    sem reserve;            // 定义一个信号量,类对象
};

// 实现资源获取即初始化（Resource Acquisition Is Initialization，RAII）的模式
// 通过构造函数和析构函数来管理MYSQL连接的生命周期，确保连接在适当的时候被正确地释放，以避免资源泄漏和管理错误
class connectionRAII
{
public:
    // 初始化函数,两个*代表指向MYSQL *指针的指针
    connectionRAII(MYSQL **con, connection_pool *connPool);
    ~connectionRAII();

private:
    MYSQL *connRAII; //声明一个MYSQL指针，代表需要管理连接对象
    connection_pool *poolRAII; // 声明需要管理的连接池对象
};

#endif
