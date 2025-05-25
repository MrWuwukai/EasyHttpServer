// 只接受get请求，如果是get请求，则返回家目录下a文件夹里的1.txt。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define WEB_ROOT "./a"  // 家目录下的a文件夹
#define FILE_PATH WEB_ROOT "/1.txt"  // 要返回的文件路径

// 检查请求是否为有效的GET请求
int is_valid_get_request(const char* request) {
    // 简单检查是否以"GET "开头
    return strncmp(request, "GET ", 4) == 0;
}

// 读取文件内容到缓冲区
ssize_t read_file_to_buffer(const char* path, char* buffer, size_t buffer_size) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, buffer_size - 1);
    close(fd);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';  // 确保字符串以'\0'结尾
    }

    return bytes_read;
}

// 生成HTTP响应
void generate_http_response(const char* file_content, size_t content_length, char* response, size_t response_size) {
    const char* status_line = "HTTP/1.1 200 OK\r\n";
    const char* content_type = "Content-Type: text/plain\r\n";
    const char* content_length_header = "Content-Length: ";

    // 构建响应头
    snprintf(response, response_size,
        "%s%s%s%zu\r\n\r\n",
        status_line,
        content_type,
        content_length_header,
        content_length);

    // 添加文件内容
    strncat(response, file_content, response_size - strlen(response) - 1);
}

// 读缓冲区回调函数 - 当从客户端读取数据时触发
void read_cb(struct bufferevent* bev, void* arg)
{
    char buf[1024] = { 0 };  // 初始化缓冲区为全0

    size_t n = bufferevent_read(bev, buf, sizeof(buf) - 1);  // 留出空间给'\0'
    if (n > 0) {
        buf[n] = '\0';  // 确保字符串以'\0'结尾
        printf("client request: %s\n", buf);  // 打印客户端发送的数据

        // 检查是否为GET请求
        if (!is_valid_get_request(buf)) {
            const char* error_response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nBad Request";
            bufferevent_write(bev, error_response, strlen(error_response));
            return;
        }
    }

    // 读取文件内容
    char file_content[1024] = { 0 };
    ssize_t file_size = read_file_to_buffer(FILE_PATH, file_content, sizeof(file_content));

    if (file_size <= 0) {
        const char* error_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
        bufferevent_write(bev, error_response, strlen(error_response));
        return;
    }

    // 生成HTTP响应
    char response[2048] = { 0 };
    generate_http_response(file_content, file_size, response, sizeof(response));

    // 发送响应
    bufferevent_write(bev, response, strlen(response));
}

// 写缓冲区回调函数 - 当bufferevent_write数据成功写入客户端时触发
void write_cb(struct bufferevent* bev, void* arg)
{
    // printf("I'm server, successfully wrote data to client. Write buffer callback function was called...\n");
}

// 事件回调函数 - 当发生事件（如连接关闭、错误等）时触发
void event_cb(struct bufferevent* bev, short events, void* arg)
{
    if (events & BEV_EVENT_EOF) {
        // printf("Connection closed\n");  // 客户端正常断开连接
    }
    else if (events & BEV_EVENT_ERROR) {
        // printf("Some other error occurred\n");  // 发生其他错误
    }

    // 释放bufferevent资源
    bufferevent_free(bev);
    // printf("bufferevent resource has been released...\n");
}

// 监听回调函数 - 当有新客户端连接时触发
void cb_listener(
    struct evconnlistener* listener,  // 监听器对象
    evutil_socket_t fd,               // 新连接的文件描述符
    struct sockaddr* addr,            // 客户端地址信息
    int len,                          // 地址长度
    void* ptr)                        // 用户数据（通常为event_base指针）
{
    // printf("connect new client\n");

    // 将void*指针转换为event_base*
    struct event_base* base = (struct event_base*)ptr;

    // 创建一个新的bufferevent对象（添加事件）
    struct bufferevent* bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        fprintf(stderr, "Error constructing bufferevent!\n");
        evutil_closesocket(fd);  // 关闭文件描述符
        return;
    }

    // 给bufferevent缓冲区设置回调函数
    bufferevent_setcb(bev, read_cb, write_cb, event_cb, NULL);

    // 启用bufferevent的读缓冲区（默认是disable的）
    bufferevent_enable(bev, EV_READ);
}

int main(int argc, const char* argv[])
{
    // 检查文件是否存在
    struct stat st;
    if (stat(FILE_PATH, &st) == -1) {
        fprintf(stderr, "Error: File %s does not exist or cannot be accessed\n", FILE_PATH);
        return 1;
    }

    // 初始化服务器地址结构
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));  // 清空结构体
    serv.sin_family = AF_INET;       // 设置地址族为IPv4
    serv.sin_port = htons(9876);     // 设置端口号为9876（网络字节序）
    serv.sin_addr.s_addr = htonl(INADDR_ANY);  // 绑定到所有可用的网络接口

    // 创建事件基
    struct event_base* base;
    base = event_base_new();  // 创建一个新的事件基，用于管理事件循环

    // 创建监听器
    struct evconnlistener* listener;
    listener = evconnlistener_new_bind(base,
        cb_listener,  // 连接到达时的回调函数
        base,         // 回调函数的参数（通常为event_base）
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,  // 选项：关闭时释放资源，允许地址重用
        -1,           // 默认的backlog大小
        (struct sockaddr*)&serv,  // 绑定的地址结构
        sizeof(serv));            // 地址结构的大小

    if (!listener) {
        fprintf(stderr, "Could not create a listener!\n");
        return 1;
    }

    // 启动事件循环
    event_base_dispatch(base);

    // 清理资源
    evconnlistener_free(listener);  // 释放监听器
    event_base_free(base);          // 释放事件基

    return 0;
}