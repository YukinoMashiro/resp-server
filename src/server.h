//
// Created by yukino on 2023/4/29.
//

#ifndef RESP_SERVER_SERVER_H
#define RESP_SERVER_SERVER_H

#include "event.h"
#include "adlist.h"
#include "connection.h"
#include "sds.h"
#include "object.h"
#include "dict.h"

#define DEFAULT_PORT 6379;
#define DEFAULT_BACKLOG 511;
#define MAX_CLIENT_LIMIT 10000;

/* Protocol and I/O related defines */
#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define LONG_STR_SIZE      21          /* Bytes needed for long -> str + '\0' */
#define REDIS_AUTOSYNC_BYTES (1024*1024*32) /* fdatasync every 32MB */

#define LIMIT_PENDING_QUERYBUF (4*1024*1024) /* 4mb */

/* Client request types */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2

#define NET_MAX_WRITES_PER_EVENT (1024*64)

#define C_OK                    0
#define C_ERR                   -1

#define UNUSED(V) ((void) V)

typedef struct client client;
typedef void commandProc(client *c);
typedef long long mstime_t; /* millisecond time type. */
typedef long long ustime_t; /* microsecond time type. */

typedef struct respCommand {
    // 命令名称，如SET、GET
    char *name;

    // 命令处理函数，负责执行命令的逻辑
    commandProc *proc;

    // 命令参数数量
    int arity;
}respCommand;

typedef struct respServer {
    eventLoop *el;
    int port;
    int ipFd;
    int tcpBacklog;
    int maxClient;
    list *clients;
    _Atomic uint64_t next_client_id;
    _Atomic size_t client_max_querybuf_len; /* Limit for client query buffer length */
    long long proto_max_bulk_len;   /* Protocol bulk length maximum size. */
    client *current_client;
    dict *commands;             /* Command table */
    int tcpkeepalive;
    list *clients_pending_write;
    list *clients_to_close;     /* Clients to close asynchronously */
    char *logfile;                  /* Path of log file */
    int verbosity;
    int syslog_enabled;
    time_t timezone;
    int daylight_active;        /* Currently in daylight saving time. */
    mstime_t mstime;            /* 'unixtime' in milliseconds. */
    ustime_t ustime;            /* 'unixtime' in microseconds. */
    _Atomic time_t unixtime;    /* Unix time sampled every cron cycle. */
}respServer;

typedef struct clientReplyBlock {
    size_t size, used;
    char buf[];
} clientReplyBlock;

struct client {
    uint64_t id;            /* Client incremental unique ID. */
    connection *conn;

    // 查询缓冲区，用于存放客户端请求数据
    sds querybuf;           /* Buffer we use to accumulate client queries. */

    // 查询缓冲区最新读取位置
    size_t qb_pos;          /* The position we have read in querybuf. */
    int argc;               /* Num of arguments of current command. */
    robj **argv;            /* Arguments of current command. */

    // 命令所有参数的长度,即所有参数的<length>之和
    size_t argv_len_sum;    /* Sum of lengths of objects in argv list. */

    struct respCommand *cmd;  /* Last command executed. */
    int reqtype;            /* Request protocol type: PROTO_REQ_* */

    // 当前解析的命令请求中尚未处理的命令参数数量，它是通过读取RESP协议中的<element-num>得到的
    // 每解析完命令的一个参数，值就减1
    int multibulklen;       /* Number of multi bulk arguments left to read. */

    // 当前读取命令的参数长度,即RESP格式 $<length>\r\n<data>\r\n 中的<length>,初始值为-1
    // 在读取参数时，才会被赋值
    long bulklen;           /* Length of bulk argument in multi bulk request. */

    // 链表回复缓冲区
    list *reply;            /* List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* Tot bytes of objects in reply list. */ /* 链表回复缓冲区的字节数 */
    size_t sentlen;         /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    listNode *client_list_node;

    /* Response buffer */
    int bufpos;/* 固定回复缓冲区的最新操作位置 */
    char buf[PROTO_REPLY_CHUNK_BYTES];/* 固定回复缓冲区 */
};

extern respServer server;
extern sharedObjectsStruct shared;

void addReplyError(client *c, const char *err);

#endif //RESP_SERVER_SERVER_H
