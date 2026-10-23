/* SPDX-License-Identifier: MIT
 * Banana OS: Mesa's iris (Intel gen 8 - 11 here: Broadwell to Ice Lake)
 * behind OSMesa. The GPU is the system's i915 interface on /dev/dri/card0
 * (kernel/gpu_gem.c, kernel/gpu_i915_lrc.c), which opens only once `gpu3d
 * on` was given; iris turns down the older GPUs (crocus takes those).
 */
struct pipe_screen;
struct pipe_screen* banana_iris_create_screen(int asked);
#if defined(GALLIUM_IRIS)      /* (the Linux reference build too: tools/tests/iris_host) */
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
#include "iris/drm/iris_drm_public.h"

static const driOptionDescription banana_iris_driconf[] = {
#include "pipe-loader/driinfo_gallium.h"
#include "iris/driinfo_iris.h"
};

struct pipe_screen* banana_iris_create_screen(int asked) {
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        if (asked) fprintf(stderr, "Banana iris: no Intel GPU for apps (%s) - gpu3d on, and gpu3d test\n", strerror(errno));
        return NULL;
    }
    static driOptionCache info, options;
    static int parsed;
    if (!parsed) {
        driParseOptionInfo(&info, banana_iris_driconf, ARRAY_SIZE(banana_iris_driconf));
        parsed = 1;
    }
    struct pipe_screen_config config = { .options = &options, .options_info = &info };
    struct pipe_screen* screen = iris_drm_screen_create(fd, &config);   /* (it keeps a dup of fd) */
    close(fd);
    if (!screen && asked) fprintf(stderr, "Banana iris: the driver did not take this GPU (gen 8 and later)\n");
    return screen;
}
#else
struct pipe_screen* banana_iris_create_screen(int asked) { (void)asked; return 0; }
#endif
