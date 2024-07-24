#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include "log.h"

using namespace std;

Log::Log()
{
    m_count=0; //计数器初始化为0
    m_is_async = false; //默认同步传输
}

Log::~Log()
{
    if(m_fp != nullptr)
    {
        fclose(m_fp); //关闭文件夹
    }
}
// 异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(int max_queue_size = 0, int close_log,
               int log_buf_size = 8192, int split_lines = 5000000,
               const char *file_name)
{
    // 如果大于等于1，表示需要设置为异步方式进行日志记录
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size); // 动态分配
        pthread_t tid;                                         // pthread_t代表POSIX 线程库中表示线程的数据类型
        // 创建线程并异步写日志，flush_log_thread为线程函数
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;             // 设置日志开关标志
    m_log_buf_size = log_buf_size;       // 设置日志缓冲区大小
    m_buf = new char[m_log_buf_size];    // 分配日志缓冲区内存
    memset(m_buf, '\0', m_log_buf_size); // 初始化日志缓冲区
    m_split_lines = split_lines;         // 设置日志分割行数

    time_t t = time(NULL); // 获取当前时间
    // 获取本地时间
    struct tm *sys_tm = localtime(&t); // 将时间转换为本地时间,localtime函数
    struct tm my_tm = *sys_tm;         // 复制本地时间结构体，即将当前时间信息保存在my_tm中

    const char *p = strrchr(file_name, '/'); // 寻找文件名中最后一个斜杠，从后往前搜索
    char log_full_name[256] = {0};           // 声明一个空的char数组,用于存放完整的日记名

    // 如果没有斜杠则表示文件名没有路径，只有一个文件名
    if (p == nullptr)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                 my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1); // 表示只拷贝前p-file_name+1个字符
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s",
                 dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1,
                 my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday; // 记录当天日期

    m_fp = fopen(log_full_name, "a");
    // 如果路径错误则会返回一个空指针
    if (m_fp == nullptr)
    {
        return false; // 初始化失败
    }
    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    // 获取当前时间
    struct timeval now = {0, 0}; // 初始化
    gettimeofday(&now, nullptr);

    // 解析时间
    time_t t = now.tv_sec;             // 获取秒钟
    struct tm *sys_tm = localtime(&t); // 转化为年月日等信息
    struct tm my_tm = *sys_tm;         // 解引用,获得具体的值

    // 定义日志级别对应的标识符
    char s[16] = {0}; // 初始化char数组为0
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 对日志进行写入操作，需要加锁确保线程安全
    m_mutex.lock();
    m_count++; // 日志行数加1

    // 判断是否需要进行日志文件分割，日期不一样进行分割，或者达到最大行数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        char new_log[256] = {0};
        fflush(m_fp); // 刷新流（清空文件缓冲区），将将流m_fp的输出缓冲区中的数据立刻写入到对应的文件中
        fclose(m_fp); // 关闭文件

        // 创建日志文件名后缀，格式为年月日_，例如：2022_03_15_
        char tail[16] = {0};
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name); // 路径+时间+文件名
            m_today = my_tm.tm_mday;                                    // 更新时间
            m_count = 0;                                                // 重置计数器
        }
        else
        {
            // 如果是当天，说明已写入的日志行数达到每个文件最大行数，以序号进行命名
            // 路径+时间+文件名+.num
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }

        m_fp = fopen(new_log, "a"); // 以追加方式打开日志文件，不存在则会自动创建
    }

    m_mutex.unlock(); // 解锁

    // 格式化日志内容
    va_list valist;           // 存储可变参数列表
    va_start(valist, format); // 初始化可变参数列表，定位到最后一个显式参数的后面，即可变参数列表的起始位置

    string log_str;
    m_mutex.lock(); // 上锁

    // 写入的具体时间内容格式,n为实际写入的字符数
    // 即使超过47个字符，n也为47，发生错误n为负数
    // s为信息的声明，如debug、info、warn、error
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                     my_tm.tm_year + 1990, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    // 将后面的可变参数变量以format的格式写入m_buf + n开始的地址中，
    // m_log_buf_size - n -1为最大的写入的字符数，而m是输入字符串的长度，而不是实际写入的长度
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valist);
    m_buf[n + m] = '\n';     // 加入换行符
    m_buf[n + m + 1] = '\0'; // 加入字符串的结束符
    log_str = m_buf;         // 将格式化后的带有换行符的完整日志内容从m_buf复制到log_str字符串中

    m_mutex.unlock(); // 解锁

    // 将日志写入队列或直接写入日志文件
    // 是异步&&队列没满
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str); // 写入堵塞队列
    }
    else
    {
        m_mutex.lock();
        //将log_str这个C++的字符串以C风格字符串的格式写入到文件指针m_fp所指向的文件中
        //log_str.c_str()返回一个指向以null结尾的字符数组的指针
        //以便将C++的字符串转换为C风格字符串
        fputs(log_str.c_str(),m_fp);
        m_mutex.unlock();
    }

    va_end(valist); //清理 va_list 变量
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}
