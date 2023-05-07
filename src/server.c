//
// Created by yukino on 2023/4/29.
//
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include "server.h"
#include "error.h"
#include "connection.h"
#include "zmalloc.h"
#include "endianconv.h"
#include "util.h"
#include "dict.h"
#include "command.h"
#include "reply.h"
#include "log.h"

respServer server;
sharedObjectsStruct shared;

void populateCommandTable(respCommand *commandTab, int numCommands) {
    int j;

    for (j = 0; j < numCommands; j++) {
        struct respCommand *c = commandTab + j;
        int retVal = dictAdd(server.commands, sdsnew(c->name), c);
        serverAssert(retVal == DICT_OK);
    }
}

void createSharedObjects(void) {
    shared.crlf = createObject(OBJ_STRING, sdsnew("\r\n"));
    shared.ok = createObject(OBJ_STRING, sdsnew("+OK\r\n"));
    shared.err = createObject(OBJ_STRING, sdsnew("-ERR\r\n"));
    shared.pong = createObject(OBJ_STRING,sdsnew("+PONG\r\n"));
}

void freeClientReplyValue(void *o) {
    zfree(o);
}

void *dupClientReplyValue(void *o) {
    clientReplyBlock *old = o;
    clientReplyBlock *buf = zmalloc(sizeof(clientReplyBlock) + old->size);
    memcpy(buf, o, sizeof(clientReplyBlock) + old->size);
    return buf;
}

void linkClient(client *c) {
    listAddNodeTail(server.clients,c);
    c->client_list_node = listLast(server.clients);
}

unsigned long processMultibulkBuffer(client *c) {
    char *newline = NULL;
    int ok;
    long long ll;

    /* multibulklen == 0，代表上一个命令请求数据已解析完成，这里开始解析一个新的命令请求 */
    if (c->multibulklen == 0) {
        serverAssert(c->argc == 0);
        newline = strchr(c->querybuf+c->qb_pos,'\r');
        if (newline == NULL) {
            if (sdslen(c->querybuf)-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                addReplyError(c,"Protocol error: too big mbulk count string");
                serverLog(LL_WARNING, "too big mbulk count string.");
            }
            return ERROR_FAILED;
        }
        if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(sdslen(c->querybuf)-c->qb_pos-2))
            return ERROR_FAILED;

        serverAssert(c->querybuf[c->qb_pos] == '*');

        /*
         * resp协议格式为"*<element-num>\r\n<element1>\r\n...<element2>\r\n"
         * string2ll即获取<element-num>的值
         */
        ok = string2ll(c->querybuf+1+c->qb_pos,newline-(c->querybuf+1+c->qb_pos),&ll);
        if (!ok || ll > 1024*1024) {
            addReplyError(c,"Protocol error: invalid multibulk length");
            serverLog(LL_WARNING,"invalid multibulk count.");
            return ERROR_FAILED;
        }

        c->qb_pos = (newline-c->querybuf)+2;

        if (ll <= 0) return ERROR_SUCCESS;

        c->multibulklen = ll;

        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(robj*)*c->multibulklen);
        c->argv_len_sum = 0;
    }

    serverAssert(c->multibulklen > 0);

    /* 读取当前命令的所有参数 */
    while(c->multibulklen) {
        if (c->bulklen == -1) {
            newline = strchr(c->querybuf+c->qb_pos,'\r');
            if (newline == NULL) {
                if (sdslen(c->querybuf)-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                    addReplyError(c,
                                  "Protocol error: too big bulk count string");
                    serverLog(LL_WARNING,"too big bulk count string.");
                    return ERROR_FAILED;
                }
                break;
            }

            if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(sdslen(c->querybuf)-c->qb_pos-2))
                break;

            /* RESP格式 "$<length>\r\n<data>\r\n" */
            if (c->querybuf[c->qb_pos] != '$') {
                addReplyErrorFormat(c,
                                    "Protocol error: expected '$', got '%c'",
                                    c->querybuf[c->qb_pos]);
                serverLog(LL_WARNING,"expected $ but got something else.");
                return ERROR_FAILED;
            }

            /*
             * 1. RESP格式 "$<length>\r\n<data>\r\n"
             * 2. string2ll获取<length>
             */
            ok = string2ll(c->querybuf+c->qb_pos+1,newline-(c->querybuf+c->qb_pos+1),&ll);
            if (!ok || ll < 0 || ll > server.proto_max_bulk_len) {
                addReplyError(c,"Protocol error: invalid bulk length");
                serverLog(LL_WARNING,"invalid bulk length.");
                return ERROR_FAILED;
            }

            c->qb_pos = newline-c->querybuf+2;

            /* 当前参数是超大参数 */
            if (ll >= PROTO_MBULK_BIG_ARG) {

                /* querybuf剩余的空间不足以容纳当前参数 */
                if (sdslen(c->querybuf)-c->qb_pos <= (size_t)ll+2) {

                    /* 清除查询缓冲区的其他参数（这些参数已处理），确保查询缓冲区只有当前参数 */
                    sdsrange(c->querybuf,c->qb_pos,-1);
                    c->qb_pos = 0;

                    /* 对查询缓冲区进行扩容，确保可以容纳当前参数 */
                    c->querybuf = sdsMakeRoomFor(c->querybuf,ll+2);
                }
            }

            c->bulklen = ll;
        }

        /*
         * 当前查询缓冲区字符串长度小于当前参数长度，说明当前参数并没有读取完整，退出函数。
         * 等待下次readQueryFromClient函数被调用后，继续读取剩余剩余数据。
         */
        if (sdslen(c->querybuf)-c->qb_pos < (size_t)(c->bulklen+2)) {
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            if (c->qb_pos == 0 &&
                c->bulklen >= PROTO_MBULK_BIG_ARG &&
                sdslen(c->querybuf) == (size_t)(c->bulklen+2))
            {
                /*
                 * 如果读取的是超大参数，则直接使用查询缓冲区创建一个redisObject作为参数存放到client.argv中，
                 * 该redisObject.ptr指向查询缓冲区，前面做了很多工作，确保读取超大参数时，查询缓冲区只有该参数数据
                 */
                c->argv[c->argc++] = createObject(OBJ_STRING,c->querybuf);
                c->argv_len_sum += c->bulklen;

                /* 清除\r\n */
                sdsIncrLen(c->querybuf,-2);


                /* 申请新的内存空间作为查询缓冲区 */
                c->querybuf = sdsnewlen(SDS_NOINIT,c->bulklen+2);
                sdsclear(c->querybuf);
            } else {
                /*
                 * 如果读取的不是非超大参数，则调用createStringObject赋值查询缓冲区中的数据并创建一个redisObject作为参数
                 * 存放到client.argv
                 */
                c->argv[c->argc++] =
                        createStringObject(c->querybuf+c->qb_pos,c->bulklen);
                c->argv_len_sum += c->bulklen;
                c->qb_pos += c->bulklen+2;
            }
            c->bulklen = -1;
            c->multibulklen--;
        }
    }

    /*
     * client.multibulklen == 0 代表当前命令已读取完全，返回C_OK，这时返回processInputBuffer函数后会执行命令
     * 否则返回C_ERR，这时需要readQueryFromClient函数继续请求剩余数据
     */
    if (c->multibulklen == 0) return ERROR_SUCCESS;

    return ERROR_FAILED;
}

static void freeClientArgv(client *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        decrRefCount(c->argv[j]);
    c->argc = 0;
    c->cmd = NULL;
    c->argv_len_sum = 0;
}

struct respCommand *lookupCommand(sds name) {
    return dictFetchValue(server.commands, name);
}

void resetClient(client *c) {
    freeClientArgv(c);
    c->reqtype = 0;
    c->multibulklen = 0;
    c->bulklen = -1;
}

long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

void updateCachedTime(int update_daylight_info) {
    server.ustime = ustime();
    server.mstime = server.ustime / 1000;
    server.unixtime = server.mstime / 1000;

    if (update_daylight_info) {
        struct tm tm;
        time_t ut = server.unixtime;
        localtime_r(&ut,&tm);
        server.daylight_active = tm.tm_isdst;
    }
}

/**
 * 执行命令
 * @param c
 */
void processCommand(client *c) {
    int isProc = 1;

    /* c->argv[0]->ptr表示command name */
    c->cmd = lookupCommand(c->argv[0]->ptr);
    if (!c->cmd) {
        sds args = sdsempty();
        int i;
        for (i=1; i < c->argc && sdslen(args) < 128; i++) {
            args = sdscatprintf(args, "`%.*s`, ", 128-(int)sdslen(args), (char*)c->argv[i]->ptr);
        }
        addReplyErrorFormat(c, "unknown command `%s`, with args beginning with: %s\r\n",
                            (char*)c->argv[0]->ptr, args);
        sdsfree(args);
        isProc = 0;
    } else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
               (c->argc < -c->cmd->arity)) {
        serverLog(LL_WARNING, "wrong number of arguments for '%s' command.", c->cmd->name);
        isProc = 0;
    }

    if (isProc) {
        c->cmd->proc(c);
    }
    updateCachedTime(0);
    resetClient(c);
}

void unlinkClient(client *c) {

    if (server.current_client == c) {
        server.current_client = NULL;
    }

    if (c->conn) {
        /* Remove from the list of active clients. */
        if (c->client_list_node) {
            listDelNode(server.clients,c->client_list_node);
            c->client_list_node = NULL;
        }
        connClose(c->conn);
        c->conn = NULL;
    }
}

void freeClient(client *c) {
    sdsfree(c->querybuf);
    c->querybuf = NULL;
    listRelease(c->reply);
    freeClientArgv(c);
    unlinkClient(c);
    zfree(c->argv);
    c->argv_len_sum = 0;
    zfree(c);
}

int setReuseAddr(int fd) {
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        serverLog(LL_WARNING, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        return ERROR_FAILED;
    }
    return ERROR_SUCCESS;
}

int tcpServer(int port , int backlog) {
    int serverSocket;
    struct sockaddr_in server_addr = {0};

    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        serverLog(LL_WARNING, "create server socket failed.");
        return -1;
    }

    if (ERROR_SUCCESS != setReuseAddr(serverSocket)) {
        close(serverSocket);
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (-1 == bind(serverSocket, (struct sockaddr*)&server_addr, sizeof(server_addr))) {
        serverLog(LL_WARNING, "bind server socket failed.");
        return -1;
    }

    if (-1 == listen(serverSocket, backlog)) {
        serverLog(LL_WARNING, "listen server socket failed.");
        return -1;
    }
    netNonBlock(serverSocket);
    return serverSocket;
}

uint64_t dictSdsCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

int dictSdsKeyCaseCompare(void *privdata, const void *key1,
                          const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

/* Command table. sds string -> command struct pointer. */
dictType commandTableDictType = {
        dictSdsCaseHash,            /* hash function */
        NULL,                           /* key dup */
        NULL,                           /* val dup */
        dictSdsKeyCaseCompare,      /* key compare */
        dictSdsDestructor,         /* key destructor */
        NULL                        /* val destructor */
};

void initDefaultOptions() {
    server.port = DEFAULT_PORT;
    server.logfile = "\0";
    server.tcpBacklog = DEFAULT_BACKLOG;
    server.maxClient = MAX_CLIENT_LIMIT;
    server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    server.proto_max_bulk_len = PROTO_MAX_BULK_LEN;
    server.tcpkeepalive = DEFAULT_TCP_KEEPALIVE;
    server.verbosity = LL_NOTICE;
    server.syslog_enabled = 0;
}

void initServerAttr() {
    updateCachedTime(1);
    server.el = NULL;
    server.ipFd = -1;
    server.clients = listCreate();
    server.next_client_id = 1;
    server.current_client = NULL;
    server.commands = dictCreate(&commandTableDictType,NULL);
    server.clients_pending_write = listCreate();
    server.clients_to_close = listCreate();
    server.timezone = getTimeZone();
}

/**
 * 处理客户端请求缓冲区数据
 * @param c
 */
void processInputBuffer(client *c) {
    while(c->qb_pos < sdslen(c->querybuf)) {
        /*
         * 1. !c->reqtype即客户端数据类型未确认，当前解析的是一个新的请求命令
         * 2. '*'开头，表明是PROTO_REQ_MULTIBULK类型，符合RESP协议
         * 3. 非'*'开头，表明是PROTO_REQ_INLINE类型，并非RESP协议，为管道命令，用于支持telnet，暂不支持
         */
        if (!c->reqtype) {
            if (c->querybuf[c->qb_pos] == '*') {
                c->reqtype = PROTO_REQ_MULTIBULK;
            } else {
                c->reqtype = PROTO_REQ_INLINE;
            }
        }

        if (c->reqtype == PROTO_REQ_MULTIBULK) {
            if (processMultibulkBuffer(c) != ERROR_SUCCESS) break;
        } else {
            serverLog(LL_WARNING, "Unknown request type.");
        }

        /* 执行命令 */
        server.current_client = c;
        processCommand(c);
    }

    /* Trim to pos */
    if (c->qb_pos) {
        sdsrange(c->querybuf,c->qb_pos,-1);
        c->qb_pos = 0;
    }
}

/**
 * 读取客户端发送的数据
 * @param el
 * @param fd
 * @param clientData
 * @param mask
 */
void readQueryFromClient(eventLoop *el, int fd, void *clientData, int mask) {
    connection *conn = (connection *)clientData;

    assert(NULL != conn);

    client *c = connGetPrivateData(conn);
    int nread, readlen;
    size_t qblen;

    /* 读取请求最大字节，默认为16KB */
    readlen = PROTO_IOBUF_LEN;

    /*
     * 1. multibulklen !=0 ==> 当前解析的命令请求中尚未处理的命令参数数量不为0，即代表发生了拆包
     * 2. bulklen != -1    ==> 初始值为-1，发生拆包时不为-1
     * 3. bulklen >= PROTO_MBULK_BIG_ARG ==> 超大参数
     */
    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG)
    {
        /*
         * 1. 拆包情况
         * 2. +2是指 "\r\n"的长度
         * 3. 因为拆包的原因，应该把参数长度多出缓冲区的部分，重新赋值为要读取的长度
         */
        ssize_t remaining = (size_t)(c->bulklen+2)-sdslen(c->querybuf);

        if (remaining > 0 && remaining < readlen) {
            readlen = remaining;
        }
    }

    /*
     * 1. 当初次读取客户端的数据时，querybuf没有数据，需要后面读取数据套接字内容后才有数据
     * 2. 如果此处querybuf有数据，说明一定发生了拆包
     */
    qblen = sdslen(c->querybuf);

    /* 为querybuf扩容，保证其可用内存不小于readlen */
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);

    nread = connRead(c->conn, c->querybuf+qblen, readlen);
    if (nread == -1) {
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            return;
        } else {
            serverLog(LL_WARNING, "Reading from client: %s.", connGetLastError(c->conn));
            connClose(c->conn);
            return;
        }
    } else if (nread == 0) {
        serverLog(LL_NOTICE, "Client closed connection.");
        connClose(c->conn);
        return;
    }

    /* 因querybuf为sds结构，更新sds结构的len属性 */
    sdsIncrLen(c->querybuf,nread);

    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        serverLog(LL_WARNING, "Closing client that reached max query buffer length.");
        connClose(c->conn);
        return;
    }

    /* 解析读取的数据 */
    processInputBuffer(c);
}

client *createClient(connection *conn) {
    client *c = zmalloc(sizeof(client));
    if (NULL == c) {
        serverLog(LL_WARNING, "malloc client failed.");
        return NULL;
    }

    if (conn) {
        /* 将文件描述符设置为非阻塞模式 */
        connNonBlock(conn);

        /* 关闭TCP的Delay选项 */
        connEnableTcpNoDelay(conn);

        /* 开启TCP的keepAlive选项，服务器定时向空闲客户端发送ACK进行探测 */
        if (server.tcpkeepalive) {
            connKeepAlive(conn,server.tcpkeepalive);
        }

        connSetPrivateData(conn, c);
    }
    uint64_t client_id = ++server.next_client_id;
    c->id = client_id;
    c->conn = conn;
    c->bufpos = 0;
    c->qb_pos = 0;
    c->querybuf = sdsempty();
    c->reqtype = 0;
    c->argc = 0;
    c->argv = NULL;
    c->argv_len_sum = 0;
    c->cmd = NULL;
    c->multibulklen = 0;
    c->bulklen = -1;
    c->sentlen = 0;
    c->reply = listCreate();
    c->reply_bytes = 0;
    c->client_list_node = NULL;
    listSetFreeMethod(c->reply,freeClientReplyValue);
    listSetDupMethod(c->reply,dupClientReplyValue);
    if (conn) linkClient(c);
    return c;
}

void acceptTcpHandler(eventLoop *el, int fd, void *clientData, int mask) {
    socklen_t saSize;
    struct sockaddr_in sa;
    int clientFd;
    connection * conn = NULL;
    client *c = NULL;
    int max = MAX_ACCEPTS_PER_CALL;

    UNUSED(clientData);
    UNUSED(mask);

    /* 每次事件循环中最多接收1000个客户请求，防止短时间内处理过多客户请求导致进程阻塞 */
    while(max--) {
        saSize = sizeof(sa);
        clientFd = accept(fd, (struct sockaddr*)&sa, &saSize);
        if (-1 == clientFd) {
            if (errno != EWOULDBLOCK) {
                serverLog(LL_WARNING,
                          "Accepting client connection: %s", strerror(errno));
            }
            return;
        }

        conn = connCreateAcceptedSocket(clientFd);

        if (listLength(server.clients) >= server.maxClient) {
            char *err= "-ERR max number of clients reached.\r\n";
            if (connWrite(conn,err,strlen(err)) == -1) {
                /* Nothing to do, Just to avoid the warning... */
            }
            connClose(conn);
            return;
        }

        c = createClient(conn);
        if (NULL == c) {
            serverLog(LL_WARNING, "Error registering fd event for the new client: %s.", connGetLastError(conn));
            connClose(conn);
            return;
        }

        createFileEvent(el, clientFd, EVENT_READABLE, readQueryFromClient, conn);
    }

}

int serverCron(struct eventLoop *el, long long id, void *clientData) {
    UNUSED(el);
    UNUSED(id);
    UNUSED(clientData);

    /* Update the time cache. */
    updateCachedTime(1);
    return EVENT_NOMORE;
}

void freeClientAsync(client *c) {
    listAddNodeTail(server.clients_to_close,c);
}

int writeToClient(client *c, int handler_installed) {
    ssize_t nwritten = 0, totwritten = 0;
    size_t objlen;
    clientReplyBlock *o;

    while(clientHasPendingReplies(c)) {

        /* 固定缓冲区写入TCP */
        if (c->bufpos > 0) {
            nwritten = connWrite(c->conn,c->buf+c->sentlen,c->bufpos-c->sentlen);
            if (nwritten <= 0) break;
            c->sentlen += nwritten;
            totwritten += nwritten;

            if ((int)c->sentlen == c->bufpos) {
                c->bufpos = 0;
                c->sentlen = 0;
            }
        } else {

            /* 非固定回复缓冲区写入TCP */
            o = listNodeValue(listFirst(c->reply));
            objlen = o->used;

            if (objlen == 0) {
                c->reply_bytes -= o->size;
                listDelNode(c->reply,listFirst(c->reply));
                continue;
            }

            nwritten = connWrite(c->conn, o->buf + c->sentlen, objlen - c->sentlen);
            if (nwritten <= 0) break;
            c->sentlen += nwritten;
            totwritten += nwritten;

            /* If we fully sent the object on head go to the next one */
            if (c->sentlen == objlen) {
                c->reply_bytes -= o->size;
                listDelNode(c->reply,listFirst(c->reply));
                c->sentlen = 0;
                /* If there are no longer objects in the list, we expect
                 * the count of reply bytes to be exactly zero. */
                if (listLength(c->reply) == 0)
                    assert(c->reply_bytes == 0);
            }
        }

        /* 在单线程服务器中, 避免发送超过NET_MAX_WRITES_PER_EVENT字节，为其他客户端提供服务 */
        if (totwritten > NET_MAX_WRITES_PER_EVENT) {
            break;
        }
    }
    if (nwritten == -1) {
        if (connGetState(c->conn) == CONN_STATE_CONNECTED) {
            nwritten = 0;
        } else {
            serverLog(LL_WARNING, "Error writing to client: %s.", connGetLastError(c->conn));
            freeClientAsync(c);
            return C_ERR;
        }
    }
    if (!clientHasPendingReplies(c)) {
        c->sentlen = 0;
        if (handler_installed) {
            deleteFileEvent(server.el, c->conn->fd, EVENT_READABLE);
        }

    }
    return C_OK;
}

void sendReplyToClient(connection *conn) {
    client *c = connGetPrivateData(conn);
    writeToClient(c,1);
}

void handleClientsWithPendingWrites(void) {
    listIter li;
    listNode *ln;
    unsigned long errorCode;

    int processed = listLength(server.clients_pending_write);
    if (processed == 0) {
        return;
    }

    listRewind(server.clients_pending_write,&li);
    while((ln = listNext(&li))) {
        client *c = listNodeValue(ln);
        listDelNode(server.clients_pending_write,ln);

        /* 将client回复缓冲区内容写入TCP发送缓冲区 */
        if (writeToClient(c,0) == C_ERR) continue;

        /*
         * 如果client回复缓冲区还有数据，则说明client回复缓冲区的内容过多，无法一次性写到TCP缓冲区中，
         * 这时要为当前连接注册监听WRITEABLE类型的文件事件，事件回调为sendReplyToClient，
         * 等到TCP发送缓冲区可写后，该函数负责继续写入数据
         */
        if (clientHasPendingReplies(c)) {
            errorCode = createFileEvent(server.el,
                                        c->conn->fd,
                                        EVENT_WRITABLE,
                                        sendReplyToClient,
                                        NULL);
            if (errorCode != ERROR_SUCCESS) {
                freeClientAsync(c);
            }
        }
    }
}

int freeClientsInAsyncFreeQueue(void) {
    int freed = 0;
    listIter li;
    listNode *ln;

    listRewind(server.clients_to_close,&li);
    while ((ln = listNext(&li)) != NULL) {
        client *c = listNodeValue(ln);
        freeClient(c);
        listDelNode(server.clients_to_close,ln);
        freed++;
    }
    return freed;
}

void beforeSleep(struct eventLoop *el) {
    UNUSED(el);

    /* 回复缓冲数据写入数据套接字 */
    handleClientsWithPendingWrites();

    /* 异步释放client */
    freeClientsInAsyncFreeQueue();
}

void initServer(respCommand *commandTab, int numCommands) {
    unsigned long error;

    /* 创建共享数据集 */
    createSharedObjects();

    /* 加载可用命令 */
    populateCommandTable(commandTab, numCommands);

    /* 创建事件循环器 */
    server.el = createEventLoop(server.maxClient);
    if (NULL == server.el) {
        return;
    }

    /* 创建时间事件 */
    error = createTimeEvent(server.el, 1, serverCron, NULL, NULL);
    if (ERROR_SUCCESS != error) {
        serverPanic("Can't create event loop timers.");
    }

    /* 开启TCP服务侦听，接收客户端请求 */
    if (0 != server.port) {
        server.ipFd = tcpServer(server.port, server.tcpBacklog);
    }

    /* 注册epoll事件 */
    error = createFileEvent(server.el, server.ipFd, EVENT_READABLE, acceptTcpHandler, NULL);
    if (ERROR_SUCCESS != error) {
        serverPanic("Unrecoverable error creating server.ipFd file event.");
    }

    /* 注册事件循环器的钩子函数 */
    setBeforeSleepProc(server.el, beforeSleep);

}

void eventMain() {
    eventLoop *el = server.el;
    while(1) {
        (void)processEvents(el, EVENT_ALL_EVENTS | EVENT_CALL_BEFORE_SLEEP);
    }
}


void RESP_INIT_OPTIONS(int port, char *logfile, respCommand *commandTab, int numCommand) {
    serverAssert(logfile != NULL);
    serverAssert(commandTab != NULL);

    initDefaultOptions();
    server.port = port;
    server.logfile = logfile;
    initServerAttr();
    initServer(commandTab, numCommand);
}

void RESP_LISTEN_EVENT() {
    eventMain();
}