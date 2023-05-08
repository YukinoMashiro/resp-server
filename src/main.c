//
// Created by yukino on 2023/5/7.
//

#include "server.h"
#include "reply.h"
#include "log.h"

void commandCommand(client *c){
    addReply(c, shared.ok);
}

void pingCommand(client *c) {
    addReply(c, shared.pong);
}

void setCommand(client *c) {
    int j;

    /* 打印命令行参数 */
    for (j = 0; j < c->argc; j++) {
        serverLog(LL_WARNING, "argv[%d]: %s", j, c->argv[j]->ptr);
    }

    /* 向客户端回复字符串 */
    addReplyBulk(c, createObject(OBJ_STRING,sdsnew(
            "set command")));
}

void getCommand(client *c) {
    addReplyBulk(c, createObject(OBJ_STRING,sdsnew(
            "get command")));
}

respCommand commandTable[] = {
        {"command", commandCommand, -1},
        {"ping", pingCommand, 0},
        {"set",setCommand,0},
        {"get", getCommand, 0},
};

int main(int argc, char **argv) {

    RESP_INIT_OPTIONS(2234,
                      "\0",
                      commandTable,
                      sizeof(commandTable) / sizeof(struct respCommand));
    RESP_LISTEN_EVENT();
    return 0;
}