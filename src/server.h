//
// Created by yukino on 2023/4/29.
//

#ifndef RESP_SERVER_SERVER_H
#define RESP_SERVER_SERVER_H

#include "event.h"

#define DEFAULT_PORT 6379;
#define DEFAULT_BACKLOG 511;
#define MAX_CLIENT_LIMIT 10000;

typedef struct respServer {
    eventLoop *el;
    int port;
    int ipFd;
    int tcpBacklog;
    int maxClient;
}respServer;

extern respServer server;

#endif //RESP_SERVER_SERVER_H
