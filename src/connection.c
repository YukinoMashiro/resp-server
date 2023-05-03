//
// Created by yukino on 2023/5/1.
//

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
        //.accept = connSocketAccept,
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

/* Associate a private data pointer with the connection */
void connSetPrivateData(connection *conn, void *data) {
    conn->private_data = data;
}

/* Get the associated private data pointer */
void *connGetPrivateData(connection *conn) {
    return conn->private_data;
}

int connGetState(connection *conn) {
    return conn->state;
}

int netSetBlock(int fd, int non_block) {
    int flags;

    /* Set the socket blocking (if non_block is zero) or non-blocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    /* 获取fd的属性 */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        printf("fcntl(F_GETFL): %s.\r\n", strerror(errno));
        return C_ERR;
    }

    if (non_block)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    /* 设置fd的属性 */
    if (fcntl(fd, F_SETFL, flags) == -1) {
        printf("fcntl(F_SETFL,O_NONBLOCK): %s.\r\n", strerror(errno));
        return C_ERR;
    }
    return C_OK;
}

int netNonBlock(int fd) {
    return netSetBlock(fd,1);
}

int netBlock(int fd) {
    return netSetBlock(fd,0);
}

int connNonBlock(connection *conn) {
    if (conn->fd == -1) return C_ERR;
    return netNonBlock(conn->fd);
}

static int netSetTcpNoDelay(int fd, int val)
{
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == -1)
    {
        printf("setsockopt TCP_NODELAY: %s.\r\n", strerror(errno));
        return C_ERR;
    }
    return C_OK;
}

int netEnableTcpNoDelay(int fd)
{
    return netSetTcpNoDelay(fd, 1);
}

int netDisableTcpNoDelay(int fd)
{
    return netSetTcpNoDelay(fd, 0);
}

int connEnableTcpNoDelay(connection *conn) {
    if (conn->fd == -1) return C_ERR;
    return netEnableTcpNoDelay(conn->fd);
}

int netKeepAlive(int fd, int interval)
{
    int val = 1;

    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1)
    {
        printf("setsockopt SO_KEEPALIVE: %s.\r\n", strerror(errno));
        return C_ERR;
    }

#ifdef __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval. */
    val = interval;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPIDLE: %s.\r\n", strerror(errno));
        return C_ERR;
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    val = interval/3;
    if (val == 0) val = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPINTVL: %s\r\n", strerror(errno));
        return C_ERR;
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    val = 3;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
        printf("setsockopt TCP_KEEPCNT: %s\r\n", strerror(errno));
        return C_ERR;
    }
#else
    ((void) interval); /* Avoid unused var warning for non Linux systems. */
#endif

    return C_OK;
}

int connKeepAlive(connection *conn, int interval) {
    if (conn->fd == -1) return C_ERR;
    return netKeepAlive(conn->fd, interval);
}