#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <mysql/mysql.h>
#include <list>


using namespace std;

class connection_pool
{
private:
    list<MYSQL *> connList;

public:
    MYSQL *GetConnection();             //获取数据库连接
    
};


#endif
