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

#define DEFAULT_PORT 2233;
#define DEFAULT_BACKLOG 511;
#define MAX_CLIENT_LIMIT 10000;

/* Protocol and I/O related defines */
#define PROTO_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PROTO_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PROTO_MBULK_BIG_ARG     (1024*32)
#define PROTO_MAX_BULK_LEN      (512ll*1024*1024)

#define DEFAULT_TCP_KEEPALIVE 300

#define MAX_ACCEPTS_PER_CALL 1000

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

    // 命令处理函数
    commandProc *proc;

    // 命令参数数量
    int arity;
}respCommand;

typedef struct respServer {
    // 配置类
    int port;                               /* 端口 */
    char *logfile;                          /* 日志文件路径 */
    int tcpBacklog;                         /* TCP连接请求等待队列长度 */
    int maxClient;                          /* 最大客户端数量 */
    _Atomic size_t client_max_querybuf_len; /* 最大请求缓冲区限度 */
    long long proto_max_bulk_len;           /* 最大RESP协议<length>限度 */
    int tcpkeepalive;                       /* TCP保活时间 */
    int verbosity;                          /* 日志等级 */

    // 其他类
    eventLoop *el;                          /* 事件循环定时器 */
    int ipFd;                               /* 服务端侦听套接字 */
    list *clients;                          /* 客户端链表 */
    _Atomic uint64_t next_client_id;        /* 下一个客户端ID */
    client *current_client;                 /* 当前连接客户端 */
    dict *commands;                         /* 已支持的命令表 */
    list *clients_pending_write;            /* 待回复客户端链表 */
    list *clients_to_close;                 /* 待异步释放客户端 */
    time_t timezone;                        /* 时区 */
    int daylight_active;
    mstime_t mstime;                        /* 以毫秒为单位的'unixtime' */
    ustime_t ustime;                        /* 以微秒为单位的'unixtime' */
    _Atomic time_t unixtime;
}respServer;

typedef struct clientReplyBlock {
    size_t size, used;
    char buf[];
} clientReplyBlock;

struct client {
    uint64_t id;                    /* 客户端ID */
    connection *conn;               /* 客户端关联的连接 */
    sds querybuf;                   /* 查询缓冲区，用于存放客户端请求数据 */
    size_t qb_pos;                  /* 查询缓冲区最新读取位置 */
    int argc;                       /* 当前命令参数数量 */
    robj **argv;                    /* 当前命令参数 */
    size_t argv_len_sum;            /* 命令所有参数的长度,即所有参数的<length>之和 */
    struct respCommand *cmd;        /* 当前执行命令 */
    int reqtype;                    /* R请求协议类型 */
    int multibulklen;               /* 当前命令尚未解析的参数个数 */
    long bulklen;                   /* resp协议个数'$<length>\r\n<data>\r\n'中的<length> */
    list *reply;                    /* 非固定回复缓冲区 */
    unsigned long long reply_bytes; /* 非固定回复缓冲区的字节数 */
    size_t sentlen;                 /* Amount of bytes already sent in the current
                                        buffer or object being sent. */
    listNode *client_list_node;
    int bufpos;                     /* 固定回复缓冲区的最新操作位置 */
    char buf[PROTO_REPLY_CHUNK_BYTES];  /* 固定回复缓冲区 */
};

extern respServer server;
extern sharedObjectsStruct shared;

void addReplyError(client *c, const char *err);

void RESP_INIT_OPTIONS(int port, char *logfile, respCommand *commandTab, int numCommand);

void RESP_LISTEN_EVENT();

#endif //RESP_SERVER_SERVER_H
