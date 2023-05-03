//
// Created by yukino on 2023/5/2.
//
#include <stdio.h>
#include "unistd.h"

#include "command.h"
#include "server.h"


void testComand(client *c){
    connection *conn = NULL;
    printf("client.querybuf=%s.\r\n", c->querybuf);
    conn = c->conn;
    write(conn->fd, "+OK\r\n", 5);
    //connClose(conn);
}

void commandCommand(client *c){
    connection *conn = NULL;
    printf("client.querybuf=%s.\r\n", c->querybuf);
    conn = c->conn;
    //connWrite(conn, "+OK\r\n", 5);
    printf("last client fd:%d\r\n", conn->fd);
    write(conn->fd, "+OK\r\n", 5);
    //connClose(conn);
}