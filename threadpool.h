#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include"locker.h"

template<typename T>
class threadpool{
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* requeset);//add tasks

private:
    int m_thread_number;// the number of threads
    int m_max_requests;// max request num in queue
    pthread_t* m_threads;// thread array. size: m_thread_number
    std::list<T*> m_workqueue; //request queue
    locker m_queuelocker;// mutex used in request queue
    sem m_queuestat; // if task need exec
    bool m_stop; // if stop thread

    static void* worker(void* arg);// from queue take tasks and exec it
    void run();

};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests): 
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(nullptr)
{
    if((thread_number <= 0) || (max_requests <= 0)){
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];//create pool
    if(!m_threads) throw std::exception();

    for(int i=0;i<thread_number;++i){
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads + i, nullptr, worker, this) != 0){//create thread
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){// detach from main thread, make alone,
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();// shared var, need lock
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//?why post
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        request->process();// virtual func?
    }
}

#endif