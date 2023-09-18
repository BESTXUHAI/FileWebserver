# FileWebserver
在Linux系统下使用C++语言编写文件资源服务器，实现了Reactor和Proactor两种事件模式。 支持解析HTTP的GET和POST请求报文，通过浏览器能请求图片视频等资源。
具有日志系统、线程池、连接池等模块。
## 准备工作
编译时需要用到第三方库pthread和mysqlclient，可以在config.h配置是否打开数据库和日志系统，以及事件模式。
## 数据库
如果开启了数据库模块，需要安装mysql服务器和客户端，创建对应的数据库和用户表。在main.cpp的database_init函数中配置数据库登录账号密码和库表。
```
create database webdb;
USE webdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;
```
## 运行结果
![登录显示](resources/test1.jpg)
![资源列表](resources/test2.jpg)
## 完整项目文件
[百度云](https://pan.baidu.com/s/1KDnY9qob6o8Pc6e8m51f9A ) 提取码：yuze

## 更新日志
- [x] 当请求服务器上大文件时，浏览器会重置连接，导致请求失败。解决方案：增加cookie检验

## 参考项目
https://github.com/qinguoyi/TinyWebServer
