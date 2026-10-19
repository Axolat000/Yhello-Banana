#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Apply the Banana-only OSMesa winsys to the pinned Mesa source, idempotently."""
from pathlib import Path
import shutil, sys
root = Path(sys.argv[1]); here = Path(__file__).resolve().parent
def write(path, text):
 if path.read_text() != text: path.write_text(text)
p = root/'src/gallium/targets/osmesa/meson.build'; s=p.read_text()
if "'banana_virgl.c'" not in s:
 s=s.replace("'osmesa_target.c',", "'osmesa_target.c', 'banana_virgl.c',")
 s=s.replace('inc_gallium_drivers,', 'inc_gallium_drivers, inc_virtio,')
 s=s.replace('dep_ws2_32, dep_thread,', 'driver_virgl, dep_ws2_32, dep_thread,')
if 'libgalliumvl_stub' not in s: s=s.replace('libmesa, libgallium,', 'libmesa, libgallium, libgalliumvl_stub,')
if 'inc_virtio' not in s: s=s.replace('inc_gallium_drivers,', 'inc_gallium_drivers, inc_virtio,')
write(p, s)
dst=p.parent/'banana_virgl.c'; source=(here/'banana/virgl_winsys.c').read_text()
if not dst.exists() or dst.read_text() != source: dst.write_text(source)
p=root/'src/gallium/targets/osmesa/osmesa_target.c';s=p.read_text()
if 'banana_virgl_create_screen' not in s:
 s=s.replace('struct pipe_screen *\nosmesa_create_screen(void);', 'struct pipe_screen *banana_virgl_create_screen(void);\n\nstruct pipe_screen *\nosmesa_create_screen(void);')
 s=s.replace('   /* We use a null software winsys', '''   const char *driver = getenv("GALLIUM_DRIVER");
   if (!driver || !strcmp(driver, "virgl")) {
      screen = banana_virgl_create_screen();
      if (screen) return debug_screen_wrap(screen);
      if (driver) return NULL;
   }

   /* We use a null software winsys''')
write(p, s)
p=root/'src/gallium/drivers/virgl/meson.build';s=p.read_text().replace('if not with_platform_windows\n', "if not with_platform_windows and not get_option('osmesa')\n");write(p, s)
p=root/'src/gallium/auxiliary/target-helpers/inline_sw_helper.h';s=p.read_text().replace('#if defined(GALLIUM_VIRGL)', '#if defined(GALLIUM_VIRGL) && !defined(BANANA_MESA_OSMESA)');write(p, s)

p=root/'src/gallium/drivers/virgl/virgl_context.c';s=p.read_text().replace('#ifndef _WIN32\n#include <libsync.h>', '#if !defined(_WIN32) && !defined(__BANANA_OS__)\n#include <libsync.h>');write(p, s)
p=root/'src/gallium/frontends/osmesa/osmesa.c';s=p.read_text()
if 'Banana: validate the selected screen' not in s:
 s=s.replace('   if (sharelist) {\n      st_shared = sharelist->st;', '''   /* Banana: validate the selected screen before using its callbacks. */
   struct pipe_frontend_screen *manager = get_st_manager();
   if (!manager || !manager->screen) return NULL;

   if (sharelist) {
      st_shared = sharelist->st;''')
 s=s.replace('      osmesa->depth_stencil_format = PIPE_FORMAT_Z24_UNORM_S8_UINT;', '''      const enum pipe_format choices[] = {PIPE_FORMAT_Z24_UNORM_S8_UINT,
         PIPE_FORMAT_S8_UINT_Z24_UNORM, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT};
      for (unsigned candidate = 0; candidate < ARRAY_SIZE(choices); candidate++) {
         if (manager->screen->is_format_supported(manager->screen, choices[candidate],
               PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_DEPTH_STENCIL)) {
            osmesa->depth_stencil_format = choices[candidate]; break;
         }
      }
      if (osmesa->depth_stencil_format == PIPE_FORMAT_NONE) { FREE(osmesa); return NULL; }''')
write(p, s)
# Banana's OSMesa has no Linux DRM modifier/udmabuf interface.
p=root/'src/gallium/drivers/llvmpipe/lp_texture.c';s=p.read_text().replace('#ifndef _WIN32\n', '#if !defined(_WIN32) && !defined(__BANANA_OS__)\n');write(p,s)
p=root/'src/gallium/drivers/llvmpipe/lp_scene.c';s=p.read_text()
if '#undef HAVE_REALLOCARRAY' not in s:
 s=s.replace('#include "util/reallocarray.h"', '#ifdef __BANANA_OS__\n#undef HAVE_REALLOCARRAY\n#endif\n#include "util/reallocarray.h"')
write(p,s)
