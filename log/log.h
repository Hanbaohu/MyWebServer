#ifndef LOG_H
#define LOG_H

#include <string>
#include "block_queue.h"
#include "../lock/locker.h"

using namespace std;

class Log
{
public:
    //单例模式；使用局部静态变量
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    } 
    //void *为无类型指针,线程函数要求的返回类型,隐式的返回nullptr
    //void *args 这个参数是为了符合线程库pthread_create的要求，这个参数的存在确保了所有线程的创建和执行接口的一致性 
    //线程库通常要求传递一个函数指针作为线程的入口函数
    //这个函数的作用就是为了符合线程库所要求的函数签名，本身没有意义，间接调用async_write_log()函数
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log(); //以异步的方式写入log
    }
    bool init ( int max_queue_size = 0, int close_log, 
                int log_buf_size = 8192, int split_lines = 5000000,
                const char *file_name );
    
    void write_log(int level, const char *format, ...);

    void flush(void); //显式的表明不接受任何参数，c中有区别，cpp中等价于()
private:
    Log();//构造函数
    //如果一个类有可能成为基类，最好为其提供一个虚析构函数。
    //这确保了通过基类指针删除派生类对象时，派生类的析构函数也会被调用。
    virtual ~Log();

    //void *是为了与POSIX线程（pthread）标准兼容
    //任何作为线程入口的函数都必须返回一个 void* 类型指针
    void *async_write_log()
    {
        string single_log; //用来存储从日志队列中取出的单个日志数据
        while (m_log_queue->pop(single_log)) //一直循环，直到阻塞队列为空
        {
            m_mutex.lock();
            //s.c_str()函数的作用是将c++中的字符串转化为c风格的字符串
            //返回一个指向存储字符串内容的字符数组的指针，并且保证以 null 结尾
            //fputs 是一个标准 C 语言中的函数，用于将一个以 null 结尾的字符串写入到指定的文件流中
            fputs(single_log.c_str(),m_fp);
            m_mutex.unlock();
        }
        
    }

private:
    block_queue<string> *m_log_queue; //声明一个string类型的阻塞队列
    locker m_mutex;
    FILE *m_fp;
    bool m_is_async; //判断是否是异步写入
    int m_close_log; //关闭日志
    int m_log_buf_size; //日志缓冲区大小
    char *m_buf; //缓存区地址指针，用于动态分配
    int m_split_lines; //日志最大行数
    char log_name[128]; //用一个128char数组来保存log文件名
    char dir_name[128]; //路径名
    int m_today; //因为按天分类,记录当前时间是哪一天
    long long m_count; //日志行数记录
};

#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
