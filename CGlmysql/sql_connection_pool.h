#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <mysql/mysql.h>
#include <list>
#include <string>
#include"../lock/locker.h"
#include <string>

using namespace std;

class connection_pool
{
public:
    MYSQL *GetConnection();             //获取数据库连接
    bool ReleaseConnection(MYSQL *conn); //释放连接
    int GetFreeConn();                  //获取连接
    void DestroyPool();                 //销毁所有连接

    //单例模式，确保一个类只有一个实例，并提供一个全局访问点以获取该实例
    //单例类必须自己创建自己的唯一实例
    //需要构造函数私有化，确保外部无法直接实例化对象
    static connection_pool *GetInstance();

    //初始化函数
    void init(string url,string User,string Passward,string DataBaseName,int Port,int MaxConn,int close_log);

    //参数声明
    string m_url;       //主机地址
    string m_Port;      //数据库的端口号
    string m_User;      //登录数据库的用户名
    string m_PassWord;  //登录数据库的密码
    string m_DatabaseName;  //使用的数据库名
    int m_close_log;    //日志开关

private:
    list<MYSQL *> connList;//连接池
    sem reverse;  //定义一个信号类
    locker lock;  //定义一个锁的类
    int m_CurConn; //当前已使用的连接数
    int m_FreeConn; //当前空闲的连接数

};


#endif
