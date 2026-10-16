#ifndef _SYS_UN_H
#define _SYS_UN_H
#ifdef __cplusplus
extern "C" {
#endif
/* Unix-domain sockets: not there (socket(AF_UNIX) fails) */
#include <sys/socket.h>
struct sockaddr_un { sa_family_t sun_family; char sun_path[108]; };
#ifdef __cplusplus
}
#endif
#endif
