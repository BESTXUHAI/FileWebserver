#ifndef HTTPDEAL_H
#define HTTPDEAL_H
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <dirent.h>
#include <iostream>
#include <map>
#include "log/log.h"
#include "sql/connection_pool.h"


#define WRITE_BUFFER_SIZE 1024
// HTTP请求方法，这里只支持GET和POST
enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

/*
    解析客户端请求时，主状态机的状态
    CHECK_STATE_REQUESTLINE:当前正在分析请求行
    CHECK_STATE_HEADER:当前正在分析头部字段
    CHECK_STATE_CONTENT:当前正在解析请求体
    CHECK_STATE_SUCCESS:解析全部完成
*/
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
/*
    服务器处理HTTP请求的可能结果，报文解析的结果
    NO_REQUEST          :   请求不完整，需要继续读取客户数据
    GET_REQUEST         :   表示获得了一个完成的客户请求
    BAD_REQUEST         :   表示客户请求语法错误
    NO_RESOURCE         :   表示服务器没有资源
    FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
    FILE_REQUEST        :   文件请求,获取文件成功
    INTERNAL_ERROR      :   表示服务器内部错误
    CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    DIR_REQUEST         :   表示请求的是目录文件
*/
enum HTTP_CODE { NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION, DIR_REQUEST };


class httpdeal
{

    
private:
    /* data */
    //传入的缓冲区
    char* m_readbuf;
    //传入的缓冲区长度
    int m_readlen;
    //请求类型
    METHOD m_method;
    //保存解析的文件地址
    char* m_filepath;
    //映射文件长度
    int m_filelen;
    //映射文件地址
    char* m_filemmap;
    //请求体长度
    int m_content_length;
    //是否保持连接
    bool m_keepalive;
    //保存请求的主机地址
    char* m_host;
    //保存解析结果
    HTTP_CODE m_http_code;
    //写入缓冲区地址
    char* m_writebuf;
    //写入的长度
    int m_writelen;
    //是否已经登录
    bool m_islogin;
    //开启登录校验
    bool m_openlogjudge;


public:
    httpdeal(/* args */);
    ~httpdeal();
    //解析报文内容
    bool process_read(char* buf, int buflien);
    //回复响应报文
    bool process_write(char* writebuf, int* writelen, char** filemmap, int* filelen, bool* keepalive);
    //将账号信息预读到map中
    static bool initmysql_result(connection_pool *connpool);
    void unmap();
    void connectinit();
private:
    
    //从缓冲区中读取一行
    char* getline();
    //初始化各种成员
    void readinit();

    // 下面这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    //写入响应报文
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    bool add_contentdir();
    bool readtobuf(const char* file_p);
    //通过文件名字获得文件类型
    const char * get_mime_type(char *name);

    //查看请求的文件状态
    HTTP_CODE filestat();
    //根据请求类型，对请求的文件进行判断
    bool dealurl(char *filepath, int len);
    //查找数据库中账号是否存在
    bool userjudge(const char* user, const char* passwd);
    bool register_user(const char* user, const char* passwd);
};


#endif