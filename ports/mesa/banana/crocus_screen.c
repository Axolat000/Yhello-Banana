/* SPDX-License-Identifier: MIT
 * Banana OS: Mesa's crocus (Intel gen 4 - 7.5) behind OSMesa. The GPU is
 * the system's i915 interface on /dev/dri/card0 (kernel/gpu_gem.c), which
 * opens only once `gpu3d on` was given; the frames are read back for the
 * window as with the other OSMesa back ends.
 */
struct pipe_screen;
struct pipe_screen* banana_crocus_create_screen(int asked);
#if defined(GALLIUM_CROCUS)      /* (the Linux reference build too: tools/tests/crocus_host) */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "pipe/p_screen.h"
#include "frontend/drm_driver.h"
#include "util/driconf.h"
#include "util/xmlconfig.h"
#include "util/u_memory.h"
#include "crocus/drm/crocus_drm_public.h"

static const driOptionDescription banana_crocus_driconf[] = {
#include "pipe-loader/driinfo_gallium.h"
#include "crocus/driinfo_crocus.h"
};

struct pipe_screen* banana_crocus_create_screen(int asked) {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        if (asked) fprintf(stderr, "Banana crocus: no Intel GPU for apps (%s) - gpu3d on, and gpu3d test\n", strerror(errno));
        return NULL;
    }
    /* the options' descriptions: crocus fills the values in from them itself */
    static driOptionCache info, options;
    static int parsed;
    if (!parsed) {
        driParseOptionInfo(&info, banana_crocus_driconf, ARRAY_SIZE(banana_crocus_driconf));
        parsed = 1;
    }
    struct pipe_screen_config config = { .options = &options, .options_info = &info };
    struct pipe_screen* screen = crocus_drm_screen_create(fd, &config);   /* (it keeps a dup of fd) */
    close(fd);
    if (!screen) fprintf(stderr, "Banana crocus: the driver did not take this GPU\n");
    return screen;
}
#else
struct pipe_screen* banana_crocus_create_screen(int asked) { (void)asked; return 0; }
#endif
