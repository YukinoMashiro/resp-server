//
// Created by yukino on 2023/4/29.
//

#include <sys/epoll.h>
#include "event.h"
#include "error.h"
#include "zmalloc.h"

int createEpollData(eventLoop *el) {
    epollData *state = zmalloc(sizeof(epollData));
    if (NULL == state) {
        printf("malloc epoll data failed.\r\n");
        return -1;
    }

    state->events = zmalloc(sizeof(struct epoll_event) * el->size);
    if (NULL == state->events) {
        printf("malloc epoll event failed.\r\n");
        zfree(state);
        return -1;
    }

    state->epollFd = epoll_create(EPOLL_SIZE);
    if (-1 == state->epollFd) {
        printf("create epoll socket failed.\r\n");
        zfree(state->events);
        zfree(state);
        return -1;
    }
    el->epData = state;
    return 0;
}

eventLoop *createEventLoop(int maxSize) {
    eventLoop *el = NULL;

    el = zmalloc(sizeof(eventLoop));
    if (NULL == el) {
        printf("malloc event loop failed.\r\n");
        return NULL;
    }
    el->size = maxSize;
    el->fileEvents = zmalloc(sizeof(fileEvent) * maxSize);
    if (NULL == el->fileEvents) {
        zfree(el);
        printf("malloc fileEvents failed.\r\n");
        return NULL;
    }
    el->firedFileEvents = zmalloc(sizeof(firedFileEvent) * maxSize);
    if (NULL == el->firedFileEvents) {
        zfree(el->fileEvents);
        zfree(el);
        printf("malloc firedFileEvents failed.\r\n");
        return NULL;
    }


    if (-1 == createEpollData(el)) {
        zfree(el->fileEvents);
        zfree(el->firedFileEvents);
        zfree(el);
        return NULL;
    }

    return el;
}

int addEpollEvent(eventLoop *el, int fd, int mask) {
    struct epoll_event ee = {0};
    int op;

    if (el->fileEvents[fd].mask == EVENT_NONE) {
        op = EPOLL_CTL_ADD;
    } else {
        op = EPOLL_CTL_MOD;
    }

    ee.events = 0;
    mask |= el->fileEvents[fd].mask;
    if (mask & EVENT_READABLE) ee.events |= EPOLLIN;
    if (mask & EVENT_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.fd = fd;
    if (-1 == epoll_ctl(el->epData->epollFd, op, fd,&ee)) {
        return -1;
    }

    return 0;
}

void deleteEpollEvent(eventLoop *el, int fd, int delMask) {
    epollData *state = el->epData;
    struct epoll_event ee = {0};
    int mask = el->fileEvents[fd].mask & (~delMask);

    ee.events = 0;
    if (mask & EVENT_READABLE) {
        ee.events |= EPOLLIN;
    }

    if(mask & EVENT_WRITABLE) {
        ee.events |= EPOLLOUT;
    }

    if (EVENT_NONE != mask) {
        epoll_ctl(state->epollFd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        epoll_ctl(state->epollFd, EPOLL_CTL_DEL, fd, &ee);
    }
}

void deleteFileEvent(eventLoop *el, int fd, int mask) {
    if (fd >= el->size) {
        printf("fd out of range.\r\n");
        return;
    }
    fileEvent *fe = &el->fileEvents[fd];
    if (EVENT_NONE == fe->mask) {
        return;
    }

    deleteEpollEvent(el, fd, mask);
    fe->mask = fe->mask & (~mask);
}

int eventPoll(eventLoop *el) {
    epollData *state = el->epData;
    int retVal, numEvents = 0;

    retVal = epoll_wait(state->epollFd,state->events,el->size,-1);
    if (retVal > 0) {
        int j;

        numEvents = retVal;
        for (j = 0; j < numEvents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;
            if (e->events & EPOLLIN) mask |= EVENT_READABLE;
            if (e->events & EPOLLOUT) mask |= EVENT_WRITABLE;
            if (e->events & EPOLLERR) mask |= EVENT_WRITABLE|EVENT_READABLE;
            if (e->events & EPOLLHUP) mask |= EVENT_WRITABLE|EVENT_READABLE;
            el->firedFileEvents[j].fd = e->data.fd;
            el->firedFileEvents[j].mask = mask;
        }
    }
    return numEvents;
}

int createFileEvent(eventLoop *el, int fd, int mask, void *proc, void *clientData) {
    if(-1 == fd) {
        printf("invalid server fd.\r\n");
        return ERROR_FAILED;
    }
    if(fd >= el->size) {
        printf("server fd out of range.\r\n");
        return ERROR_FAILED;
    }

    fileEvent *fe = &el->fileEvents[fd];
    if (NULL == fe) {
        printf("fileEvent is null.\r\n");
        return ERROR_FAILED;
    }

    if (-1 == addEpollEvent(el, fd, mask))  {
        printf("add epoll event failed.\r\n");
        return ERROR_FAILED;
    }

    fe->mask |= mask;
    if(mask & EVENT_READABLE) {
        fe->rFileProc = proc;
    }
    if (mask & EVENT_WRITABLE) {
        fe->wFileProc = proc;
    }

    fe->clientData = clientData;

    return ERROR_SUCCESS;
}
