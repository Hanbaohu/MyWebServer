#include <mysql/mysql.h>
#include "sql_connection_pool.h"

MYSQL *connection_pool::GetConnection()
{
    MYSQL *con =nullptr;

    if(connList.size())
        return nullptr;

}