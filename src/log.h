//
// Created by yukino on 2023/5/4.
//

#ifndef RESP_SERVER_LOG_H
#define RESP_SERVER_LOG_H

/* Log levels */
#define LL_DEBUG 0
#define LL_VERBOSE 1
#define LL_NOTICE 2
#define LL_WARNING 3
#define LL_RAW (1<<10) /* Modifier to log without timestamp */

#define LOG_MAX_LEN    1024 /* Default maximum length of syslog messages.*/

#ifdef __GNUC__
void _serverPanic(const char *file, int line, const char *msg, ...)
__attribute__ ((format (printf, 3, 4)));
#else
void _serverPanic(const char *file, int line, const char *msg, ...);
#endif

void _serverAssert(const char *estr, const char *file, int line);

#define serverPanic(...) _serverPanic(__FILE__,__LINE__,__VA_ARGS__),_exit(1)
#define serverAssert(_e) ((_e)?(void)0 : (_serverAssert(#_e,__FILE__,__LINE__),_exit(1)))

void serverLog(int level, const char *fmt, ...);

#endif //RESP_SERVER_LOG_H
