//
// Created by yukino on 2023/4/29.
//

#include <malloc.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "server.h"
#include "error.h"

respServer server;

int tcpServer(int port , int backlog) {
    int serverSocket = -1;
    struct sockaddr_in server_addr = {0};

    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        printf("create server socket failed.\r\n");
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        printf("bind server socket failed.\r\n");
        return -1;
    }

    if (listen(serverSocket, backlog) == -1) {
        printf("listen server socket failed.\r\n");
    }
    return serverSocket;
}

#define BUF_SIZE 1024
void receiveClientData(eventLoop *el, int fd, int mask) {
    char buf[BUF_SIZE] = {0};
    int strLen;

    strLen = read(fd, buf, BUF_SIZE);
    if (strLen == 0) {
        close(fd);
        printf("closed client: %d\r\n", fd);
    } else {
        //write(fd, buf, strLen);
        write(fd, "+OK\r\n", 5);
        printf("received from %d: %s", fd, buf);
    }
}

void acceptTcpHandler(eventLoop *el, int fd, int mask) {
    socklen_t saSize = 0;
    struct sockaddr_in sa;
    int clientFd = -1;

    saSize = sizeof(sa);
    clientFd = accept(fd, (struct sockaddr*)&sa, &saSize);
    printf("connected client: %d\r\n", clientFd);
    createFileEvent(el, clientFd, EVENT_READABLE, receiveClientData);
}

void initServer() {
    unsigned long error;

    // 创建事件循环器
    server.el = createEventLoop(server.maxClient);
    if (NULL == server.el) {
        return;
    }

    // 开启TCP服务侦听，接收客户端请求
    if (0 != server.port) {
        server.ipFd = tcpServer(server.port, server.tcpBacklog);
    }

    // 注册epoll
    error = createFileEvent(server.el, server.ipFd, EVENT_READABLE, acceptTcpHandler);
    if (ERROR_SUCCESS != error) {
        printf("failed to create server.ipFd file event.\r\n");
        return;
    }

}

void initConf() {
    server.el = NULL;
    server.port = DEFAULT_PORT;
    server.ipFd = -1;
    server.tcpBacklog = DEFAULT_BACKLOG;
    server.maxClient = MAX_CLIENT_LIMIT;
}

void ProcessEvents() {
    eventLoop *el = server.el;
    int numEvents;
    int j;
    for(;;) {
        numEvents = eventPoll(el);
        for (j = 0; j < numEvents; j++) {
            fileEvent *fe = &el->fileEvents[el->firedFileEvents[j].fd];
            int mask = el->firedFileEvents[j].mask;
            int fd = el->firedFileEvents[j].fd;
            if(fe->mask & mask & EVENT_READABLE) {
                fe->rFileProc(el, fd, mask);
            }
            if (fe->mask & mask & EVENT_WRITABLE) {
                fe->wFileProc(el, fd, mask);
            }
        }
    }
}

int main(int argc, char **argv) {

    initConf();

    initServer();

    ProcessEvents();

    return 0;
}