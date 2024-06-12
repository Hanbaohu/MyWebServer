#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <semaphore.h>
#include <pthread.h>

//这段代码定义了一个简单的线程同步库
//操作系统知识，互斥锁（locker）、信号量（sem），以及条件变量（cond）

class sem
{
public:
    sem()
    {
        if(sem_init(&m_sem,0,0) != 0)   //用于初始化信号量，成功返回0
        {
            throw std::exception();
        }
    }

    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)  //num为指定信号量的初始值
        {
            throw std::exception();
        }
    }

    ~sem()
    {
        sem_destroy(&m_sem);   //用于销毁信号量
    }

    bool wait()
    {
        //>0时,以原子操作的方式将信号量的值减1
        //=0时,sem_wait阻塞
        //调用成功时返回0，失败返回-1
        return sem_wait(&m_sem) == 0;   
    }
    bool post()
    {
        //以原子操作方式将信号量加一
        return sem_post(&m_sem) == 0; 
    }
private:
    sem_t m_sem;  //定义一个信号量
};

class locker
{
public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex,NULL) != 0) //NULL代表使用默认属性
        {
            throw std::exception();
        }
    }

    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock()
    {
        //以原子操作方式给互斥锁加锁
        //锁定互斥锁，如果尝试锁定已经被上锁的互斥锁则阻塞至可用为止
        return pthread_mutex_lock(&m_mutex) == 0; 
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0; //释放互斥锁
    }

    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex; //声明一个自定义的数据类型的变量
};

class cond
{
private:
    /* data */
public:
    cond(/* args */);
    ~cond();
};

cond::cond(/* args */)
{
}

cond::~cond()
{
}


#endif
