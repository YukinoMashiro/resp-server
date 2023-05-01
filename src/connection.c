//
// Created by yukino on 2023/5/1.
//

#include <unistd.h>
#include <errno.h>
#include "connection.h"
#include "zmalloc.h"
#include "event.h"
#include "server.h"

ConnectionType CT_Socket;

static inline void connIncrRefs(connection *conn) {
    conn->refs++;
}

static inline void connDecrRefs(connection *conn) {
    conn->refs--;
}

static inline int connHasRefs(connection *conn) {
    return conn->refs;
}

static void connSocketClose(connection *conn) {
    if (conn->fd != -1) {
        deleteFileEvent(server.el,conn->fd,EVENT_READABLE);
        deleteFileEvent(server.el,conn->fd,EVENT_WRITABLE);
        close(conn->fd);
        conn->fd = -1;
    }
    zfree(conn);
}

static int connSocketWrite(connection *conn, const void *data, size_t data_len) {
    int ret = write(conn->fd, data, data_len);
    if (ret < 0 && errno != EAGAIN) {
        conn->last_errno = errno;

        /* Don't overwrite the state of a connection that is not already
         * connected, not to mess with handler callbacks.
         */
        if (conn->state == CONN_STATE_CONNECTED)
            conn->state = CONN_STATE_ERROR;
    }

    return ret;
}

static int connSocketRead(connection *conn, void *buf, size_t buf_len) {
    int ret = read(conn->fd, buf, buf_len);
    if (!ret) {
        conn->state = CONN_STATE_CLOSED;
    } else if (ret < 0 && errno != EAGAIN) {
        conn->last_errno = errno;

        /* Don't overwrite the state of a connection that is not already
         * connected, not to mess with handler callbacks.
         */
        if (conn->state == CONN_STATE_CONNECTED)
            conn->state = CONN_STATE_ERROR;
    }

    return ret;
}

static const char *connSocketGetLastError(connection *conn) {
    return strerror(conn->last_errno);
}

ConnectionType CT_Socket = {
        .close = connSocketClose,
        .write = connSocketWrite,
        .read = connSocketRead,
        .accept = connSocketAccept,
        .get_last_error = connSocketGetLastError
};

connection *connCreateSocket() {
    connection *conn = zcalloc(sizeof(connection));
    conn->type = &CT_Socket;
    conn->fd = -1;

    return conn;
}

connection *connCreateAcceptedSocket(int fd) {
    connection *conn = connCreateSocket();
    conn->fd = fd;
    conn->state = CONN_STATE_ACCEPTING;
    return conn;
}