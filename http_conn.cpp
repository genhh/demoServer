#include "http_conn.h"
// some info of http response
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file form this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "Threre was an unusual problem serving the requested file.\n";
const char* doc_root = "/home/ubuntu/demoServer/resources";

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

int setnonblocking(int fd){//set fd non block
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);//file control
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;// epoll_LT
    //event.events |= EPOLLET;
    if(one_shot){
        event.events |= EPOLLONESHOT;//one socket only process by one spec thread
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev){//modfiy fd
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::close_conn(bool real_close){
    if(real_close && (m_sockfd != -1)){
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in& addr){
    m_sockfd = sockfd;
    m_address = addr;
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    
    init();
}

void http_conn::init(){
    //response
    bytes_have_send = 0;
    bytes_to_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;// parse request first line
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);//different with bzero?
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_check_idx < m_read_idx; ++m_check_idx){
        temp = m_read_buf[m_check_idx];
        if(temp == '\r'){
            if((m_check_idx+1) == m_read_idx){//range of m_read_idx
                return LINE_OPEN;
            }else if(m_read_buf[m_check_idx+1] == '\n'){
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }else if(temp == '\n'){
            if((m_check_idx>1) && (m_read_buf[m_check_idx-1] == '\r')){
                m_read_buf[m_check_idx-1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read(){// non block read, read all data until close connect
    if(m_read_idx >= READ_BUFFER_SIZE)return false;
    int bytes_read = 0;
    while(true){
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);//func?
        if(bytes_read == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){//no data, end this while
                break;
            }
            return false;
        }else if(bytes_read == 0){//client close the connection
            return false;
        }
        m_read_idx += bytes_read;
    }
    //printf("read buf: %s\n",m_read_buf);
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    //printf("text:%s\n",text);
    m_url = strpbrk(text, " ");//error : before:"\t" now:" "
    //printf("m_url:%s\n",m_url);
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    //printf("test 1\n");
    char* method = text;
    if(strcasecmp(method, "GET") == 0){
        m_method = GET;
    }else{
        return BAD_REQUEST;
    }
    //printf("test 2\n");
    //m_url += strspn(m_url, "\t");
    m_version = strpbrk(m_url, " ");
    if(!m_version){
        return BAD_REQUEST;
    }
    //printf("test 3\n");
    *m_version++ = '\0';
    //m_version += strspn(m_version, "\t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0){
        return BAD_REQUEST;
    }
    //printf("test 4\n");
    if(strncasecmp(m_url, "http://", 7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    //printf("test 5\n");
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }
    //printf("test 6\n");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }else if(strncasecmp(text, "Connection:", 11) == 0){
        text += 11;//? is true
        text += strspn(text, "\t");
        if(strcasecmp(text, "keep-alive") == 0){
            m_linger = true;
        }
    }else if(strncasecmp(text, "Content-Length:", 15) == 0){
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    }else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    }else{
        printf("oop! unkonw header %s\n", text);
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char* text){
    if(m_read_idx >= (m_content_length + m_check_idx)){
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read(){// main state machine
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK)){
        //get a full line data
        text = get_line();
        m_start_line = m_check_idx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                //printf("request line ret:%d\n",ret);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
            break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                //printf("head ret:%d\n",ret);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }else if(ret == GET_REQUEST){
                    return do_request();//parse spec content
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                //printf("content ret:%d\n",ret);
                if(ret == GET_REQUEST){
                    return do_request();                                                                                                             
                }
                // if fail                                                                                                                    
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);                                                                                                                           
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    //printf("m_real_file %s\n",m_url);
    if(stat(m_real_file, &m_file_stat) < 0){
        return NO_RESOURCE;
    } 

    //printf("dead 2\n");
    if(!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    //printf("dead 3\n");
    if(S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;
    //printf("dead 4\n");
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    close(fd);
    return FILE_REQUEST;
}

void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write(){
    int temp = 0;

    if(bytes_to_send == 0){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }    

    while(1){
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1){
            if(errno == EAGAIN){
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;

        //add write buf
        if(bytes_have_send >= m_iv[0].iov_len){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if(bytes_to_send <= 0){
            unmap();
            if(m_linger){
               init();
               modfd(m_epollfd, m_sockfd, EPOLLIN);
               return true; 
            }else{
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char* format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE -1 - m_write_idx, format, arg_list);//?

    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))return false;

    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title){
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type(){
    return add_response("Content-type: %s\r\n", "text/html");
}

bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line(){
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content){
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret){
    
    switch(ret){
        case INTERNAL_ERROR:
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            
            bytes_to_send = m_write_idx + m_file_stat.st_size;
        
            return true;
        }
        default:
            return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//******* core func ****
void http_conn::process(){
    HTTP_CODE read_ret = process_read();//parse http request
    if(read_ret == NO_REQUEST){
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    //generate response
    bool write_ret = process_write(read_ret);
    if(!write_ret){
        close_conn();
    }    
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
