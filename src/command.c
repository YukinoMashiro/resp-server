//
// Created by yukino on 2023/5/2.
//
#include <stdio.h>
#include "unistd.h"

#include "command.h"
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