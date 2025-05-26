#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXLINE 1024
#define OPEN_MAX 1024
#define SERV_PORT 8080

void perr_exit(const char* s) {
    perror(s);
    exit(1);
}

void disconnect(int cfd, int epfd) {
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret != 0) {
        perror("epoll ctl error");
        exit(1);
    }
    close(cfd);
}

// 忽略get请求后面的不感兴趣的部分
void dont_care_remaining(int cfd) {
    while (1) {
        char buf[1024] = { 0 };
        line = get_line(cfd, buf, sizeof(buf));
        if (line == '\n' || line == '\0') break;
    }
}

// 发送HTTP响应
void send_respond(int cfd, int no, int len, char* disp, char* type) {
    char buf[1024] = { 0 };

    // 构造HTTP响应头
    sprintf(buf, "HTTP/1.1 %d %s\r\n", no, disp);
    sprintf(buf + strlen(buf), "Content-Type: %s\r\n", type);
    sprintf(buf + strlen(buf), "Content-Length: %d\r\n", len);
    sprintf(buf + strlen(buf), "\r\n");  // 空行表示头部结束

    // 发送响应头
    send(cfd, buf, strlen(buf), 0);
}

// 发送服务器本地文件给浏览器
void send_file(int cfd, const char* file) {
    int n = 0;
    char buf[1024];

    // 打开服务器本地文件，cfd 是能访问客户端的 socket
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        // 404 错误页面
        perror("open error");
        exit(1);
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        int ret = send(cfd, buf, n, 0);
        if (ret == -1) {
            printf("errno = %d\n", errno);
            if (errno == EAGAIN) {
                printf("--- EAGAIN\n");
                continue;
            }
            else if (errno == EINTR) {
                printf("-- EINTR\n");
                continue;
            }
            else {
                perror("send error");
                exit(1);
            }
        }
        else if (ret < 4096) {
            printf("ret: %d\n", ret);
        }
    }

    close(fd);
}

// 发送目录内容
void send_dir(int cfd, const char* dirname) {
    int i, ret;
    // 拼一个html页面<table></table>
    char buf[4094] = { 0 };
    sprintf(buf, "<html><head><title>目录名:%s</title></head>", dirname);
    sprintf(buf + strlen(buf), "<body><h1>当前目录:%s</h1><table>", dirname);

    char enstr[1024] = { 0 };
    char path[1024] = { 0 };

    // 目录项二级指针
    struct dirent** ​ ptr;
    int num = scandir(dirname, &ptr, NULL, alphasort);

    // 遍历
    for (i = 0; i < num; ++i) {
        char* name = ptr[i]->d_name;
        // 拼接文件的完整路径
        sprintf(path, "%s/%s", dirname, name);
        printf("path=%s ====:==\n", path);

        struct stat st;
        stat(path, &st);

        //编码生成 &E5 &A7 之类的东西
        encode_ptr(enstr, sizeof(enstr), name);

        // 如果是文件
        if (S_ISREG(st.st_mode)) {
            sprintf(enstr, "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>", enstr, name, (long)st.st_size);
        }
        // 如果是目录
        else if (S_ISDIR(st.st_mode)) {
            sprintf(enstr, "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>", enstr, name, (long)st.st_size);
        }

        sprintf(buf + strlen(buf), "%s", enstr);

        // 发送数据
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                perror("send error:");
                continue;
            }
            else if (errno == EINTR) {
                perror("send error:");
                continue;
            }
            else {
                perror("send error:");
                exit(1);
            }
        }

        memset(buf, 0, sizeof(buf)); // 清空缓冲区
    }

    // 释放scandir分配的内存
    for (i = 0; i < num; ++i) {
        free(ptr[i]);
    }
    free(ptr);

    sprintf(buf + strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    printf("dir message send K!!!!\n");
}

void send_error(int cfd, int status, char* title, char* text) {
    char buf[4096] = { 0 };

    // HTTP response headers
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, title);
    sprintf(buf + strlen(buf), "Content-Type: text/html\r\n");
    sprintf(buf + strlen(buf), "Content-Length: %d\r\n", -1);  // Content-Length is not accurate here
    sprintf(buf + strlen(buf), "Connection: close\r\n\r\n");

    // Send headers
    send(cfd, buf, strlen(buf), 0);

    // Clear buffer for HTML content
    memset(buf, 0, sizeof(buf));

    // HTML content
    sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
    sprintf(buf + strlen(buf), "<body bgcolor=\"#cc99cc\"><h4 align=\"center\">%d %s</h4>\n", status, title);
    sprintf(buf + strlen(buf), "%s\n", text);
    sprintf(buf + strlen(buf), "<hr>\n</body>\n</html>\n");

    // Send HTML content
    send(cfd, buf, strlen(buf), 0);
}

// 处理HTTP请求，判断文件是否存在
void http_request(int cfd, const char* file) {
    // 判断文件是否存在
    struct stat sbuf;
    int ret = stat(file, &sbuf);
    if (ret != 0) {
        // 回发浏览器404错误页面
        send_error(cfd, 404，"Not Found", "No such file or direntry");
        perror("stat");
        exit(1);
    }

    // 判断是目录还是文件
    if (S_ISDIR(st.st_mode)) {  // 如果是目录
        // 发送头信息，get_file_type自己补充，此处不展示
        send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        // 发送目录信息
        send_dir(cfd, lfile);
    }
    else if (S_ISREG(st.st_mode)) {  // 如果是文件
        // 发送消息报头，get_file_type自己补充，此处不展示
        send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
        // 发送文件内容
        send_file(cfd, file);
    }
}

void do_read(int cfd, int epfd) {
    // 读取一行HTTP协议，拆分，获取 GET 文件名 协议号
    char line[1024] = { 0 };
    // 自己写一个get_line
    int len = get_line(cfd, line, sizeof(line)); // 读 HTTP请求协议首行 GET /hello.c HTTP/1.1

    if (len == 0) {
        printf("服务器，检査到客户端关闭....\n");
        disconnect(cfd, epfd);
    }
    else {
        char method[16], path[256], protocol[16];
        sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
        printf("method=%s, path=%s, protocol=%s\n", method, path, protocol);
        dont_care_remaining(cfd);

        if (strncasecmp(method, "GET", 3) == 0) {

            decode_str(path, path);

            char* file = path + 1; // 取出客户端要访问的文件名
            // 如果没有指定访问的资源，默认显示资源目录中的内容
            if (strcmp(path, "/") == 0) {  // 假设检查空路径
                file = "./";  // 设置默认资源目录
            }

            http_request(cfd, file);
            disconnect(cfd, epfd);
        }
    }
}

int main(int argc, char* argv[]) {
    int i, listenfd, connfd, sockfd; // 定义变量
    int n, num = 0; // n用于存储读取的字节数，num用于记录客户端数量
    ssize_t nready, efd, res; // nready用于存储epoll_wait返回的事件数，efd是epoll实例的文件描述符，res用于存储epoll_ctl的返回值
    char buf[MAXLINE], str[INET_ADDRSTRLEN]; // buf用于存储读取的数据，str用于存储客户端IP地址
    socklen_t clilen; // 客户端地址结构体的长度

    struct sockaddr_in cliaddr, servaddr; // 定义客户端和服务端的地址结构体
    struct epoll_event tep, ep[OPEN_MAX]; // tep用于临时存储epoll事件，ep用于存储所有epoll事件

    // 创建监听套接字
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    // 设置套接字选项，允许地址重用
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // 初始化服务端地址结构体
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);
    // 绑定地址和端口
    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    // 开始监听，最大连接数为20
    listen(listenfd, 20);

    // 创建epoll实例
    efd = epoll_create(OPEN_MAX);
    if (efd == -1)
        perr_exit("epoll_create error");

    // 设置epoll事件为EPOLLIN（可读事件）
    tep.events = EPOLLIN;
    tep.data.fd = listenfd;
    // 将监听套接字添加到epoll实例中
    res = epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &tep);
    if (res == -1)
        perr_exit("epoll_ctl error");

    // 进入主循环
    for (;;) {
        // 等待事件发生
        nready = epoll_wait(efd, ep, OPEN_MAX, -1);
        if (nready == -1)
            perr_exit("epoll_wait error");

        // 遍历所有发生的事件
        for (i = 0; i < nready; i++) {
            // 如果不是"读"事件，继续循环
            if (!(ep[i].events & EPOLLIN))
                continue;

            // 判断满足事件的fd是不是监听套接字
            if (ep[i].data.fd == listenfd) {
                // 接受连接
                clilen = sizeof(cliaddr);
                connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
                // 打印客户端信息
                printf("received from %s at PORT %d\n",
                    inet_ntop(AF_INET, &cliaddr.sin_addr, str, sizeof(str)),
                    ntohs(cliaddr.sin_port));
                printf("cfd %d---client %d\n", connfd, ++num);

                // 设置新连接的epoll事件为EPOLLIN（可读事件）
                tep.events = EPOLLIN;
                tep.data.fd = connfd;
                // 将新连接的套接字添加到epoll实例中
                res = epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &tep);
                if (res == -1)
                    perr_exit("epoll_ctl error");
            }
            else {
                // 如果是普通客户端套接字
                sockfd = ep[i].data.fd;
                // 从套接字中读取数据
                n = read(sockfd, buf, MAXLINE);
                if (n == 0) {
                    // 如果读取到0字节，表示客户端关闭连接
                    res = epoll_ctl(efd, EPOLL_CTL_DEL, sockfd, NULL);
                    if (res == -1)
                        perr_exit("epoll_ctl error");
                    close(sockfd);
                    printf("client[%d] closed connection\n", sockfd);
                }
                else if (n < 0) {
                    // 如果读取错误
                    perror("read n<0 error:");
                    res = epoll_ctl(efd, EPOLL_CTL_DEL, sockfd, NULL);
                    close(sockfd);
                }
                else {
                    do_read(sockfd, efd);
                }
            }
        }
    }

    // 关闭监听套接字
    close(listenfd);
    return 0;
}