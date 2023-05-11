//
// Created by yukino on 2023/5/7.
//

#include <string.h>
#include "server.h"
#include "reply.h"
#include "log.h"

void commandCommand(client *c){
    addReply(c, shared.ok);
}

/* 回复固定字符串信息
效果:
127.0.0.1:2234> ping
PONG
 * */
void pingCommand(client *c) {
    addReply(c, shared.pong);
}

/* 获取客户端参数信息
效果:
127.0.0.1:2234> set k1 v1 v2 v3 v4
"set command"
日志信息:
yukino@yukino:~/project/resp-server$ ./resp-server
376658:09 May 2023 15:20:37.621 # argv[0]: set
376658:09 May 2023 15:20:37.622 # argv[1]: k1
376658:09 May 2023 15:20:37.622 # argv[2]: v1
376658:09 May 2023 15:20:37.622 # argv[3]: v2
376658:09 May 2023 15:20:37.622 # argv[4]: v3
376658:09 May 2023 15:20:37.622 # argv[5]: v4
 * */
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

/* 回复简单字符串
效果:
127.0.0.1:2234> get
"get command"
 * */
void getCommand(client *c) {
    addReplyBulk(c, createObject(OBJ_STRING,sdsnew(
            "get command")));
}

/* 回复数字数组
效果:
127.0.0.1:2234> test
1) "1"
2) "2"
3) "3"
4) "4"
 * */
void testCommand(client *c) {
    /* 添加数组元素数量 */
    addReplyArrayLen(c,4);
    /* 添加数组元素 */
    addReplyBulkLongLong(c, 1);
    addReplyBulkLongLong(c, 2);
    addReplyBulkLongLong(c, 3);
    addReplyBulkLongLong(c, 4);
}

/* 回复字符串数组
效果:
127.0.0.1:2234> test1
1) "yukino"
2) "mashiro"
3) "hachiman"
4) "hanser"
 * */
void testCommand1(client *c) {

    addReplyArrayLen(c,4);
    addReplyBulkCBuffer(c, "yukino", strlen("yukino"));
    addReplyBulkCBuffer(c, "mashiro", strlen("mashiro"));
    addReplyBulkCBuffer(c, "hachiman", strlen("hachiman"));
    addReplyBulkCBuffer(c, "hanser", strlen("hanser"));
}

/* 命令列表
命令行的参数个数，用于检查命令请求格式是否正确
如果这个值为负数-N，那么表示参数的数量大于等于N
如果这个值为0，那么不做检查
如果这个值大于0，需要相等
注意：命令的名字本身也是一个参数
 * */
respCommand commandTable[] = {
        {"command", commandCommand, -1},
        {"ping", pingCommand, 0},
        {"set",setCommand,0},
        {"get", getCommand, 0},
        {"test", testCommand, 0},
        {"test1", testCommand1, 0},
};

int main(int argc, char **argv) {
    respInitOptions(2233,
                      "\0",
                      commandTable,
                      sizeof(commandTable) / sizeof(struct respCommand));
    respListenEvent();
    return 0;
}