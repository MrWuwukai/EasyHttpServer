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

// ��������
void sys_err(const char* str)
{
    perror(str);
    exit(1);
}

// �����get�����򷵻ؼ�Ŀ¼��a�ļ������������Ҫ���ļ��������POST���������ն˴�ӡ����POST����������塣
void handle_http_request(int cfd) {
    char buf[1024];
    int ret;

    // ��ȡHTTP����
    ret = read(cfd, buf, sizeof(buf));
    if (ret <= 0) {
        return;
    }

    // ����HTTP���󷽷�
    char method[16] = { 0 };
    char path[256] = { 0 };
    sscanf(buf, "%15s %255[^ \r\n]", method, path);

    // ����GET����
    if (strcasecmp(method, "GET") == 0) {
        // ȷ��·����/��ͷ
        if (path[0] != '/') {
            path[0] = '/';
            path[1] = '\0';
        }

        // ƴ�Ӽ�Ŀ¼��a�ļ���·��
        char full_path[512];
        const char* home_dir = getenv("HOME");
        if (!home_dir) home_dir = ".";
        snprintf(full_path, sizeof(full_path), "%s/a%s", home_dir, path);

        // ȷ��·����a�ļ����ڣ��򵥰�ȫ��飩
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

        // ����Ƿ���Ŀ¼
        struct stat st;
        if (stat(full_path, &st) == -1 || S_ISDIR(st.st_mode)) {
            // �����Ŀ¼�����Է���index.html
            char index_path[512];
            snprintf(index_path, sizeof(index_path), "%s/index.html", full_path);
            if (stat(index_path, &st) != -1 && !S_ISDIR(st.st_mode)) {
                full_path[strlen(full_path)] = '\0'; // ȷ���ַ�������
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

        // ���ļ�
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

        // ��ȡ�ļ����ݲ�����
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
    // ����POST����
    else if (strcasecmp(method, "POST") == 0) {
        // �ҵ����������ʼλ��
        char* body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4; // ����\r\n\r\n

            // ��ӡ�����嵽�ն�
            printf("POST request body:\n%s\n", body_start);

            // ���ͼ���Ӧ
            const char* response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>POST request received</body></html>";
            write(cfd, response, strlen(response));
        }
        else {
            // û���ҵ�������
            const char* response =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                "<html><body>Invalid POST request</body></html>";
            write(cfd, response, strlen(response));
        }
    }
    // ������������
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

    // ���������׽���
    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        sys_err("socket error");
    }

    // ���÷�������ַ�ṹ
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // �󶨵�ַ�Ͷ˿�
    if (bind(lfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        sys_err("bind error");
    }

    // ��ʼ����
    if (listen(lfd, MAX_CONNECTIONS) == -1) {
        sys_err("listen error");
    }

    printf("Web server is running on port %d...\n", SERV_PORT);

    while (1) {
        clit_addr_len = sizeof(clit_addr);

        // ���ܿͻ������ӣ�����û��ʹ�ö��̴߳��������ӣ�
        cfd = accept(lfd, (struct sockaddr*)&clit_addr, &clit_addr_len);
        if (cfd == -1) {
            sys_err("accept error");
        }

        // ����HTTP����
        handle_http_request(cfd);

        // �رտͻ�������
        close(cfd);
    }

    // �رռ����׽��֣�ʵ���ϲ���ִ�е����
    close(lfd);
    return 0;
}