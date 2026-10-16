#ifndef _TERMIOS_H
#define _TERMIOS_H
#ifdef __cplusplus
extern "C" {
#endif
/* the terminal's modes: Banana OS terminals always echo and read lines
 * (tcgetattr gives that; tcsetattr accepts and keeps the flags only) */
typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed, c_ospeed;
};
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ECHOK  0000040
#define ECHONL 0000100
#define ISIG   0000001
#define ICRNL  0000400
#define IXON   0002000
#define OPOST  0000001
#define VMIN   6
#define VTIME  5
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define B9600  0000015
#define B38400 0000017
int tcgetattr(int fd, struct termios* t);
int tcsetattr(int fd, int when, const struct termios* t);
#ifdef __cplusplus
}
#endif
#endif
