#include <string>

#include "http_coon.h"

using namespace std;

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";

// 给静态成员变量初始化
// -1 作为初始值是一个惯例
// 可以明确地表示这个文件描述符当前还没有被分配或初始化。
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

// 定义全局变量
map<string, string> users;
locker m_lock;

// 初始化连接
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root,
                     int TRIGMode, int close_log, string user,
                     string passwd, string sqlname)
{
    // 将参数赋值给成员变量
    m_sockfd = sockfd; // 给套结文字描述符赋值
    m_address = addr;  // 给IPv4地址赋值,客户端地址

    // 向 epoll 事件表注册 sockfd 上的可读事件以及开启oneshot模式
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;

    // 当浏览器出现连接重置时
    // 可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;         // 设置站点根目录
    m_TRIGMode = TRIGMode;   // 设置触发模式
    m_close_log = close_log; // 设置日志的关闭状态

    // 将数据库相关参数转换为c风格的字符串
    strcpy(sql_user, user.c_str()); // 将 user 转换为 C 风格字符串并复制给 sql_user
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    // 进一步初始化
    init();
}

// 初始化新接受的myqsl连接
// check_state默认为分析请求行状态
void http_conn::init()
{
    // 初始化变量值
    mysql = nullptr;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_read_buf, '\0', FILENAME_LEN);
}

// 将fd添加到epollfd中进行监控，监控事件为读事件，触发方式等可以自定义
// 将epoll内核事件表注册为读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    // events 成员用于指定需要监听的事件类型
    epoll_event event;  // 创建一个epoll_event结构体实例，用于设置要注册的事件的属性
    event.data.fd = fd; // 将要注册的文件描述符fd赋值给event实例

    // 为1设为边缘触发
    if (1 == TRIGMode)
        // EPOLLRDHUP 标志来判断是否发生了远端关闭事件
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // IN+ET+RDHUP
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    // 当某个文件描述符上的事件被触发并处理完后
    // 如果是 EPOLLONESHOT 模式，该文件描述符将不再被再次触发
    // 必须重新将它添加到 epoll 实例中
    if (one_shot)
        event.events |= EPOLLONESHOT;

    // EPOLL_CTL_ADD 表示往 epoll 实例中添加一个新的文件描述符及其监听的事件。
    // 将事件添加到epoll内核事件表中
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 将文件描述符设置为非阻塞模式
    setnonblocking(fd);
}

// 对文件描述符设置为非阻塞模式
int setnonblocking(int fd)
{
    // 获取文件描述符的当前状态
    int old_option = fcntl(fd, F_GETFL);
    // 将文件描述符的状态设置为非阻塞模式
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option); // 写入
    // 返回设置前的文件描述符状态
    return old_option;
}

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close%d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0); // 从 epoll 实例中移除指定的文件描述符
    close(fd);                                // 关闭fd文件描述符
}

void http_conn::process()
{
    // 处理读操作，返回读操作的结果
    HTTP_CODE read_ret = process_read();

    if (read_ret == NO_REQUEST)
    {
        // 如果没有请求需求处理
        // 则修改文件描述符的事件，等待下一次事件发生后继续处理
        // EPOLLIN 用来监听文件描述符上的读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    // 处理写操作，传入读操作的结果，并返回写操作的结果
    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        close_conn(); // 写入响应失败，关闭当前的连接
    }
    // 调用 modfd() 函数修改事件的注册
    // 将监听的事件类型改为 EPOLLOUT（表示可写事件），以准备将响应发送给客户端
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK; // 定义一个表示行状态的变量，初始值为LINE_OK
    HTTP_CODE ret = NO_REQUEST;        // 定义HTTP处理结果的状态量，初始值为NO_REQUEST
    char *text = 0;                    // 初始化一个字符指针为0

    // 进入循环，直到状态为CHECK_STATE_CONTENT 且 行状态为LINE_OK，或者解析行的结果仍为LINE_OK
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();            // 获取一行HTTP报文数据
        m_start_line = m_checked_idx; // 设置起始行位置为当前已处理位置
        LOG_INFO("%s", text);         // 记录日志，输出获取的行数据
        switch (m_check_state)
        {
        // 请求行状态
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text); // 解析请求行
            if (ret == BAD_REQUEST)
                return BAD_REQUEST; // 解析失败
            break;
        }
        // 请求头状态
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text); // 解析请求头
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;     // 解析失败
            else if (ret = GET_REQUEST) // 解析成功
            {
                return do_request(); // 处理GET请求
            }
            break;
        }
        // 处理解析内容状态
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text); // 解析内容
            if (ret == GET_REQUEST)    // 若内容解析完成，且为GET请求
                return do_request();   // 处理GET请求
            line_status = LINE_OPEN;   // 行状态置为LINE_OPEN，表示尚未结束
            break;
        }
        default:
            return INTERNAL_ERROR; // 返回内部错误信息
        }
    }
    return NO_REQUEST;
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) // 从已检查的位置开始循环直到已读取数据的总长度
    {
        temp = m_read_buf[m_checked_idx]; // 获取当前位置的字符

        // 如果是回车符
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx) // 如果下一个位置是已读取数据的末尾
                return LINE_OPEN;

            else if (m_read_buf[m_checked_idx + 1 == '\n']) // 如果下一个位置是换行符,请求行读取结束
            {
                m_read_buf[m_checked_idx++] = '\0'; // 将回车符替换为字符串结束符
                m_read_buf[m_checked_idx++] = '\0'; // 将换行符替换为字符串结束符
                return LINE_OK;                     // 解析出一行数据，返回LINE_OK
            }
            return LINE_BAD; // 其他情况均视为格式错误，返回LINE_BAD
        }

        else if (temp == '\n')
        {
            // 如果前一个位置是回车符
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0'; // 将回车符替换为字符串结束符
                m_read_buf[m_checked_idx++] = '\0';   // 将换行符替换为字符串结束符
                return LINE_OK;                       // 解析出一行数据，返回LINE_OK
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; // 未找到一行完整的数据，需要继续接收更多数据
}

// 解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 从text中找到第一个空格或制表符的位置并将指针赋值给m_url
    m_url = strpbrk(text, " \t");
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    // 在找到的空格或制表符位置处插入空字符，同时将m_url指针后移一位
    *m_url++ = '\0';
    char *method = text;
    // 比较字符串，判断请求方法是GET还是POST
    // strcasecmp方法只比对字符串的开头内容是否相等
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    // 跳过空格和制表符
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    // 如果URL以http://或https://开头，则跳过这部分内容，移动m_url指针到第一个'/'字符的位置
    if (strncasecmp(m_version, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (strncasecmp(m_version, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    // 检查URL是否为空或不以'/'开头,如果是则返回BAD_REQUEST
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    // 如果URL只有一个'/'，则追加"judge.html"
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 检查文本是否为空
    if (text[0] == '\0')
    {
        // 如果内容长度不为0，则设置状态为CHECK_STATE_CONTENT，返回NO_REQUEST
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 如果内容长度为0，则返回GET_REQUEST
        return GET_REQUEST;
    }
    // 检查connection字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        // 跳过字段名及空格制表符等空白字符
        text += 11;
        text += strspn(text, " \t");
        // 如果字段值是 keep-alive，则设置 linger 为 true
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; // 启用 TCP 连接的优雅关闭（即延迟关闭）
        }
    }
    // 检查 Content-length 字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        // 获取内容长度，并使用 atol 转换为长整型数
        m_content_length = atol(text); // 将字符串转换为长整型数值
    }
    // 检查Host字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        // 将主机信息保存到 m_host 中
        m_host = text;
    }
    // 如果是其他未知的字段，则记录日志
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root); // 复制C字符串
    int len = strlen(doc_root);    // 计算C字符串长度，不包括结尾的null字符

    const char *p = strrchr(m_url, '/'); // 反向查找字符，即在字符串中查找特定字符最后一次出现的位置，返回指针

    // 处理cgi
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        // 为m_url_real分配内存并复制字符串
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);

        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        // 释放m_url_real占用的内存
        free(m_url_real);

        // 将用户名和密码提取出来
        // m_string="user=123&passwd=123"
        char name[100], password[100];
        int i;
        // 提取用户名
        for (i = 5; m_string[i] != '&'; i++)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0'; // 加入结束符

        // 提取密码
        int j = 0;
        // 也许是 i + 8 ？
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
        {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            // 如果是注册，先检测数据库中是否有重名的
            // 没有重名的，进行增加数据

            // 构建insert sql语句, SQL INSERT INTO 语句用于向表中插入新记录
            // 语法：INSERT INTO table_name VALUES (value1,value2,value3,...);
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "','");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 如果users中没有重名的条目
            //  则加锁，执行SQL语句，更新users数据，然后解锁
            if (users.find(name) == users.end())
            {
                m_lock.lock();

                // mysql_query 函数用于向 MySQL 数据库发送 SQL 查询
                // mysql 是一个 MYSQL* 类型的指针，指向一个已连接的 MySQL 数据库
                // 0：表示查询成功,非0值：表示查询失败
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));

                m_lock.unlock();

                // 根据执行结果更新m_url
                // 为0，则登录成功
                if (!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        // 如果是登录，直接判断
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 注册请求
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 登录请求
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // picture
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // video
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // fans
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    // stat 函数返回值： 0 表示成功 -1 表示失败
    // m_real_file想要检查的文件或目录的路径
    // m_file_stat用来保存文件的信息
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_REQUEST;

    // 提取出其他用户读取权限位
    // st_mode 包含文件的模式信息（即文件的类型和权限）
    // S_IROTH 是一个宏，表示文件的读取权限位掩码，用于标识“其他用户”的读取权限
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 检查是否为目录,如果是目录返回一个非零值，如果不是目录返回 0
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    // 打开文件，并将文件映射到内存中
    // O_RDONLY 表示以只读模式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // mmap 是一个系统调用函数，用来创建新的内存映射或者修改现有内存映射
    // 第一个参数 0 是指定映射的起始地址。通常设置为0，表示由操作系统自动选择合适的地址。
    // 第二个参数是要映射的文件的大小
    // 第三个参数是指定内存映射的保护方式。PROT_READ 表示映射的区域可以被读取。
    // 第四个参数是指定内存映射的类型。
    // 第五个参数是文件描述符,表示需要映射的文件
    // 第六个参数表示文件在内存中的偏移量，通常设置为0，表示从文件的开头开始映射
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 如果已经读取的数据长度大于等于（消息的长度+检查过的长度）
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        // 在消息体末尾添加字符串结束标志
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;    // 将消息体保存到m_string中
        return GET_REQUEST; // 返回GET请求，表示请求已经完整读入
    }
    // 若请求还未完全接收
    return NO_REQUEST;
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd; // 将文件描述符fd分配给 epoll_event 结构体event中的数据字段。

    if (1 == TRIGMode)
        // 如果 TRIGMode 为 1
        // 则在事件中设置指定的事件类型 'ev' 与 EPOLLET（边缘触发）
        // EPOLLONESHOT（一次性触发）、EPOLLRDHUP（对端关闭连接）标志
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        // 如果 TRIGMode 不为 1，则不选择EPOLLET（边缘触发）
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    // 使用新的事件设置在 'event' 结构体中修改 epoll 实例中给定文件描述符 'fd' 的设置
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    // 服务器内部错误 
    case INTERNAL_ERROR:
    {
        add_status_line(500,error_500_title); // 设置状态行，返回 500 错误码和相应的标题
        add_headers(strlen(error_500_form)); // 添加 HTTP 头部，设置内容长度
        if(!add_content(error_500_form)) // 添加错误内容，如果失败，返回 false
            return false;
        break;
    }
    // 请求不合法
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title); // 行
        add_headers(strlen(error_404_form)); // 头
        if(!add_content(error_404_form))
            return false;
        break;
    }
    // 请求被禁止
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403,error_403_title);
        add_headers(strlen(error_403_form));
        if(!add_content(error_403_form))
            return false;
        break;
    }
    // 文件请求
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if(m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>"; // 空页面
            add_headers(strlen(ok_string));
            if(!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    // 当没有文件内容时，准备发送 m_write_buf 中的数据
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status,title);
}

bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_black_line();
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s",content);
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Type:%s\r\n",content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection%s\r\n",(m_linger == true) ? "keep_alive":"close");
}

bool http_conn::add_black_line()
{
    return add_response("%s","\r\n");
}

// 接受一个格式字符串 format，后面可以跟多个可变参数（使用 ... 表示）
bool http_conn::add_response(const char *format, ...)
{
    // 如果当前的写入索引大于或等于预设的写缓冲区大小
    // 返回 false，表示无法再继续添加响应内容
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list; // va_list 用于存储可变参数列表

    va_start(arg_list, format); // 初始化arg_list
    // 使用 vsnprintf 函数将格式化后的字符串写入 m_write_buf 的指定位置
    // （m_write_idx 到 m_write_buf 的末尾）
    int len = vsnprintf(m_write_buf + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);

    // 检查写入长度，如果返回的长度 len 大于或等于剩余的缓冲区大小
    // 结束可变参数的处理（调用 va_end）。
    // 返回 false，表示写入失败。
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    // 更新写入索引,并结束可变参数处理
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("resquest:%s",m_write_buf);
    return true;
}
