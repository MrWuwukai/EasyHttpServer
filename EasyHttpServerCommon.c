#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>

#define SERV_PORT 8080
#define MAX_CONNECTIONS 128
#define BUFFER_SIZE 1024

// 错误处理函数
void sys_err(const char* str)
{
    perror(str);
    exit(1);
}

// 如果是get请求则返回家目录下a文件夹里的它所需要的文件，如果是POST请求则在终端打印出它POST请求的请求体。
void handle_http_request(int cfd) {
    char buf[1024];
    int ret;

    // 读取HTTP请求
    ret = read(cfd, buf, sizeof(buf));
    if (ret <= 0) {
        return;
    }

    // 解析HTTP请求方法
    char method[16] = { 0 };
    char path[256] = { 0 };
    sscanf(buf, "%15s %255[^ \r\n]", method, path);

    // 处理GET请求
    if (strcasecmp(method, "GET") == 0) {
        // 确保路径以/开头
        if (path[0] != '/') {
            path[0] = '/';
            path[1] = '\0';
        }

        // 拼接家目录和a文件夹路径
        char full_path[512];
        const char* home_dir = getenv("HOME");
        if (!home_dir) home_dir = ".";
        snprintf(full_path, sizeof(full_path), "%s/a%s", home_dir, path);

        // 确保路径在a文件夹内（简单安全检查）
        if (strncmp(full_path, home_dir, strlen(home_dir)) != 0 ||
            strstr(full_path, "../") != NULL) {
            const char* error_response =
                "HTTP/1.1 403 Forbidden\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>403 Forbidden</body></html>";
            write(cfd, error_response, strlen(error_response));
            return;
        }

        // 检查是否是目录
        struct stat st;
        if (stat(full_path, &st) == -1 || S_ISDIR(st.st_mode)) {
            // 如果是目录，尝试返回index.html
            char index_path[512];
            snprintf(index_path, sizeof(index_path), "%s/index.html", full_path);
            if (stat(index_path, &st) != -1 && !S_ISDIR(st.st_mode)) {
                full_path[strlen(full_path)] = '\0'; // 确保字符串结束
                snprintf(full_path + strlen(full_path), sizeof(full_path) - strlen(full_path), "/index.html");
            }
            else {
                const char* dir_response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "\r\n"
                    "<html><body>Directory listing not supported</body></html>";
                write(cfd, dir_response, strlen(dir_response));
                return;
            }
        }

        // 打开文件
        int fd = open(full_path, O_RDONLY);
        if (fd == -1) {
            const char* not_found_response =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>404 Not Found</body></html>";
            write(cfd, not_found_response, strlen(not_found_response));
            return;
        }

        // 读取文件内容并发送
        const char* header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "\r\n";
        write(cfd, header, strlen(header));

        char file_buf[1024];
        ssize_t bytes_read;
        while ((bytes_read = read(fd, file_buf, sizeof(file_buf))) > 0) {
            write(cfd, file_buf, bytes_read);
        }
        close(fd);

    }
    // 处理POST请求
    else if (strcasecmp(method, "POST") == 0) {
        // 找到请求体的起始位置
        char* body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4; // 跳过\r\n\r\n

            // 打印请求体到终端
            printf("POST request body:\n%s\n", body_start);

            // 发送简单响应
            const char* response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>POST request received</body></html>";
            write(cfd, response, strlen(response));
        }
        else {
            // 没有找到请求体
            const char* response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>Invalid POST request</body></html>";
            write(cfd, response, strlen(response));
        }
    }
    // 处理其他方法
    else {
        const char* response =
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Type: text/html\r\n"
            "\r\n"
            "<html><body>Method Not Allowed</body></html>";
        write(cfd, response, strlen(response));
    }
}

int main(int argc, char* argv[])
{
    int lfd = 0, cfd = 0;
    int ret;
    struct sockaddr_in serv_addr, clit_addr;
    socklen_t clit_addr_len;

    // 创建监听套接字
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        sys_err("socket error");
    }

    // 设置服务器地址结构
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 绑定地址和端口
    if (bind(lfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        sys_err("bind error");
    }

    // 开始监听
    if (listen(lfd, MAX_CONNECTIONS) == -1) {
        sys_err("listen error");
    }

    printf("Web server is running on port %d...\n", SERV_PORT);

    while (1) {
        clit_addr_len = sizeof(clit_addr);

        // 接受客户端连接（这里没有使用多线程处理多个连接）
        cfd = accept(lfd, (struct sockaddr*)&clit_addr, &clit_addr_len);
        if (cfd == -1) {
            sys_err("accept error");
        }

        // 处理HTTP请求
        handle_http_request(cfd);

        // 关闭客户端连接
        close(cfd);
    }

    // 关闭监听套接字（实际上不会执行到这里）
    close(lfd);
    return 0;
}