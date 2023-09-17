#ifndef REACTOR_H
#define REACTOR_H
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "httpdeal.h"
#include <sys/uio.h>
#include "log/log.h"

#define BUF_SIZE 1024




//负责事件的处理
class reactor
{

public:
    

private:
    static int m_user_count;//当前用户数量
    //static pthread_mutex_t mutex;//保护m_user_count


    /* data */
    int m_fd;
    int m_ept;
    int m_events;
    int re_events;//触发的事件
    int writebuf_len;//要发送的数据长度
    int bytes_have_send;//已经发送的数据长度

    int readbuf_len;//读到的数据长度
    char* writebuf;
    char* readbuf;
    httpdeal* httpd;
    //文件映射的地址
    char* filemmap;
    //文件映射的长度
    int filelen;
    //是否保持lianj
    bool keepalive;

public:
    reactor();
    ~reactor();


    //修改事件
    void modevent(int events);
    //初始化连接并将事件加入事件树
    void init(int ept, int fd, int events);
    //销毁连接并删除事件
    void destory();
    //设置监听到的事件
    void setevents(int events);
    //返回反应堆用户数是否超过MAXSIZE
    static bool isfull(int MAXSIZE);

    //线程池执行的函数
    void process();

    //读写事件处理函数
    bool dealread();
    bool dealwrite();


};





#endif