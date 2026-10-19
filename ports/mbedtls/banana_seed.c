#include "banana_platform.h"

int banana_tls_seed(unsigned char* out, size_t len) {
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (a < 7) return -1;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0));
    if (!(b & (1u << 18))) return -1;
    size_t done = 0;
    while (done < len) {
        uint32_t word = 0;
        unsigned char ok = 0;
        for (int retry = 0; retry < 128 && !ok; retry++) {
            __asm__ volatile("rdseed %0; setc %1" : "=r"(word), "=qm"(ok));
            if (!ok) __asm__ volatile("pause");
        }
        if (!ok) {
            /* Never leave partially collected seed material with the caller. */
            volatile unsigned char* p = out;
            for (size_t i = 0; i < len; i++) p[i] = 0;
            return -1;
        }
        size_t n = len - done;
        if (n > sizeof(word)) n = sizeof(word);
        memcpy(out + done, &word, n);
        done += n;
        word = 0;
    }
    return 0;
}
