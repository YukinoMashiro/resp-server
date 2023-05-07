//
// Created by yukino on 2023/5/7.
//

#include "server.h"
#include "reply.h"

void testCommand(client *c){
    addReply(c, shared.ok);
}

void commandCommand(client *c){
    addReply(c, shared.ok);
}

void pingCommand(client *c) {
    addReply(c, shared.pong);
}

respCommand commandTable[] = {
        {"test",testCommand,0},
        {"command", commandCommand, -1},
        {"ping", pingCommand, 0},
};

int main(int argc, char **argv) {

    RESP_INIT_OPTIONS(2234,
                      "\0",
                      commandTable,
                      sizeof(commandTable) / sizeof(struct respCommand));
    RESP_LISTEN_EVENT();
    return 0;
}