// ֻ����get���������get�����򷵻ؼ�Ŀ¼��a�ļ������1.txt��
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

#define WEB_ROOT "./a"  // ��Ŀ¼�µ�a�ļ���
#define FILE_PATH WEB_ROOT "/1.txt"  // Ҫ���ص��ļ�·��

// ��������Ƿ�Ϊ��Ч��GET����
int is_valid_get_request(const char* request) {
    // �򵥼���Ƿ���"GET "��ͷ
    return strncmp(request, "GET ", 4) == 0;
}

// ��ȡ�ļ����ݵ�������
ssize_t read_file_to_buffer(const char* path, char* buffer, size_t buffer_size) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    ssize_t bytes_read = read(fd, buffer, buffer_size - 1);
    close(fd);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';  // ȷ���ַ�����'\0'��β
    }

    return bytes_read;
}

// ����HTTP��Ӧ
void generate_http_response(const char* file_content, size_t content_length, char* response, size_t response_size) {
    const char* status_line = "HTTP/1.1 200 OK\r\n";
    const char* content_type = "Content-Type: text/plain\r\n";
    const char* content_length_header = "Content-Length: ";

    // ������Ӧͷ
    snprintf(response, response_size,
        "%s%s%s%zu\r\n\r\n",
        status_line,
        content_type,
        content_length_header,
        content_length);

    // ����ļ�����
    strncat(response, file_content, response_size - strlen(response) - 1);
}

// ���������ص����� - ���ӿͻ��˶�ȡ����ʱ����
void read_cb(struct bufferevent* bev, void* arg)
{
    char buf[1024] = { 0 };  // ��ʼ��������Ϊȫ0

    size_t n = bufferevent_read(bev, buf, sizeof(buf) - 1);  // �����ռ��'\0'
    if (n > 0) {
        buf[n] = '\0';  // ȷ���ַ�����'\0'��β
        printf("client request: %s\n", buf);  // ��ӡ�ͻ��˷��͵�����

        // ����Ƿ�ΪGET����
        if (!is_valid_get_request(buf)) {
            const char* error_response = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 11\r\n\r\nBad Request";
            bufferevent_write(bev, error_response, strlen(error_response));
            return;
        }
    }

    // ��ȡ�ļ�����
    char file_content[1024] = { 0 };
    ssize_t file_size = read_file_to_buffer(FILE_PATH, file_content, sizeof(file_content));

    if (file_size <= 0) {
        const char* error_response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot Found";
        bufferevent_write(bev, error_response, strlen(error_response));
        return;
    }

    // ����HTTP��Ӧ
    char response[2048] = { 0 };
    generate_http_response(file_content, file_size, response, sizeof(response));

    // ������Ӧ
    bufferevent_write(bev, response, strlen(response));
}

// д�������ص����� - ��bufferevent_write���ݳɹ�д��ͻ���ʱ����
void write_cb(struct bufferevent* bev, void* arg)
{
    // printf("I'm server, successfully wrote data to client. Write buffer callback function was called...\n");
}

// �¼��ص����� - �������¼��������ӹرա�����ȣ�ʱ����
void event_cb(struct bufferevent* bev, short events, void* arg)
{
    if (events & BEV_EVENT_EOF) {
        // printf("Connection closed\n");  // �ͻ��������Ͽ�����
    }
    else if (events & BEV_EVENT_ERROR) {
        // printf("Some other error occurred\n");  // ������������
    }

    // �ͷ�bufferevent��Դ
    bufferevent_free(bev);
    // printf("bufferevent resource has been released...\n");
}

// �����ص����� - �����¿ͻ�������ʱ����
void cb_listener(
    struct evconnlistener* listener,  // ����������
    evutil_socket_t fd,               // �����ӵ��ļ�������
    struct sockaddr* addr,            // �ͻ��˵�ַ��Ϣ
    int len,                          // ��ַ����
    void* ptr)                        // �û����ݣ�ͨ��Ϊevent_baseָ�룩
{
    // printf("connect new client\n");

    // ��void*ָ��ת��Ϊevent_base*
    struct event_base* base = (struct event_base*)ptr;

    // ����һ���µ�bufferevent��������¼���
    struct bufferevent* bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
    if (!bev) {
        fprintf(stderr, "Error constructing bufferevent!\n");
        evutil_closesocket(fd);  // �ر��ļ�������
        return;
    }

    // ��bufferevent���������ûص�����
    bufferevent_setcb(bev, read_cb, write_cb, event_cb, NULL);

    // ����bufferevent�Ķ���������Ĭ����disable�ģ�
    bufferevent_enable(bev, EV_READ);
}

int main(int argc, const char* argv[])
{
    // ����ļ��Ƿ����
    struct stat st;
    if (stat(FILE_PATH, &st) == -1) {
        fprintf(stderr, "Error: File %s does not exist or cannot be accessed\n", FILE_PATH);
        return 1;
    }

    // ��ʼ����������ַ�ṹ
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));  // ��սṹ��
    serv.sin_family = AF_INET;       // ���õ�ַ��ΪIPv4
    serv.sin_port = htons(9876);     // ���ö˿ں�Ϊ9876�������ֽ���
    serv.sin_addr.s_addr = htonl(INADDR_ANY);  // �󶨵����п��õ�����ӿ�

    // �����¼���
    struct event_base* base;
    base = event_base_new();  // ����һ���µ��¼��������ڹ����¼�ѭ��

    // ����������
    struct evconnlistener* listener;
    listener = evconnlistener_new_bind(base,
        cb_listener,  // ���ӵ���ʱ�Ļص�����
        base,         // �ص������Ĳ�����ͨ��Ϊevent_base��
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,  // ѡ��ر�ʱ�ͷ���Դ�������ַ����
        -1,           // Ĭ�ϵ�backlog��С
        (struct sockaddr*)&serv,  // �󶨵ĵ�ַ�ṹ
        sizeof(serv));            // ��ַ�ṹ�Ĵ�С

    if (!listener) {
        fprintf(stderr, "Could not create a listener!\n");
        return 1;
    }

    // �����¼�ѭ��
    event_base_dispatch(base);

    // ������Դ
    evconnlistener_free(listener);  // �ͷż�����
    event_base_free(base);          // �ͷ��¼���

    return 0;
}