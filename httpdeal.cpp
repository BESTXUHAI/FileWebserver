#include "httpdeal.h"


// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
const char* rootpath = "/home/hai/vsworkdir/mywebserver/resources";

//数据库保护锁
locker db_lock;
std::map<string, string> map_users;
//程序运行时，将用户表预先读取到map中，提高效率
bool httpdeal::initmysql_result(connection_pool *connpool)
{
    MYSQL *mysql = NULL;
    //从线程池中取出一个连接
    connectionRAII conraii(&mysql, connpool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //返回结果
    MYSQL_RES *result = mysql_store_result(mysql);

    //将每一行数据保存到map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string user(row[0]);
        string passwd(row[1]);
        map_users[user] = passwd;
    }
    
}

void httpdeal::readinit()
{
    m_readbuf = NULL;
    m_readlen = 0;
    m_filepath = NULL;
    m_method = GET;
    m_http_code = NO_REQUEST;
    m_content_length = 0;
    m_host = NULL;
    m_filelen = 0;
    m_filemmap = NULL;
    m_writebuf = NULL;
    m_writelen = 0;
    m_openlogjudge = OPEN_LOGJUDGE;
}

void httpdeal::connectinit()
{
    m_keepalive = false;
    m_islogin = false;
    readinit();
}


httpdeal::httpdeal(/* args */)
{
    connectinit();
}

httpdeal::~httpdeal()
{
}
//从缓冲区中获取一行
char* httpdeal::getline()
{
    char* linest = m_readbuf;
    if (m_readlen <= 0)
    {
        return NULL;
    }
    
    for (int i = 0; i < m_readlen-1; i++)
    {
        if (linest[i]=='\r'&&linest[i+1]=='\n')
        {
            //设置结束标志位
            linest[i] = '\0';
            m_readbuf += i+2;
            m_readlen -= i+2;
            return linest;
        }
    }
    
    return NULL;
}

//解析请求行
HTTP_CODE httpdeal::parse_request_line( char* text )
{   
    //// GET /index.html HTTP/1.1
    //空格数
    int space = 0;
    int nextstart = 0;
    for (int i = 0; text[i]!='\0'; i++)
    {
        if (text[i] == ' ')
        {
            space++;
            text[i] = '\0';
            if (space == 1)
            {
                LOG_DEBUG("method:%s", text);
                if (strcasecmp(text, "GET") == 0 )
                {
                    m_method = GET;
                    nextstart = i+1;
                }
                else if (strcasecmp(text, "POST") == 0)
                {
                    m_method = POST;
                    nextstart = i+1;
                }
                
                else
                {
                    return BAD_REQUEST;
                }
            }
            else if (space == 2)
            {
                
                m_filepath = text + nextstart;
                LOG_DEBUG("url:%s", m_filepath);
                nextstart = i+1;
                //比较前7个字符大小
                if (strncasecmp(m_filepath, "http://", 7) == 0 ) {   
                    //http://192.168.110.129:10000/index.html是否带ip地址
                    m_filepath += 7;
                    // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
                    m_filepath = strchr( m_filepath, '/' );
                    if (m_filepath == NULL)
                    {
                        return BAD_REQUEST;
                    }
                }

            }            
        }
    }
    if (space != 2)
    {
        return BAD_REQUEST;
    }
    
    char* protocol = text + nextstart;
    LOG_INFO("protocol:%s", protocol);
    return NO_REQUEST;
}
/**
 * 
    GET / HTTP/1.1
    Host: www.baidu.com
    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:86.0) Gecko/20100101 Firefox/86.0
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,/;q=0.8
    Accept-Language: zh-CN,zh;q=0.8,zh-TW;q=0.7,zh-HK;q=0.5,en-US;q=0.3,en;q=0.2
    Accept-Encoding: gzip, deflate, br
    Connection: keep-alive
 * 
 **/

//解析请求头部信息
HTTP_CODE httpdeal::parse_headers( char* text )
{
        // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else {
        char* key = text;
        char* value = strchr( text, ':' );
        if(value == NULL)
            return BAD_REQUEST;
        value[0] = '\0';
        value++;
        while (*value == ' ')
        {
            value++;
        }
        //LOG_DEBUG("parse header key=%s value=%s", key, value);
        if (strcasecmp(key, "Connection") == 0)
        {
            if (strcasecmp(value, "keep-alive") == 0)
            {
                m_keepalive = true;
                //LOG_DEBUG("keep-alive");
            }
            
        }
        else if (strcasecmp(key, "Content-Length") == 0)
        {
            m_content_length = atol(value);
            //LOG_DEBUG("content length= %d", m_content_length);
        }
        else if (strcasecmp(key, "host") == 0)
        {
            m_host = value;
            //LOG_DEBUG("host:%s", m_host);
        }
        else
        {
            //LOG_DEBUG("unknow header key=%s value=%s", key, value);
        }
        
    }
    return NO_REQUEST;

}

//解析请求体只解析，请求体是否完整，
HTTP_CODE httpdeal::parse_content( char* text )
{
    if ( m_readlen >= m_content_length)
    {
        text[ m_content_length ] = '\0';
        LOG_DEBUG("content:%s", text);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

bool httpdeal::process_read(char* buf, int buflen)
{
    readinit();
    CHECK_STATE check_state = CHECK_STATE_REQUESTLINE;
    m_readbuf = buf;
    m_readlen = buflen;

    while (m_http_code == NO_REQUEST)
    {
        char* text = getline();
        //数据不完整
        if (text == NULL && check_state != CHECK_STATE_CONTENT)
        {
            return false;
        }
        LOG_DEBUG("get line:%s", text);
        
        switch (check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            m_http_code = parse_request_line(text);
            //解析未完成,改变状态
            if (m_http_code == NO_REQUEST)
            {
                check_state = CHECK_STATE_HEADER;
            }
            break;
        
        case CHECK_STATE_HEADER:
            m_http_code = parse_headers(text);
            //继续解析请求体
            if (m_http_code == GET_REQUEST && m_content_length != 0)
            {
                check_state = CHECK_STATE_CONTENT;
                m_http_code = NO_REQUEST;
            }
            break;
        case CHECK_STATE_CONTENT:
            m_http_code = parse_content(m_readbuf);
            break;

        default:
            break;
        }

    }


    return true;
}

bool httpdeal::userjudge(const char* user, const char* passwd)
{
    string ur(user);
    string pd(passwd);
    return map_users.count(ur)!=0 && map_users[ur] == pd;
}

bool httpdeal::register_user(const char* user, const char* passwd)
{
    string ur(user);
    if(map_users.count(ur) != 0)
        return false;

    MYSQL *mysql = NULL;
    //获取数据库连接实例
    connection_pool *connpool = connection_pool::GetInstance();
    //获取连接
    connectionRAII connraii(&mysql, connpool);
    char sql[200] = "";
    sprintf(sql, "INSERT INTO user(username, passwd) VALUES(\'%s\',\'%s\')", user, passwd);
    LOG_INFO("sql query:%s", sql);
    db_lock.lock();
    int res = mysql_query(mysql, sql);
    if (!res)
    {
        map_users[string(user)] = string(passwd);
    }
    db_lock.unlock();
    if (res != 0)
    {
        LOG_ERROR("insert into user failed");    
        return false;
    }
    
    return true;

}



bool httpdeal::dealurl(char *filepath, int len)
{
    
    if (m_method == GET)
    {
        if (!strcmp(m_filepath, "/register.html")||!strcmp(m_filepath, "/login.html")||!m_openlogjudge||m_islogin)
        {
            strncpy( filepath + len, m_filepath, 200 - len - 1 );
        }
        else
        {            
            LOG_INFO("jump to login.html");
            strncpy( filepath + len, "/login.html", 200 - len - 1 );
        }
    }
    //POST /register HTTP/1.1
    else if (m_method == POST)
    {
        if (!m_openlogjudge)
        {
            strncpy( filepath + len, m_filepath, 200 - len - 1 );
            return true;
        }
        

        char *user = strchr(m_readbuf, '=')+1;
        if (user == NULL || *user=='&')
        {
            LOG_DEBUG("username not found");
            return false;
        }
        char *passwd = strchr(user, '&');
        *passwd = '\0';
        passwd++;
        passwd=strchr(passwd, '=')+1;
        if (passwd == NULL)
        {
            LOG_DEBUG("passwd not found");
            return false;
        }
        LOG_DEBUG("username=%s,password=%s",user,passwd);
        bool ujudge = userjudge(user, passwd);

        //检查保存在文件路径中的action;
        if (strcmp(m_filepath+1, "login")==0)
        {
            if (ujudge)
            {
                m_filepath[1] = '\0';
                strncpy( filepath + len, "/", 200 - len - 1 );
                LOG_INFO("login success jump to /");
                m_islogin = true;
            }
            else
            {
                strncpy( filepath + len, "/loginerror.html", 200 - len - 1 );
                LOG_INFO("jump to loginerror.html");
            }
            
        }
        else if (strcmp(m_filepath+1, "register")==0)
        {
            if (register_user(user, passwd))
            {
                strncpy( filepath + len, "/login.html", 200 - len - 1 );
                LOG_INFO("register success jump to login.html");
            }
            else
            {
                strncpy( filepath + len, "/registererror.html", 200 - len - 1 );
                LOG_INFO("jump to registererror.html");
            }
        }
        else
        {
            LOG_DEBUG("action not found");
            return false;
        }
        
    }
    
    return true;

}

//解析请求文件的状态
HTTP_CODE httpdeal::filestat()
{
    char filepath[200] = "";
    strcpy( filepath, rootpath );
    int len = strlen( rootpath );
    if(!dealurl(filepath, len))
    {
        return BAD_REQUEST;
    }
    struct stat file_stat;
    // 获取文件的相关的状态信息，-1失败，0成功
    if ( stat( filepath, &file_stat ) < 0 ) {
        LOG_WARN("filestat parse acquisition failed");
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( file_stat.st_mode ) ) {
        return DIR_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( filepath, O_RDONLY );
    // 创建内存映射
    m_filemmap = ( char* )mmap( 0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    m_filelen = file_stat.st_size;
    return FILE_REQUEST;

}

// 对内存映射区执行munmap操作
void httpdeal::unmap() {
    if( m_filemmap )
    {
        munmap( m_filemmap, m_filelen);
        m_filemmap = NULL;
    }
}



// 往写缓冲中写入待发送的数据
bool httpdeal::add_response( const char* format, ... ) {
     
    if( m_writelen >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_writebuf + m_writelen, WRITE_BUFFER_SIZE - 1 - m_writelen, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_writelen ) ) {
        return false;
    }
    m_writelen += len;
    va_end( arg_list );
    return true;
}

bool httpdeal::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool httpdeal::add_headers(int content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool httpdeal::add_content_length(int content_len) {
    if(content_len <=0 )
        return true;
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool httpdeal::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_keepalive == true ) ? "keep-alive" : "close" );
}

bool httpdeal::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool httpdeal::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool httpdeal::add_content_type() {
    return add_response("Content-Type:%s\r\n", get_mime_type(m_filepath));
}

bool httpdeal::add_contentdir()
{
    if ((m_filemmap = (char *)mmap(0, WRITE_BUFFER_SIZE*10, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0)) == MAP_FAILED)
    {
        LOG_ERROR("Error in anonymous memory mapping"); 
        return false;
    }
   

    bool flag=true;
    flag = flag & readtobuf("/dir_header.html");

    char filepath[200] = "";
    strcpy( filepath, rootpath );
    int len = strlen( rootpath );
    strncpy( filepath + len, m_filepath, 200 - len - 1 );

    //send dir info 
    DIR *dir = opendir(filepath);
    if(dir == NULL){
        LOG_ERROR("opendir err"); 
        return false;
    }
    int iglen = 5;
    const char *ignore[5] = {"register", "login", "index", "favicon", "dir_"};
    struct dirent *dent = NULL;
    while( (dent= readdir(dir) ) ){
        if(!flag)
        {
            break;
        }
        bool ig=false;

        for (int i = 0; i < iglen; i++)
        {
            

            if (strncmp(dent->d_name, ignore[i], strlen(ignore[i])) == 0)
            {
                ig=true;
                break;
            }
        }
        if (ig)
        {
            continue;
        }
        

        int n=0;
        if(dent->d_type == DT_DIR){
            //目录文件 特殊处理
            //格式 <a href="dirname/">dirname</a><p>size</p><p>time</p></br>
            n = sprintf(m_filemmap+m_filelen,"<li><a href='%s/'>%32s/</a></li>",dent->d_name,dent->d_name);
            
        }
        else if(dent->d_type == DT_REG){
            //普通文件 直接显示列表即可
            n = sprintf(m_filemmap+m_filelen,"<li><a href='%s'>%32s</a></li>",dent->d_name,dent->d_name);
            
        }
        if (n < 0)
        {
            flag=false;
        }
        else
            m_filelen += n;
        
    }
    closedir(dir);
    flag = flag & readtobuf("/dir_tail.html");
    return flag;
}

bool httpdeal::readtobuf(const char* file_p)
{
    char filepath[200] = "";
    strcpy( filepath, rootpath );
    int len = strlen( rootpath );
    strncpy( filepath + len, file_p, 200 - len - 1 );

    int fd = open(filepath, O_RDONLY);
    int ret = read(fd, m_filemmap+m_filelen, WRITE_BUFFER_SIZE*10-m_filelen);
    if (ret < 0 )
    {
        LOG_ERROR("read file error:%s", file_p);
        close(fd);
        return false;
    }

    m_filelen += ret;
    close(fd);
    return true;
}

bool httpdeal::process_write(char* writebuf, int* writelen, char** filemmap, int* filelen, bool* keepalive)
{

    if (m_filepath != NULL && m_http_code == GET_REQUEST)
    {
        LOG_DEBUG("parse filestat:%s", m_filepath);
        m_http_code = filestat();
    }




    m_writebuf = writebuf;
    m_writelen = 0;
    *keepalive = m_keepalive;
    bool flag = true;
    switch (m_http_code)
    {
        case INTERNAL_ERROR:
            flag &= add_status_line( 500, error_500_title );
            flag &=add_headers( strlen( error_500_form ) );
            flag &=add_content( error_500_form );
            break;
        case BAD_REQUEST:
            flag &= add_status_line( 400, error_400_title );
            flag &= add_headers( strlen( error_400_form ) );
            flag &= add_content( error_400_form );
            break;
        case NO_RESOURCE:
            flag &= add_status_line( 404, error_404_title );
            flag &= add_headers( strlen( error_404_form ) );
            flag &= add_content( error_404_form );
            break;
        case FORBIDDEN_REQUEST:
            flag &= add_status_line( 403, error_403_title );
            flag &= add_headers(strlen( error_403_form));
            flag &= add_content( error_403_form );
            break;
        case FILE_REQUEST:
            flag &= add_status_line(200, ok_200_title );
            flag &= add_headers(m_filelen);
        
            *writelen = m_writelen;
            *filemmap = m_filemmap;
            *filelen = m_filelen;
            break;
        case DIR_REQUEST:
            flag &= add_status_line(200, ok_200_title );
            flag &= add_contentdir();
            flag &= add_headers(m_filelen);
            *filemmap = m_filemmap;
            *filelen = m_filelen;
            break;
        default:
            LOG_WARN("unknow http stat");
            return false;
    }


    *writelen = m_writelen;
    if (!flag)
    {
        LOG_WARN("Insufficient write buffer length");
        return false;
    }
    

    return true;

}

//通过文件名字获得文件类型
const char* httpdeal::get_mime_type(char *name)
{
    char* dot;
    const char* type = NULL;
    dot = strrchr(name, '.');	

    if (m_openlogjudge && !m_islogin)
    {
        return "text/html; charset=utf-8";
    }
    


    if (dot == (char*)0)
        type = "text/html; charset=utf-8";
    else if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        type = "text/html; charset=utf-8";
    else if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        type = "image/jpeg";
    else if (strcmp(dot, ".gif") == 0)
        type = "image/gif";
    else if (strcmp(dot, ".png") == 0)
        type = "image/png";
    else if (strcmp(dot, ".css") == 0)
        type = "text/css";
    else if (strcmp(dot, ".au") == 0)
        type = "audio/basic";
    else if (strcmp( dot, ".wav") == 0)
        type = "audio/wav";
    else if (strcmp(dot, ".avi") == 0)
        type = "video/x-msvideo";
    else if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        type = "video/quicktime";
    else if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        type = "video/mpeg";
    else if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        type = "model/vrml"; 
    else if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        type = "audio/midi";
    else if (strcmp(dot, ".mp3") == 0)
        type = "audio/mp3";
    else if (strcmp(dot, ".ogg") == 0)
        type = "application/ogg";
    else if (strcmp(dot, ".pac") == 0)
        type = "application/x-ns-proxy-autoconfig";
    else
        type = "text/plain; charset=utf-8";

    return type;
}