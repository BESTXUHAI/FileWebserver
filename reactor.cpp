#include "reactor.h"

int reactor::m_user_count = 0;
//初始化静态锁
//pthread_mutex_t reactor::mutex = PTHREAD_MUTEX_INITIALIZER;



void reactor::modevent(int events)
{
    epoll_event ev;
    ev.data.fd = m_fd;
    ev.events = events | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    m_events = ev.events;
    epoll_ctl(m_ept, EPOLL_CTL_MOD, m_fd, &ev);
}


reactor::reactor()
{
    m_fd = -1;
    m_ept = -1;
    m_events = 0;
    re_events = 0;
    writebuf_len = 0;
    readbuf_len = 0;
    bytes_have_send = 0;
    filemmap = NULL;
    filelen = 0;
    keepalive = false;

    writebuf = new char[BUF_SIZE];
    readbuf = new char[BUF_SIZE];
    httpd = new httpdeal();
    
}

reactor::~reactor()
{
    delete [] writebuf;
    delete [] readbuf;
    delete httpd;
}


void reactor::init(int ept, int fd, int events)
{
    //已经被初始化
    if (m_fd != -1)
    {
        
        LOG_WARN("Already initialized");
        close(fd);
        return;
    }
    
    m_fd = fd;
    m_ept = ept;
    m_events = events;
    writebuf_len = 0;
    readbuf_len = 0;
    bytes_have_send = 0;
    filemmap = NULL;
    filelen = 0;
    keepalive = false;
    httpd->connectinit();
    
    //设置文件描述符为非阻塞
    int flag = fcntl(fd, F_GETFL);
    flag = flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);

    // 端口复用(用于webbench测试?)
    //int reuse = 1;
    //setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );


    //添加事件
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    epoll_ctl(ept, EPOLL_CTL_ADD, fd, &ev);

    //pthread_mutex_lock(&mutex);
    //printf("clients number:%d fd=%d\n", ++m_user_count, fd);
    //pthread_mutex_unlock(&mutex);
    
}


void reactor::destory()
{

    //已经被删除
    if (m_fd == -1)
    {
        LOG_WARN("The connection has been closed");
        return;
    }

    //删除事件
    epoll_ctl(m_ept, EPOLL_CTL_DEL, m_fd, 0);
    
    //关闭文件映射
    if (filemmap)
    {
        httpd->unmap();
        filemmap = NULL;
        filelen = 0;
    }
    //close一旦关闭,accept可能会提取相同的文件描述符
    int tfd = m_fd;
    m_fd = -1;
    close(tfd);
    
    //pthread_mutex_lock(&mutex);
    //m_user_count--;
    //pthread_mutex_unlock(&mutex);
}


bool reactor::isfull(int MAXSIZE)
{
    int count = 0;
    //pthread_mutex_lock(&mutex);
    count = m_user_count;
    //pthread_mutex_unlock(&mutex);

    return count >= MAXSIZE;

}



void reactor::setevents(int events)
{
    re_events = events;
}

bool reactor::dealread()
{
    //缓冲区已经满了
    if (readbuf_len >= BUF_SIZE)
    {
        LOG_WARN("Read buffer full!!!");
        return false;
    }
    
    //非阻塞读
    while (1)
    {
        int n = recv(m_fd, readbuf+readbuf_len, BUF_SIZE-readbuf_len, 0);
        if (n == -1)
        {
            //缓冲区已经没有数据
            if (errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            //发生错误
            return false;
        }
        else if (n == 0)
        {
            //对方关闭连接
            return false;
        }
        readbuf_len += n;    
    }

    if (readbuf_len < BUF_SIZE)
    {
        readbuf[readbuf_len] = '\0';
    }
    

    return true;
}

bool reactor::dealwrite()
{

    while (1)
    {
        if (writebuf_len + filelen <= bytes_have_send)
        {
            //写事件响应完毕
            writebuf_len = 0;
            readbuf_len = 0;
            bytes_have_send = 0;
            filelen = 0;
            if (keepalive)
            {
                //继续监听
                modevent(EPOLLIN);
            }
            else
            {
                //关闭连接
                destory();
            }
                //关闭文件映射
            if (filemmap)
            {
                httpd->unmap();
                filemmap = NULL;
                filelen = 0;
            }
            LOG_INFO("response success");

            return true;
        }

        int n;
        //先写响应头再写文件
        if (bytes_have_send < writebuf_len)
        {   

            
            n = send(m_fd, writebuf+bytes_have_send, writebuf_len-bytes_have_send, 0);
            LOG_INFO("send http header");
            
        }
        else
        {
            if (filemmap == NULL)
            {
                LOG_WARN("File mapping incomplete");
                return false;
            }

            n = send(m_fd, filemmap+bytes_have_send - writebuf_len, writebuf_len + filelen -bytes_have_send, 0);
            LOG_INFO("send http content");
        
        }
         
        if (n <= -1)
        {
            //非阻塞
            if (errno == EAGAIN)
            {
                //继续监听读事件
                modevent(EPOLLOUT);
                return true;
            }
            
            return false;
        }
        LOG_INFO("send date bytes=%d", n);

        bytes_have_send += n;

    }
        

    return true;
}

void reactor::process()
{


    #ifdef REACTOR
    if (re_events & EPOLLIN)
    {
        if(!dealread())
        {
            destory();
            LOG_ERROR("%s:errno is:%d", "dealread", errno);
 
            return;
        }
        if (httpd->process_read(readbuf, readbuf_len))
        {
            //解析完成
            readbuf_len = 0;
            if (httpd->process_write(writebuf, &writebuf_len, &filemmap, &filelen, &keepalive))
            {
                bytes_have_send = 0;
                LOG_INFO("Listening for write events, writebuflen=%d, filelen=%d keppalive=%d", writebuf_len, filelen, (int)keepalive);

                modevent(EPOLLOUT);
            }
            else
            {
                destory();
            }
            
        }
        else
        {
            LOG_INFO("data not complete");
            //数据不完整继续等待数据
            modevent(EPOLLIN);
        }
    }
    
    else if (re_events & EPOLLOUT)
    {
        if(!dealwrite())
        {
            destory();
            LOG_ERROR("%s:errno is:%d", "dealwrite", errno);
            return;
        }
    }

    
    #else
    if (httpd->process_read(readbuf, readbuf_len))
    {
        //解析完成
        readbuf_len = 0;
        if (httpd->process_write(writebuf, &writebuf_len, &filemmap, &filelen, &keepalive))
        {
            bytes_have_send = 0;
            LOG_INFO("Listening for write events, writebuflen=%d, filelen=%d keppalive=%d", writebuf_len, filelen, (int)keepalive);
            modevent(EPOLLOUT);
        }
        else
        {
            destory();
        }
        
    }
    else
    {
        //数据不完整继续等待数据
        LOG_INFO("data not complete");
        modevent(EPOLLIN);
    }
    #endif
}