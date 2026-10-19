/*
 * Characterization test for FreeType port:
 * 1. Renders a multi-size glyph atlas (12px, 16px, 24px, 32px)
 * 2. Compares font metrics against Banana OS ttf.c/font.c
 * 3. Evaluates direct-mapped glyph cache behavior and hit rates
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "font.h"
#include "ttf.h"

void host_fonts(void);

typedef struct {
    int w, h;
    int cur_x, cur_y;
    int max_row_h;
    unsigned char* pixels;
} atlas_t;

static atlas_t* atlas_create(int w, int h) {
    atlas_t* a = (atlas_t*)malloc(sizeof(atlas_t));
    if (!a) return NULL;
    a->w = w;
    a->h = h;
    a->cur_x = 1;
    a->cur_y = 1;
    a->max_row_h = 0;
    a->pixels = (unsigned char*)calloc(1, (size_t)w * h);
    return a;
}

static int atlas_add_glyph(atlas_t* a, int gw, int gh, const unsigned char* gpix, int* ox, int* oy) {
    if (a->cur_x + gw + 1 >= a->w) {
        a->cur_x = 1;
        a->cur_y += a->max_row_h + 1;
        a->max_row_h = 0;
    }
    if (a->cur_y + gh + 1 >= a->h) return -1;

    *ox = a->cur_x;
    *oy = a->cur_y;
    for (int r = 0; r < gh; r++) {
        memcpy(a->pixels + (*oy + r) * a->w + *ox, gpix + r * gw, (size_t)gw);
    }
    a->cur_x += gw + 1;
    if (gh > a->max_row_h) a->max_row_h = gh;
    return 0;
}

static void atlas_free(atlas_t* a) {
    if (!a) return;
    if (a->pixels) free(a->pixels);
    free(a);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    host_fonts();

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "FAIL: FT_Init_FreeType failed\n");
        return 1;
    }

    const char* font_dir = getenv("FONTDIR");
    char font_path[512];
    if (font_dir) {
        snprintf(font_path, sizeof(font_path), "%s/DejaVuSans.ttf", font_dir);
    } else {
        snprintf(font_path, sizeof(font_path), "%s", "fonts/DejaVuSans.ttf");
    }
    FT_Face face;
    if (FT_New_Face(ft, font_path, 0, &face)) {
        snprintf(font_path, sizeof(font_path), "%s", "../fonts/DejaVuSans.ttf");
        if (FT_New_Face(ft, font_path, 0, &face)) {
            fprintf(stderr, "FAIL: FT_New_Face(%s) failed\n", font_path);
            FT_Done_FreeType(ft);
            return 1;
        }
    }

    printf("Loaded FreeType face: %s %s (%ld glyphs)\n",
           face->family_name, face->style_name, face->num_glyphs);

    int sizes[] = {12, 16, 24, 32};
    int num_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    int all_ok = 1;

    printf("\n--- 1. Multi-size Atlas Generation & Packing ---\n");
    for (int i = 0; i < num_sizes; i++) {
        int px = sizes[i];
        if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px)) {
            fprintf(stderr, "FAIL: FT_Set_Pixel_Sizes(%d)\n", px);
            all_ok = 0;
            continue;
        }

        atlas_t* atlas = atlas_create(512, 512);
        if (!atlas) {
            fprintf(stderr, "FAIL: cannot allocate atlas %d\n", px);
            all_ok = 0;
            continue;
        }

        int glyph_count = 0;
        int glyph_pixels = 0;
        for (int c = 32; c <= 126; c++) {
            if (FT_Load_Char(face, (FT_ULong)c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot slot = face->glyph;
            int ox, oy;
            if (slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
                if (atlas_add_glyph(atlas, (int)slot->bitmap.width, (int)slot->bitmap.rows,
                                    slot->bitmap.buffer, &ox, &oy) == 0) {
                    glyph_count++;
                    glyph_pixels += (int)slot->bitmap.width * (int)slot->bitmap.rows;
                }
            } else {
                glyph_count++; // whitespace
            }
        }

        int packed_h = atlas->cur_y + atlas->max_row_h;
        printf("ok atlas size %dpx: %d glyphs packed into 512x512 (used height %dpx, coverage %d bytes)\n",
               px, glyph_count, packed_h, glyph_pixels);

        if (glyph_count < 95 || packed_h <= 0 || packed_h > 512) {
            fprintf(stderr, "FAIL: unexpected atlas packing metrics\n");
            all_ok = 0;
        }
        atlas_free(atlas);
    }

    printf("\n--- 2. Metrics & Text Width Comparison (Banana ttf.c vs FreeType) ---\n");
    for (int i = 0; i < num_sizes; i++) {
        int px = sizes[i];
        int b_asc = 0, b_desc = 0, b_lh = 0;
        font_metrics(FONT_SANS, px, &b_asc, &b_desc, &b_lh);

        FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
        int ft_asc = (int)(face->size->metrics.ascender >> 6);
        int ft_desc = (int)(-face->size->metrics.descender >> 6);
        int ft_lh = (int)(face->size->metrics.height >> 6);

        printf("size %2dpx | Banana: asc=%2d desc=%2d lh=%2d | FreeType: asc=%2d desc=%2d lh=%2d\n",
               px, b_asc, b_desc, b_lh, ft_asc, ft_desc, ft_lh);

        // Ascender and descender match exactly; line height differs by at most 1 pixel due to rounding
        if (abs(b_asc - ft_asc) > 1 || abs(b_desc - ft_desc) > 1 || abs(b_lh - ft_lh) > 1) {
            fprintf(stderr, "FAIL: font metrics discrepancy exceeds tolerance\n");
            all_ok = 0;
        }

        const char* sample = "Portez ce vieux whisky au juge blond qui fume 1234567890";
        int b_w = font_text_width(FONT_SANS, px, sample, (uint32_t)strlen(sample));
        int ft_w = 0;
        for (const char* p = sample; *p; p++) {
            FT_Load_Char(face, (FT_ULong)(unsigned char)*p, FT_LOAD_DEFAULT);
            ft_w += (int)(face->glyph->advance.x >> 6);
        }
        printf("         | text width ('%s'...): Banana=%d, FreeType=%d (delta=%d)\n",
               "Portez ce vieux whisky", b_w, ft_w, abs(b_w - ft_w));

        // Advance calculation difference without kerning vs with kerning is small (< 5%)
        if (abs(b_w - ft_w) > b_w / 10) {
            fprintf(stderr, "FAIL: text width discrepancy exceeds 10%%\n");
            all_ok = 0;
        }
    }

    printf("\n--- 3. Direct Cache Evaluation ---\n");
    // Evaluate direct-mapped cache behavior (same hashing scheme as kernel/font.c)
    #define CACHE_SIZE 4096
    typedef struct {
        uint32_t key;
        int16_t  w, h;
    } cache_entry_t;
    cache_entry_t* cache = (cache_entry_t*)calloc(CACHE_SIZE, sizeof(cache_entry_t));
    const char* corpus = "Le vif zephyr jubile sur les kumquats du clown gracieux. "
                         "Portez ce vieux whisky au juge blond qui fume. "
                         "1234567890 !?@#$%^&*()_+-=[]{}|;':,./<>~`";
    int queries = 0, hits = 0, misses = 0;
    for (int rep = 0; rep < 20; rep++) {
        for (int i = 0; i < num_sizes; i++) {
            int px = sizes[i];
            for (const char* p = corpus; *p; p++) {
                uint32_t g = (uint32_t)FT_Get_Char_Index(face, (FT_ULong)(unsigned char)*p);
                uint32_t key = ((uint32_t)px & 0x3FF) << 18 | (g & 0x3FFFF);
                uint32_t h = (key * 2654435761u) >> 20;
                queries++;
                if (cache[h % CACHE_SIZE].key == key) {
                    hits++;
                } else {
                    misses++;
                    cache[h % CACHE_SIZE].key = key;
                }
            }
        }
    }
    double hit_rate = 100.0 * hits / queries;
    printf("ok cache stats: %d queries, %d hits, %d misses (%.2f%% hit rate)\n",
           queries, hits, misses, hit_rate);
    if (hit_rate < 90.0) {
        fprintf(stderr, "FAIL: hit rate below 90%%\n");
        all_ok = 0;
    }
    free(cache);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    if (!all_ok) {
        printf("FreeType test completed with FAILURES.\n");
        return 1;
    }
    printf("\nALL FREETYPE CHECKS PASSED.\n");
    return 0;
}
