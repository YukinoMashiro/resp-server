//
// Created by yukino on 2023/5/3.
//

#ifndef RESP_SERVER_REPLY_H
#define RESP_SERVER_REPLY_H

#include "server.h"

int clientHasPendingReplies(client *c);
void addReplyError(client *c, const char *err);
void addReply(client *c, robj *obj);
void addReplyErrorFormat(client *c, const char *fmt, ...);

#endif //RESP_SERVER_REPLY_H
