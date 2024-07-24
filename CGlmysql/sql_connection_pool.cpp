#include <mysql/mysql.h>
#include <string>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
    // 将当前连接数和空闲连接数设为0
    m_CurConn = 0;
    m_FreeConn = 0;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = nullptr; // 声明一个MYSQL指针用于存储获取到的连接

    if (connList.size() == 0) // 判断连接池中是否有可用连接
        return nullptr;

    reverse.wait(); // 使用信号类，等待可用连接

    lock.lock(); // 加锁，确保线程安全

    con = connList.front(); // 获取连接池中的第一个连接
    connList.pop_front();   // 移除

    --m_FreeConn; // 空闲连接数-1
    ++m_CurConn;  // 当前连接数+1

    lock.unlock(); // 解锁

    return con; // 返回得到的连接地址
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if (con == nullptr)
        return false;

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reverse.post();
    return true;
}

// 返回当前空闲的连接数
int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{
    lock.lock();
    if (connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for (it = connList.begin(); it != connList.end(); it++)
        {
            MYSQL *con = *it; // 获取当前迭代器指向的连接
            mysql_close(con); // 关闭数据库连接
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

// 获取单例对象的实例
connection_pool *connection_pool::GetInstance()
{
    // 静态变量是在程序运行期间在静态存储区分配的变量
    // 其生命周期从程序启动时直到程序结束
    // 定义了一个静态局部变量
    // 只会在第一次调用该函数时被初始化，并在整个程序运行期间保留其值
    static connection_pool connPool;
    return &connPool; // 每次返回的都是同一个实例指针
}

void connection_pool::init(string url, string User,
                           string Passward, string DataBaseName,
                           int Port, int MaxConn, int close_log)
{
    // 初始化类的成员变量
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = Passward;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;

    // 循环创建MaxConn个数据库连接
    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = nullptr;
        con = mysql_init(con); // 初始化MYSQL对象

        // 如果初始化错误
        if (con == nullptr)
        {
            LOG_ERROR("MYSQL Error"); // 记录一个错误日志
            exit(1);                  // 以错误状态退出程序
        }

        // 建立到MySQL数据库的真实连接
        // mysql_real_connect()函数连接到MySQL数据库
        con = mysql_real_connect(con, url.c_str(), User.c_str(), Passward.c_str(),
                                 DataBaseName.c_str(), Port, NULL, 0);

        // 连接错误返回空指针，检测是否连接出现错误
        if (con == nullptr)
        {
            LOG_ERROR("MYSQL Error"); // 记录一个错误日志
            exit(1);                  // 以错误状态退出程序
        }
        connList.push_back(con); // 将连接对象添加到连接列表中
        m_FreeConn++;            // 空闲连接数自增
    }

    reserve = sem(m_FreeConn); // 使用sem构造函数创建一个sem实例给reserve

    m_MaxConn = m_FreeConn; // 将最大连接数设置为当前空闲连接数
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
    *SQL = connPool ->GetConnection(); // 获取连接池中的一个指针，并将该连接的指针赋给SQL指向的指针

    connRAII = *SQL; // 初始化需要管理的数据库连接对象
    poolRAII = connPool;// 初始化需要管理的连接池对象
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(connRAII); // 释放该连接池的数据库连接对象
}
