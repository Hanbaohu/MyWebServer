#include <mysql/mysql.h>
#include "sql_connection_pool.h"

//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
    MYSQL *con =nullptr; // 声明一个MYSQL指针用于存储获取到的连接

    if(connList.size() == 0) // 判断连接池中是否有可用连接
        return nullptr;

    reverse.wait(); //使用信号类，等待可用连接

    lock.lock(); //加锁，确保线程安全

    con = connList.front(); //获取连接池中的第一个连接
    connList.pop_front(); //移除

    --m_FreeConn;//空闲连接数-1
    ++m_CurConn;//当前连接数+1

    lock.unlock();//解锁

    return con; //返回得到的连接地址
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
    if(con == nullptr)
        return false;

    lock.lock();

    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reverse.post();
    return true;
}

//返回当前空闲的连接数
int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for(it = connList.begin();it!=connList.end();it++)
        {
            MYSQL *con = *it; //获取当前迭代器指向的连接
            mysql_close(con); //关闭数据库连接
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

//获取单例对象的实例
connection_pool *connection_pool::GetInstance()
{
    //静态变量是在程序运行期间在静态存储区分配的变量
    //其生命周期从程序启动时直到程序结束
    //定义了一个静态局部变量
    //只会在第一次调用该函数时被初始化，并在整个程序运行期间保留其值
    static connection_pool connPool;
    return &connPool;  //每次返回的都是同一个实例指针
}

void connection_pool::init(string url,string User,
                            string Passward,string DataBaseName,
                            int Port,int MaxConn,int close_log)
{
    //初始化类的成员变量
    m_url = url; 
    m_Port = Port;
    m_User = User;
    m_PassWord = Passward;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;

    //循环创建MaxConn个数据库连接
    for(int i = 0; i<MaxConn;i++)
    {
        MYSQL *con = nullptr;
        con = mysql_init(con); //初始化

        if(con == nullptr)
        {
            
        }
    }

}
