// 只接受get请求，如果是get请求，则返回家目录下a文件夹里的1.txt。
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAXLINE 8192
#define SERV_PORT 8000
#define FILE_PATH "/home/a/1.txt"  // 家目录下a文件夹里的1.txt

struct s_info {
    struct sockaddr_in cliaddr;
    int connfd;
};

void send_file(int connfd, const char* filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        char* error_msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile not found";
        write(connfd, error_msg, strlen(error_msg));
        return;
    }

    // 获取文件大小
    struct stat st;
    fstat(fd, &st);
    int file_size = st.st_size;

    // 构造HTTP响应头
    char header[MAXLINE];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n",
        file_size);

    // 发送响应头
    write(connfd, header, strlen(header));

    // 发送文件内容
    char buffer[MAXLINE];
    int n;
    while ((n = read(fd, buffer, MAXLINE)) > 0) {
        write(connfd, buffer, n);
    }

    close(fd);
}

void* do_work(void* arg) {
    int n;
    struct s_info* ts = (struct s_info*)arg;
    char buf[MAXLINE];
    char str[INET_ADDRSTRLEN];
    char method[16], uri[MAXLINE], version[16];

    while (1) {
        n = read(ts->connfd, buf, MAXLINE);
        if (n == 0) {
            printf("the client %d closed...\n", ts->connfd);
            break;
        }
        printf("received from %s at PORT %d\n",
            inet_ntop(AF_INET, &(*ts).cliaddr.sin_addr, str, sizeof(str)),
            ntohs((*ts).cliaddr.sin_port));

        // 解析HTTP请求行
        if (sscanf(buf, "%15s %127s %15s", method, uri, version) != 3) {
            char* error_msg = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid request";
            write(ts->connfd, error_msg, strlen(error_msg));
            break;
        }

        // 只处理GET请求
        if (strcasecmp(method, "GET") != 0) {
            char* error_msg = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nOnly GET method is allowed";
            write(ts->connfd, error_msg, strlen(error_msg));
            break;
        }

        // 简单验证URI是否为根路径
        if (strcmp(uri, "/") != 0) {
            char* error_msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nOnly root path is supported";
            write(ts->connfd, error_msg, strlen(error_msg));
            break;
        }

        // 发送文件内容
        send_file(ts->connfd, FILE_PATH);

        break;  // 处理完一个请求后退出循环
    }
    close(ts->connfd);
    return (void*)0;
}

int main(void) {
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len;
    int listenfd, connfd;
    pthread_t tid;
    struct s_info ts[256];
    int i = 0;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SERV_PORT);

    bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr));
    listen(listenfd, 128);

    printf("Accepting client connect...\n");
    while (1) {
        cliaddr_len = sizeof(cliaddr);
        connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &cliaddr_len);
        ts[i].cliaddr = cliaddr;
        ts[i].connfd = connfd;
        pthread_create(&tid, NULL, do_work, (void*)&ts[i]);
        pthread_detach(tid);
        i++;
    }
    return 0;
}