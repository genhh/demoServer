#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h> //book code bug
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "locker.h"

class http_conn{
public:
    static const int FILENAME_LEN = 200;//max len of file
    static const int READ_BUFFER_SIZE = 2048;//read buff size
    static const int WRITE_BUFFER_SIZE = 1024;//write buff size
    
    enum METHOD{GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};//http request method. only get
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0,
                     CHECK_STATE_HEADER,
                     CHECK_STATE_CONTENT};// main state
    enum HTTP_CODE{NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};// the value of http response
    enum LINE_STATUS{LINE_OK = 0, LINE_BAD, LINE_OPEN};//the state of reading line.

    static int m_epollfd;// contain all socket
    static int m_user_count;// the count of users


    http_conn(){}
    ~http_conn(){}
    void init(int sockfd, const sockaddr_in& addr);// init new connection
    void close_conn(bool real_close = true);//close connection
    void process();// process client request
    bool read();//nonblock read
    bool write();//nonblock write

private:
    int m_sockfd;// http's socket
    sockaddr_in m_address;//clients socket's address
    char m_read_buf[READ_BUFFER_SIZE];//read buf
    int m_read_idx;// the id of read buf data
    int m_check_idx;// the position of reading in read buf
    int m_start_line;// start line of process line
    char m_write_buf[WRITE_BUFFER_SIZE];//write buf
    int m_write_idx;//data ready for send in write buf 

    CHECK_STATE m_check_state; // main state machine cur state
    METHOD m_method;// request method

    char m_real_file[FILENAME_LEN];//file paths
    char* m_url;// file name
    char* m_version;// http version
    char* m_host;//name of host
    int m_content_length;// http message len
    bool m_linger;//if keep connection

    char* m_file_address;//start pos in memory
    struct stat m_file_stat;//file state
    struct iovec m_iv[2];
    int m_iv_count;// a number of memory block

    int bytes_to_send;
    int bytes_have_send;

    void init();//init conn
    HTTP_CODE process_read();//process http request
    bool process_write(HTTP_CODE ret);//fill http response

    //couple of func to process http request by func "process_read"
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line(){return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    //couple of func to fill http response by func "process_write"
    void unmap();
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
};

#endif