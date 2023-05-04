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

void serverLog(int level, const char *fmt, ...);

#endif //RESP_SERVER_LOG_H
