//
// Created by yukino on 2023/4/29.
//

#include <malloc.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "server.h"
#include "error.h"
#include "connection.h"
#include "zmalloc.h"
#include "endianconv.h"
#include "util.h"
#include "dict.h"
#include "command.h"

respServer server;

respCommand commandTable[] = {
        {"test",testComand,-2},
        {"command", commandCommand, -1}
};

static int anetSetReuseAddr(int fd) {
    int yes = 1;
    /* Make sure connection-intensive things like the redis benchmark
     * will be able to close/open sockets a zillion of times */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        printf("setsockopt SO_REUSEADDR: %s\r\n", strerror(errno));
        return ERROR_FAILED;
    }
    return ERROR_SUCCESS;
}

int tcpServer(int port , int backlog) {
    int serverSocket = -1;
    struct sockaddr_in server_addr = {0};

    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        printf("create server socket failed.\r\n");
        return -1;
    }

    anetSetReuseAddr(serverSocket);

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
    /* Note that we remember the linked list node where the client is stored,
     * this way removing the client in unlinkClient() will not require
     * a linear scan, but just a constant time operation. */
    c->client_list_node = listLast(server.clients);
    uint64_t id = htonu64(c->id);
    //raxInsert(server.clients_index,(unsigned char*)&id,sizeof(id),c,NULL);
}

client *createClient(connection *conn) {
    client *c = zmalloc(sizeof(client));

    /* passing NULL as conn it is possible to create a non connected client.
     * This is useful since all the commands needs to be executed
     * in the context of a client. When commands are executed in other
     * contexts (for instance a Lua script) we need a non connected client. */

    // 如果conn参数为NULL，则创建伪客户端
    if (conn) {
        // 将文件描述符设置为非阻塞模式
        //connNonBlock(conn);

        // 关闭TCP的Delay选项
        //connEnableTcpNoDelay(conn);

        // 开启TCP的keepAlive选项，服务器定时向空闲客户端发送ACK进行探测
        //if (server.tcpkeepalive)
        //    connKeepAlive(conn,server.tcpkeepalive);
        //connSetReadHandler(conn, readQueryFromClient);

        // 将client赋值给conn.private_data，因此可以通过connection找到client
        connSetPrivateData(conn, c);
    }
    /* 选择0号数据库并初始化client属性 */
    //selectDb(c,0);
    uint64_t client_id = ++server.next_client_id;
    c->id = client_id;

    // 将connection与client关联，因此可以通过client找到connection
    c->conn = conn;
    c->bufpos = 0;
    c->qb_pos = 0;
    c->querybuf = sdsempty();
    c->reqtype = 0;
    c->argc = 0;
    c->argv = NULL;
    c->argv_len_sum = 0;
    c->cmd = c->lastcmd = NULL;
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

int processMultibulkBuffer(client *c) {
    char *newline = NULL;
    int ok;
    long long ll;

    // client.multibulklen == 0 代表上一个命令请求数据已解析完成，这里开始解析一个新的命令请求
    // 通过"\r\n"分割符从当前请求数据中解析当前命令的参数数量，并赋值给client.multibulklen
    if (c->multibulklen == 0) {
        /* The client should have been reset */
        //serverAssertWithInfo(c,NULL,c->argc == 0);
        //assert(c->argc == 0);
        /* Multi bulk length cannot be read without a \r\n */
        newline = strchr(c->querybuf+c->qb_pos,'\r');
        if (newline == NULL) {
            if (sdslen(c->querybuf)-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                //addReplyError(c,"Protocol error: too big mbulk count string");
                printf("too big mbulk count string.\r\n");
            }
            return C_ERR;
        }

        /* Buffer should also contain \n */
        if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(sdslen(c->querybuf)-c->qb_pos-2))
            return C_ERR;

        /* We know for sure there is a whole line since newline != NULL,
         * so go ahead and find out the multi bulk length. */
        //serverAssertWithInfo(c,NULL,c->querybuf[c->qb_pos] == '*');
        assert(c->querybuf[c->qb_pos] == '*');

        // 此时，c->qb_pos指向的内容一定为'*'
        // 命令的resp协议格式为"*<element-num>\r\n<element1>\r\n...<element2>\r\n"
        // newline指向第一个出现'\r'的位置
        // string2ll即获取<element-num>的值
        ok = string2ll(c->querybuf+1+c->qb_pos,newline-(c->querybuf+1+c->qb_pos),&ll);
        if (!ok || ll > 1024*1024) {
            //addReplyError(c,"Protocol error: invalid multibulk length");
            printf("invalid mbulk count\r\n.");
            return C_ERR;
        }

        // c->pos 指向<element1>
        c->qb_pos = (newline-c->querybuf)+2;

        if (ll <= 0) return C_OK;

        // 得到待解析的参数的个数
        c->multibulklen = ll;

        /* Setup argv array on client structure */
        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(robj*)*c->multibulklen);
        c->argv_len_sum = 0;
    }

    //serverAssertWithInfo(c,NULL,c->multibulklen > 0);
    assert(c->multibulklen > 0);

    // 读取当前命令的所有参数，client.multibulklen为当前解析的命令请求中尚未处理的命令参数的个数
    while(c->multibulklen) {
        /* Read bulk length if unknown */
        if (c->bulklen == -1) {
            newline = strchr(c->querybuf+c->qb_pos,'\r');
            if (newline == NULL) {
                if (sdslen(c->querybuf)-c->qb_pos > PROTO_INLINE_MAX_SIZE) {
                    //addReplyError(c,
                    //              "Protocol error: too big bulk count string");
                    printf("too big bulk count string.\r\n");
                    return C_ERR;
                }
                break;
            }

            /* Buffer should also contain \n */
            if (newline-(c->querybuf+c->qb_pos) > (ssize_t)(sdslen(c->querybuf)-c->qb_pos-2))
                break;

            // RESP格式 $<length>\r\n<data>\r\n
            if (c->querybuf[c->qb_pos] != '$') {
                //addReplyErrorFormat(c,
                //                    "Protocol error: expected '$', got '%c'",
                //                    c->querybuf[c->qb_pos]);
                printf("expected $ but got something else.\r\n");
                return C_ERR;
            }

            // 获取<length>的值并赋值给ll
            ok = string2ll(c->querybuf+c->qb_pos+1,newline-(c->querybuf+c->qb_pos+1),&ll);
            if (!ok || ll < 0 || ll > server.proto_max_bulk_len) {
                //addReplyError(c,"Protocol error: invalid bulk length");
                printf("invalid bulk length.\r\n");
                return C_ERR;
            }

            // 现在client.qb_pos指向<data>
            c->qb_pos = newline-c->querybuf+2;

            // 如果当前参数是个超大参数
            if (ll >= PROTO_MBULK_BIG_ARG) {
                /* If we are going to read a large object from network
                 * try to make it likely that it will start at c->querybuf
                 * boundary so that we can optimize object creation
                 * avoiding a large copy of data.
                 *
                 * But only when the data we have not parsed is less than
                 * or equal to ll+2. If the data length is greater than
                 * ll+2, trimming querybuf is just a waste of time, because
                 * at this time the querybuf contains not only our bulk. */

                // client.querybuf剩余的空间不足以容纳当前参数
                if (sdslen(c->querybuf)-c->qb_pos <= (size_t)ll+2) {

                    //清除查询缓冲区的其他参数（这些参数已处理），确保查询缓冲区只有当前参数
                    sdsrange(c->querybuf,c->qb_pos,-1);
                    c->qb_pos = 0;
                    /* Hint the sds library about the amount of bytes this string is
                     * going to contain. */

                    // 对查询缓冲区进行扩容，确保可以容纳当前参数
                    c->querybuf = sdsMakeRoomFor(c->querybuf,ll+2);
                }
            }

            // client.bulklen代表当前参数的长度
            c->bulklen = ll;
        }

        /* Read bulk argument */

        // 当前查询缓冲区字符串长度小于当前参数长度，说明当前参数并没有读取完整，退出函数，等待下次readQueryFromClient函数被调用后，
        // 继续读取剩余剩余数据
        if (sdslen(c->querybuf)-c->qb_pos < (size_t)(c->bulklen+2)) {
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            /* Optimization: if the buffer contains JUST our bulk element
             * instead of creating a new object by *copying* the sds we
             * just use the current sds string. */
            if (c->qb_pos == 0 &&
                c->bulklen >= PROTO_MBULK_BIG_ARG &&
                sdslen(c->querybuf) == (size_t)(c->bulklen+2))
            {
                // 如果读取的是超大参数，则直接使用查询缓冲区创建一个redisObject作为参数存放到client.argv中，
                // (该redisObject.ptr指向查询缓冲区)
                // 前面做了很多工作，确保读取超大参数时，查询缓冲区只有该参数数据
                c->argv[c->argc++] = createObject(OBJ_STRING,c->querybuf);
                c->argv_len_sum += c->bulklen;
                sdsIncrLen(c->querybuf,-2); /* remove CRLF */
                /* Assume that if we saw a fat argument we'll see another one
                 * likely... */

                // 申请新的内存空间作为查询缓冲区
                c->querybuf = sdsnewlen(SDS_NOINIT,c->bulklen+2);
                sdsclear(c->querybuf);
            } else {
                // 如果读取的不是非超大参数，则调用createStringObject赋值查询缓冲区中的数据并创建一个redisObject作为参数，
                // 存放到client.argv
                c->argv[c->argc++] =
                        createStringObject(c->querybuf+c->qb_pos,c->bulklen);
                c->argv_len_sum += c->bulklen;
                c->qb_pos += c->bulklen+2;
            }
            c->bulklen = -1;
            c->multibulklen--;
        }
    }

    /* We're done when c->multibulk == 0 */

    // client.multibulklen == 0 代表当前命令已读取完全，返回C_OK，这时返回processInputBuffer函数后会执行命令
    // 否则返回C_ERR，这时需要readQueryFromClient函数继续请求剩余数据
    if (c->multibulklen == 0) return C_OK;

    /* Still not ready to process the command */
    return C_ERR;
}

static void freeClientArgv(client *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        decrRefCount(c->argv[j]);
    c->argc = 0;
    c->cmd = NULL;
    c->argv_len_sum = 0;
}

void resetClient(client *c) {
    freeClientArgv(c);
    c->reqtype = 0;
    c->multibulklen = 0;
    c->bulklen = -1;
}

struct redisCommand *lookupCommand(sds name) {
    return dictFetchValue(server.commands, name);
}

int processCommand(client *c) {

    printf("client =====>\r\n");
    if (NULL == c->argv[0]) {
        printf("client is null\r\n");
    }

    // 使用命令名，从server.commands命令字典中查找对应的redisCommand，并检查参数数量是否满足命令要求
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr);
    if (!c->cmd) {
        sds args = sdsempty();
        int i;
        for (i=1; i < c->argc && sdslen(args) < 128; i++)
            args = sdscatprintf(args, "`%.*s`, ", 128-(int)sdslen(args), (char*)c->argv[i]->ptr);
        printf("unknown command `%s`, with args beginning with: %s.\r\n",
                            (char*)c->argv[0]->ptr, args);
        sdsfree(args);
        return C_OK;
    } else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
               (c->argc < -c->cmd->arity)) {
        printf("wrong number of arguments for '%s' command.\r\n",
                            c->cmd->name);
        return C_OK;
    }

    c->cmd->proc(c);

    return C_OK;
}

void processInputBuffer(client *c) {
    // client.qb_pos为查询缓冲区的最新读取位置，用于索引client.querybuf中的内容，该位置小于查询缓冲区内容的长度时，循环继续执行
    while(c->qb_pos < sdslen(c->querybuf)) {

        // 请求数据类型未确认，代表当前解析的是一个新的请求命令，因此在这里需要判断请求的数据类型
        // '*'开头，表明是PROTO_REQ_MULTIBULK类型，符合RESP协议
        // 非'*'开头，表明是PROTO_REQ_INLINE类型，并非RESP协议，为管道命令，用于支持telnet
        if (!c->reqtype) {
            if (c->querybuf[c->qb_pos] == '*') {
                c->reqtype = PROTO_REQ_MULTIBULK;
            } else {
                c->reqtype = PROTO_REQ_INLINE;
            }
        }

        if (c->reqtype == PROTO_REQ_MULTIBULK) {
            if (processMultibulkBuffer(c) != C_OK) break;
        } else {
            printf("Unknown request type.\r\n");
        }

        /* Multibulk processing could see a <= 0 length. */
        /* 如果参数数量为0，则直接重置客户端（主从同步等场景会发送空行请求） */
        if (c->argc == 0) {
            resetClient(c);
        } else {
            /* 执行命令 */
            server.current_client = c;
            if (processCommand(c) == C_ERR) {
                return;
            }
        }
    }

    /* Trim to pos */
    if (c->qb_pos) {
        sdsrange(c->querybuf,c->qb_pos,-1);
        c->qb_pos = 0;
    }
}

void readQueryFromClient(eventLoop *el, int fd, void *clientData, int mask) {
    connection *conn = (connection *)clientData;

    client *c = connGetPrivateData(conn);
    int nread, readlen;
    size_t qblen;

    // readlen为读取请求最大字节，默认为16KB
    readlen = PROTO_IOBUF_LEN;

    // c->multibulklen != 0 当前解析的命令请求中尚未处理的命令参数数量不为0，即代表发生了拆包，即上次并没有读取一个完整的命令请求
    // c->bulklen != -1，初始值为-1,在读取参数时才会被赋值，这里为0只存在与拆包情况
    // c->bulklen >= PROTO_MBULK_BIG_ARG 代表超大参数
    if (c->reqtype == PROTO_REQ_MULTIBULK && c->multibulklen && c->bulklen != -1
        && c->bulklen >= PROTO_MBULK_BIG_ARG)
    {
        // 拆包情况，非第一次，参数剩余未读取的长度 +2 ？？？
        // +2是指 "\r\n"的长度
        // 因为拆包的原因，应该把参数长度多出缓冲区的部分，重新赋值为要读取的长度
        ssize_t remaining = (size_t)(c->bulklen+2)-sdslen(c->querybuf);

        /* Note that the 'remaining' variable may be zero in some edge case,
         * for example once we resume a blocked client after CLIENT PAUSE. */
        if (remaining > 0 && remaining < readlen) readlen = remaining;
    }

    // 需要注意，命令第一次进入这里，client.querybuf是没有没有数据的，需要后面读取了数据后才有内容
    // 如果说，这里有内容，说明一定发生了拆包
    qblen = sdslen(c->querybuf);

    // 为client.querybuf扩容，保证其可用内存不小于readlen
    c->querybuf = sdsMakeRoomFor(c->querybuf, readlen);

    // 从socket读取数据，返回实际读取字节数
    // c->querybuf+qblen 读取的数据存储到c->querybuf的空白位置，说明c->querybuf保存的应该是多次读取的结果
    // 此时c->querybuf读取了命令的数据
    nread = connRead(c->conn, c->querybuf+qblen, readlen);
    if (nread == -1) {
        if (connGetState(conn) == CONN_STATE_CONNECTED) {
            return;
        } else {
            printf("Reading from client: %s.\r\n",connGetLastError(c->conn));
            //freeClientAsync(c);
            close(c->conn->fd);
            return;
        }
        // 对于非超大规格参数的情况，会有两次进入readQueryFromClient，第一次正常读取参数，第二次走这里。
    } else if (nread == 0) {
        printf("Client closed connection.\r\n");
        //freeClientAsync(c);
        close(c->conn->fd);
        return;
    }

    // 因c->querybuf为sds结构，因此更新sds结构的len属性
    sdsIncrLen(c->querybuf,nread);

    if (sdslen(c->querybuf) > server.client_max_querybuf_len) {
        printf("Closing client that reached max query buffer length.\r\n");
        //freeClientAsync(c);
        close(c->conn->fd);
        return;
    }

    // 处理读取的数据
    processInputBuffer(c);
}

void acceptTcpHandler(eventLoop *el, int fd, void *clientData, int mask) {
    socklen_t saSize = 0;
    struct sockaddr_in sa;
    int clientFd = -1;
    connection * conn = NULL;
    client *c = NULL;

    saSize = sizeof(sa);
    clientFd = accept(fd, (struct sockaddr*)&sa, &saSize);
    printf("connected client: %d\r\n", clientFd);

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
        printf("Error registering fd event for the new client: %s\r\n", connGetLastError(conn));
        connClose(conn);
        return;
    }

    printf("client fd:%d\r\n", clientFd);

    createFileEvent(el, clientFd, EVENT_READABLE, readQueryFromClient, conn);
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
    error = createFileEvent(server.el, server.ipFd, EVENT_READABLE, acceptTcpHandler, NULL);
    if (ERROR_SUCCESS != error) {
        printf("failed to create server.ipFd file event.\r\n");
        return;
    }

}

uint64_t dictSdsCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
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
        NULL,                       /* key dup */
        NULL,                       /* val dup */
        dictSdsKeyCaseCompare,      /* key compare */
        dictSdsDestructor,          /* key destructor */
        NULL                        /* val destructor */
};

/* Populates the Redis Command Table starting from the hard coded list
 * we have on top of server.c file. */
void populateCommandTable(void) {
    int j;
    int numcommands = sizeof(commandTable) / sizeof(struct respCommand);

    for (j = 0; j < numcommands; j++) {
        struct respCommand *c = commandTable+j;
        int retval = dictAdd(server.commands, sdsnew(c->name), c);
        assert(retval == DICT_OK);
    }
}

void initConf() {
    server.el = NULL;
    server.port = DEFAULT_PORT;
    server.ipFd = -1;
    server.tcpBacklog = DEFAULT_BACKLOG;
    server.maxClient = MAX_CLIENT_LIMIT;
    server.clients = listCreate();
    server.next_client_id = 1;
    server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    server.proto_max_bulk_len = 512ll*1024*1024;
    server.current_client = NULL;
    server.commands = dictCreate(&commandTableDictType,NULL);
    populateCommandTable();
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
                fe->rFileProc(el, fd, fe->clientData, mask);
            }
            if (fe->mask & mask & EVENT_WRITABLE) {
                fe->wFileProc(el, fd, fe->clientData, mask);
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