//
// Created by yukino on 2023/5/1.
//

#ifndef RESP_SERVER_CONNECTION_H
#define RESP_SERVER_CONNECTION_H

#include <strings.h>

typedef struct connection connection;

typedef enum {
    CONN_STATE_NONE = 0,
    CONN_STATE_ACCEPTING,
    CONN_STATE_CONNECTED,
    CONN_STATE_CLOSED,
    CONN_STATE_ERROR
} ConnectionState;


typedef struct ConnectionType {
    int (*write)(struct connection *conn, const void *data, size_t data_len);
    int (*read)(struct connection *conn, void *buf, size_t buf_len);
    void (*close)(struct connection *conn);
    const char *(*get_last_error)(struct connection *conn);
} ConnectionType;

struct connection {
    ConnectionType *type;       /* 包含操作链接通道的函数，如connect、white、read */
    ConnectionState state;      /* 连接状态 */
    short int refs;
    int last_errno;             /* 该连接最新的errno */
    void *private_data;         /* 用于存放附加数据 */
    int fd;                     /* 数据套接字描述符 */
};

static inline int connWrite(connection *conn, const void *data, size_t data_len) {
    return conn->type->write(conn, data, data_len);
}

static inline int connRead(connection *conn, void *buf, size_t buf_len) {
    return conn->type->read(conn, buf, buf_len);
}

static inline void connClose(connection *conn) {
    conn->type->close(conn);
}

static inline const char *connGetLastError(connection *conn) {
    return conn->type->get_last_error(conn);
}

connection *connCreateAcceptedSocket(int fd);
void connSetPrivateData(connection *conn, void *data);
void *connGetPrivateData(connection *conn);
int connGetState(connection *conn);
int connNonBlock(connection *conn);
int connEnableTcpNoDelay(connection *conn);
int connKeepAlive(connection *conn, int interval);

#endif //RESP_SERVER_CONNECTION_H
