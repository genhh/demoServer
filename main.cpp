#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "threadpool.h"
#include "http_conn.h"
#include "locker.h"

//proactor mode
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);

//add signal catch
void addsig(int sig, void(handler)(int), bool restart = true){// signal process
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));//clear data ,set 0
    sa.sa_handler = handler;
    if(restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}
void show_error(int , const char* );

int main(int argc, char* argv[]){
    if(argc <= 2){
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi(argv[2]);

    addsig(SIGPIPE, SIG_IGN);

    threadpool<http_conn>* pool = nullptr;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){//catch all error
        return 1;
    }

    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1,0};//?when set {1,0}, equal i/O reuse
    int reuse = 1;
    //setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));//??
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));//core improve speed

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));// why bzero, other func not valid?
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);//?
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    //create epoll arrays
    epoll_event events[MAX_EVENT_NUMBER];//?func?
    int epollfd = epoll_create(5);//5:table size
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);//add listen fd into epoll
    http_conn::m_epollfd = epollfd;

    while(true){
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((number < 0) && (errno != EINTR)){
            printf("epoll failure\n");
            break;
        }

        for(int i=0; i<number; i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){//client prepare to connect
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);//every time has different connect fd

                if(connfd < 0){
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if(http_conn::m_user_count >= MAX_FD){
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);// new client init

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){//error happen, close connection
                users[sockfd].close_conn();

            }else if(events[i].events & EPOLLIN){//read ?request?
                if(users[sockfd].read()){//read all data
                    pool->append(users + sockfd);// point to sockfd in users which need to process by woker
                }else{
                    users[sockfd].close_conn();
                }

            }else if(events[i].events & EPOLLOUT){//write ?response? 
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }

            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}


void show_error(int connfd, const char* info){
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}