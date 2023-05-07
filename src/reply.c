//
// Created by yukino on 2023/5/3.
//

#include "reply.h"
#include "zmalloc.h"
#include "adlist.h"
#include "server.h"
#include "util.h"
#include "log.h"

int clientHasPendingReplies(client *c) {
    return c->bufpos || listLength(c->reply);
}

void prepareClientToWrite(client *c) {
    if (!clientHasPendingReplies(c)) {
        listAddNodeHead(server.clients_pending_write,c);
    }
}

int addReplyToBuffer(client *c, const char *s, size_t len) {
    size_t available = sizeof(c->buf)-c->bufpos;

    /* If there already are entries in the reply list, we cannot
     * add anything more to the static buffer. */
    if (listLength(c->reply) > 0) return C_ERR;

    /* Check that the buffer has enough space available for this string. */
    if (len > available) return C_ERR;

    memcpy(c->buf+c->bufpos,s,len);
    c->bufpos+=len;
    return C_OK;
}

void addReplyProtoToList(client *c, const char *s, size_t len) {

    listNode *ln = listLast(c->reply);
    clientReplyBlock *tail = ln? listNodeValue(ln): NULL;

    /* Note that 'tail' may be NULL even if we have a tail node, because when
     * addReplyDeferredLen() is used, it sets a dummy node to NULL just
     * fo fill it later, when the size of the bulk length is set. */

    /* Append to tail string when possible. */
    if (tail) {
        /* Copy the part we can fit into the tail, and leave the rest for a
         * new node */
        size_t avail = tail->size - tail->used;
        size_t copy = avail >= len? len: avail;
        memcpy(tail->buf + tail->used, s, copy);
        tail->used += copy;
        s += copy;
        len -= copy;
    }
    if (len) {
        /* Create a new node, make sure it is allocated to at
         * least PROTO_REPLY_CHUNK_BYTES */
        size_t size = len < PROTO_REPLY_CHUNK_BYTES? PROTO_REPLY_CHUNK_BYTES: len;
        tail = zmalloc(size + sizeof(clientReplyBlock));
        /* take over the allocation's internal fragmentation */
        tail->size = zmalloc_usable(tail) - sizeof(clientReplyBlock);
        tail->used = len;
        memcpy(tail->buf, s, len);
        listAddNodeTail(c->reply, tail);
        c->reply_bytes += tail->size;
    }
}

void addReplyProto(client *c, const char *s, size_t len) {
    prepareClientToWrite(c);
    if (addReplyToBuffer(c,s,len) != C_OK)
        addReplyProtoToList(c,s,len);
}

void addReplyErrorLength(client *c, const char *s, size_t len) {
    /* If the string already starts with "-..." then the error code
     * is provided by the caller. Otherwise we use "-ERR". */
    if (!len || s[0] != '-') addReplyProto(c,"-ERR ",5);
    addReplyProto(c,s,len);
    addReplyProto(c,"\r\n",2);
}

/*================================= reply interface ================================= */

void addReplyError(client *c, const char *err) {
    addReplyErrorLength(c,err,strlen(err));
}

void addReply(client *c, robj *obj) {
    prepareClientToWrite(c);

    if (sdsEncodedObject(obj)) {
        if (addReplyToBuffer(c,obj->ptr,sdslen(obj->ptr)) != C_OK)
            addReplyProtoToList(c,obj->ptr,sdslen(obj->ptr));
    } else if (obj->encoding == OBJ_ENCODING_INT) {
        /* For integer encoded strings we just convert it into a string
         * using our optimized function, and attach the resulting string
         * to the output buffer. */
        char buf[32];
        size_t len = ll2string(buf,sizeof(buf),(long)obj->ptr);
        if (addReplyToBuffer(c,buf,len) != C_OK)
            addReplyProtoToList(c,buf,len);
    } else {
        serverLog(LL_WARNING, "Wrong obj->encoding in addReply()");
    }
}

void addReplyErrorFormat(client *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sdscatvprintf(sdsempty(),fmt,ap);
    va_end(ap);
    /* Trim any newlines at the end (ones will be added by addReplyErrorLength) */
    s = sdstrim(s, "\r\n");
    /* Make sure there are no newlines in the middle of the string, otherwise
     * invalid protocol is emitted. */
    s = sdsmapchars(s, "\r\n", "  ",  2);
    addReplyErrorLength(c,s,sdslen(s));
    sdsfree(s);
}