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
private:
    Log();
    
    //void *是为了与POSIX线程（pthread）标准兼容
    //任何作为线程入口的函数都必须返回一个 void* 类型指针
    void *async_write_log()
    {
        string single_log; //用来存储从日志队列中取出的单个日志数据
        while (m_log_queue->pop(single_log)) //一直循环，直到阻塞队列为空
        {
            m_mutex.lock();

        }
        
    }

private:
    block_queue<string> *m_log_queue; //声明一个string类型的阻塞队列
    locker m_mutex;
};


#endif
