#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

//posix signal
class sem{
public:
    sem(){
        if(sem_init(&m_sem, 0, 0) != 0){
            throw std::exception();
        }
    }

    ~sem(){
        sem_destroy(&m_sem);
    }

    bool wait(){//wait signal
        return sem_wait(&m_sem) == 0;
    }

    bool post(){//add signal
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

//mutex locker
class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex, nullptr) != 0){// if success return 0
            throw std::exception();
        }
    }

    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

private:
    pthread_mutex_t m_mutex;
};

//conditon var
class cond{//when shared var become a special val, wake threads which need this data
public:
    cond(){
        if(pthread_mutex_init(&m_mutex, nullptr) != 0){
            throw std::exception();
        }
        if(pthread_cond_init(&m_cond, nullptr) != 0){
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    //destroy cond var
    ~cond(){
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    //wait cond var
    bool wait(){
        int ret = 0;
        pthread_mutex_lock(&m_mutex);//before cond_wait, must mutex lock
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    // wake thread which statisfy cond var
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }

private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif