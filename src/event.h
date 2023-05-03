//
// Created by yukino on 2023/4/29.
//

#ifndef RESP_SERVER_EVENT_H
#define RESP_SERVER_EVENT_H

//#include "server.h"

#define EVENT_NONE     0
#define EVENT_READABLE 1
#define EVENT_WRITABLE 2

struct eventLoop;

typedef void fileProc(struct eventLoop *eventLoop, int fd, void *clientData, int mask);

typedef struct epollData {
    int epollFd;
    struct epoll_event *events;
}epollData;

typedef struct fileEvent {
    int mask;
    fileProc *rFileProc;
    fileProc *wFileProc;
    void *clientData;
}fileEvent;

typedef struct firedFileEvent {
    int fd;
    int mask;
} firedFileEvent;

typedef struct eventLoop {
    int size;
    fileEvent *fileEvents;
    firedFileEvent *firedFileEvents;
    epollData *epData;
}eventLoop;

eventLoop *createEventLoop(int maxSize);
int createFileEvent(eventLoop *el, int fd, int mask, void *proc, void *clientData);
void deleteFileEvent(eventLoop *el, int fd, int mask);
int eventPoll(eventLoop *el);

#endif //RESP_SERVER_EVENT_H
