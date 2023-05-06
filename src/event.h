//
// Created by yukino on 2023/4/29.
//

#ifndef RESP_SERVER_EVENT_H
#define RESP_SERVER_EVENT_H

#include <time.h>
#include <sys/time.h>

#define EVENT_NONE     0
#define EVENT_READABLE 1
#define EVENT_WRITABLE 2

#define EVENT_FILE_EVENTS (1<<0)
#define EVENT_TIME_EVENTS (1<<1)
#define EVENT_ALL_EVENTS (EVENT_FILE_EVENTS|EVENT_TIME_EVENTS)
#define EVENT_DONT_WAIT (1<<2)
#define EVENT_CALL_BEFORE_SLEEP (1<<3)

#define EVENT_NOMORE -1
#define EVENT_DELETED_EVENT_ID -1

#define EPOLL_SIZE     1024

struct eventLoop;

typedef void fileProc(struct eventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int timeProc(struct eventLoop *eventLoop, long long id, void *clientData);
typedef void eventFinalizerProc(struct eventLoop *eventLoop, void *clientData);
typedef void beforeSleepProc(struct eventLoop *eventLoop);

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

typedef struct timeEvent {
    long long id;                       /* 事件事件ID */
    long when_sec;                      /* 时间事件下一次执行的秒数(UNIX事件戳) */
    long when_ms;                       /* 事件事件下一次执行的剩余毫秒数 */
    timeProc *timeProc;                 /* 时间事件处理函数 */
    eventFinalizerProc *finalizerProc;
    void *clientData;                   /* 客户端传入的附加数据 */
    struct timeEvent *prev;             /* 前一个时间事件 */
    struct timeEvent *next;             /* 后一个时间事件 */
    int refcount; /* refcount to prevent timer events from being
  		   * freed in recursive time event calls. */
} timeEvent;

typedef struct eventLoop {
    int maxfd;
    int size;
    fileEvent *fileEvents;
    firedFileEvent *firedFileEvents;
    epollData *epData;
    long long timeEventNextId;
    timeEvent *timeEventHead;
    time_t lastTime;     /* Used to detect system clock skew */
    beforeSleepProc *beforeSleep;
    int flags;
}eventLoop;

eventLoop *createEventLoop(int maxSize);
int createFileEvent(eventLoop *el, int fd, int mask, void *proc, void *clientData);
void deleteFileEvent(eventLoop *el, int fd, int mask);
int eventPoll(eventLoop *el, struct timeval *tvp);
void setBeforeSleepProc(eventLoop *el, beforeSleepProc *beforeSleep);
unsigned long createTimeEvent(eventLoop *el, long long milliseconds,
                              timeProc *proc, void *clientData,
                              eventFinalizerProc *finalizerProc);
int processEvents(eventLoop *el, int flags);
#endif //RESP_SERVER_EVENT_H
