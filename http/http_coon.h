#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include<unistd.h>
#include<map>

#include "../CGlmysql/sql_connection_pool.h"
#include "../log/log.h"
// 定义http连接类
class http_conn
{
    // 声明类成员常量
public:
    // static定义的变量表示这些变量是与类本身相关联的
    // 而不是与类的各个对象相关联
    // 即只会定义一次，其他实例共享这个变量
    // 通过类名访问
    static const int FILENAME_LEN = 200;       // 文件名的最大长度
    static const int READ_BUFFER_SIZE = 2048;  // 读取缓存区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写入缓存区大小

    // 定义枚举类型的成员，为一组连续的整数常量，定义之后不可修改
    // 默认从0开始（或者显示定义开始的值），后续的值依次递增
    // 也可以显示定义一组不连续的整数常量 const int
    // 定义http请求类型
    enum METHOD
    {
        GET = 0, // 请求指定的页面信息，并返回实体主体。--查
        POST,    // 向指定资源提交数据进行处理请求，数据内容放在请求体中。--增
        HEAD,    // 类似于 GET 请求，只不过返回的响应中没有具体的内容，用于获取报头。
        PUT,     // 从客户端向服务器传送的数据取代指定的文档的内容。--改
        DELETE,  // 请求服务器删除指定的页面。--删
        TRACE,   // 回显服务器收到的请求，主要用于测试或诊断。
        OPTIONS, // 返回服务器支持的 HTTP 请求方法。
        CONNECT, // 要求在与代理服务器通信时建立隧道连接。
        PATCH    // 与put类似，发送一个修改数据的请求，put更新数据时是全部更新，patch只进行部分更新
    };

    // 主状态机处理HTTP请求的不同部分，即请求行、请求头和请求内容
    // 状态机是指在 HTTP 请求解析过程中用于管理和解析请求
    // 主状态机的三个状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 检测请求行
        CHECK_STATE_HEADER,          // 检查头信息
        CHECK_STATE_CONTENT          // 检查内容
    };

    // 服务器处理http请求的结果(代号)
    enum HTTP_CODE
    {
        NO_REQUEST,        // 请求不完整，需继续读取客户数据 | 返回无请求
        GET_REQUEST,       // 获得了一个完整的客户请求
        BAD_REQUEST,       // 客户请求有语法错误
        NO_RESOURCE,       // 请求资源不存在
        FORBIDDEN_REQUEST, // 客户对资源没有足够的访问权限
        FILE_REQUEST,      // 文件请求
        INTERNAL_ERROR,    // 服务器内部错误
        CLOSED_CONNECTION  // 客户端已经关闭连接
    };

    // 从状态机主要用于逐行读取数据
    // 从状态机的三种可能状态（行的读取状态）
    enum LINE_STATUS
    {
        LINE_OK = 0, // 读取到完整行
        LINE_BAD,    // 行出错
        LINE_OPEN    // 行数据尚不完整（还需要继续读取更多数据）
    };

    // 声明构造函数和析构函数
public:
    // 直接完整定义*空的*构造函数和析构函数，所以没有;
    // 没有具体初始化或清理操作
    http_conn() {}
    ~http_conn() {}

    // 声明公共成员函数
public:
    void init(int sockfd, const sockaddr_in &addr, char *root,
              int TRIGMode, int close_log, string user,
              string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();

private:
    void init();
    HTTP_CODE process_read();
    LINE_STATUS parse_line();
    char *get_line(){return m_read_buf + m_start_line;};
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    bool process_write(HTTP_CODE ret);
    bool add_status_line(int status,const char *title);
    bool add_response(const char *format, ...);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_black_line();
    bool add_content(const char *content);

    // 声明私有变量
private:
    // socket file descriptor
    // 套接字文件描述符用于标识和管理一个特定的网络连接。
    int m_sockfd;
    // sockaddr_in表示 IPv4 地址和端口号的结构体类型
    sockaddr_in m_address;
    int m_TRIGMode;
    char *doc_root;
    char m_read_buf[READ_BUFFER_SIZE];
    char m_write_buf[WRITE_BUFFER_SIZE];
    char m_real_file[FILENAME_LEN];
    int m_close_log;
    CHECK_STATE m_check_state;
    bool m_linger; // 是否启用 TCP 连接的优雅关闭（即延迟关闭）

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
    int bytes_to_send;   // 要发送的字节数
    int bytes_have_send; // 已经发送的字节数
    METHOD m_method;
    char *m_url;
    char *m_version; // 表示http的版本号
    char *m_host;    // 主机名
    long m_content_length;
    int m_start_line;
    long m_checked_idx;
    long m_read_idx;
    int m_write_idx;
    int cgi; // 是否启用的POST，通用网关接口（CGI，Common Gateway Interface）
    char *m_string; //存储请求头数据
    struct stat m_file_stat; // struct stat 是 C/C++ 中用于存储文件状态信息的一个数据结构，通常在 POSIX（如 UNIX/Linux）系统中使用
    struct iovec m_iv[2]; // struct iovec 是在 C/C++ 中用于描述一个内存缓冲区的结构体，通常用于实现高效的读写操作
    char *m_file_address;
    int m_iv_count;

public:
    // 声明静态成员变量，在类中只能声明，不能定义具体值
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;
    int timer_flag; // 定时器状态标志
    int improv;
};

#endif