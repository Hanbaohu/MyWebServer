#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <stdlib.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

//声明一个类模板
template <class T>
class block_queue
{
public:
    //构造函数,默认最大值1000
    block_queue(int max_size = 1000)
    {
        if(max_size <= 0)
        {
            exit(-1); //-1代表异常退出，0代表正常退出
        }

        m_max_size = max_size; //初始化队列的最大容量
        m_array = new T[max_size]; //运算符动态分配大小为 max_size 乘以 sizeof(T)
        m_size = 0; //队列当前大小初始化为0；
        m_front = -1;
        m_back = -1;//队列的 front（队首） 和 back（队尾） 初始化为 -1，表示初始时队列为空

    }

    void clear()
    {
        m_mutex.lock();
        m_size = 0; //队列大小初始化为0，前后指针为-1
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();        
    }

    //析构函数
    ~block_queue()
    {
        m_mutex.lock();
        if(m_array !=nullptr)
            delete [] m_array; //使用 new 操作符动态分配一个数组时，你需要使用 delete [] 来释放这个数组所占用的内存
        
        m_mutex.unlock();
    }

    //判断队列是否满了
    bool full()
    {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {
            m_mutex.unlock()
            return true;
        }
        m_mutex.unlock()
        return false;
    }

    //判断队列是否为空
    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return true;            
        }
        m_mutex.unlock()
        return false;        
    }

    //返回队首元素给value
    bool front(T &value)
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return false;              
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;             
    }

    //返回队尾元素
    bool back(T &value)
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return false;              
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;             
    }

    //获取当前队列大小
    int size()
    {
        int temp = 0;
        m_mutex.lock();
        temp = m_size;

        m_mutex.unlock();
        return temp;
    }    

    //获取队列的最大容量
    int max_size()
    {
        int temp = 0;
        m_mutex.lock();
        temp = m_max_size;

        m_mutex.unlock();
        return temp;
    }

    //往队列添加元素，然后将所有使用队列的线程唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            //唤醒所有等待该条件变量m_cond的线程
            //保证数据及时处理
            //确保所有被阻塞的线程有机会争夺锁和及时处理新添加的元素
            m_cond.broadcast(); 
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size; //当m_back到达m_max_size-1时，会回到0,实现循环队列
        m_array[m_back] == item; //添加新元素

        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();
        while(m_size<=0)
        {
            //等待失败，立刻返回
            //等待成功，继续循环
            if(!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front+1)%m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //pop方法（增加了超时处理）
    bool pop(T &item,int ms_timeout)
    {
        struct timespec t = {0,0};//表示超时时间
        struct timeval now = {0,0};//表示当前时间
        gettimeofday(&now, nullptr); //获取当前的时间，第二个参数代表时区
        m_mutex.lock();
        if(m_size<=0)
        {
            t.tv_sec = now.tv_sec+ms_timeout/1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000; //获取超时的结束时间
            //等待失败或超时！立刻返回
            //等待成功，继续执行
            if(!m_cond.timewait(m_mutex.get(),t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        //再次检查队列是否仍然为空
        //如果此时仍为空，直接解锁互斥量并返回false
        if(m_size<=0)
        {
            m_mutex.unlock();
            return false;            
        }

        m_front = (m_front+1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;
    cond m_cond;

    int m_max_size;
    T *m_array;  //声明一个指针，用于动态分配内存
    int m_size;
    int m_front;
    int m_back;
};

#endif