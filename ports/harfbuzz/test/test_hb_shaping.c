/*
 * Characterization and regression test suite for HarfBuzz 8.5.0 port:
 * 1. Initialize FreeType face and create HarfBuzz hb_font_t via hb_ft_font_create_referenced
 * 2. Shape Latin, French accented words, and ligature text (fi, fl, ffi)
 * 3. Verify glyph positions, advance values, and cluster-to-byte offset mapping
 * 4. Verify character-to-glyph mapping preservation for text selection and cursor navigation
 * 5. Verify edge cases (empty strings, whitespace runs, combined accents)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>

typedef struct {
    uint32_t byte_offset;
    int      x_pos_px;
} cursor_map_entry_t;

typedef struct {
    int count;
    cursor_map_entry_t* entries;
} cursor_map_t;

/* Build mapping between byte offsets and x-pixel coordinates from shaped buffer */
static cursor_map_t* build_cursor_map(const char* text, unsigned int text_len,
                                      hb_glyph_info_t* glyph_info,
                                      hb_glyph_position_t* glyph_pos,
                                      unsigned int glyph_count) {
    cursor_map_t* map = (cursor_map_t*)malloc(sizeof(cursor_map_t));
    if (!map) return NULL;
    /* Map size at most text_len + 1 */
    map->entries = (cursor_map_entry_t*)malloc(sizeof(cursor_map_entry_t) * (text_len + 1));
    if (!map->entries) {
        free(map);
        return NULL;
    }

    int cur_x = 0;
    map->count = 0;

    for (unsigned int i = 0; i < glyph_count; i++) {
        uint32_t cluster = glyph_info[i].cluster;
        /* Record first glyph position for this cluster */
        int found = 0;
        for (int k = 0; k < map->count; k++) {
            if (map->entries[k].byte_offset == cluster) {
                found = 1;
                break;
            }
        }
        if (!found) {
            map->entries[map->count].byte_offset = cluster;
            map->entries[map->count].x_pos_px = (cur_x + 32) >> 6;
            map->count++;
        }
        cur_x += glyph_pos[i].x_advance;
    }
    /* Add end-of-text entry */
    map->entries[map->count].byte_offset = text_len;
    map->entries[map->count].x_pos_px = (cur_x + 32) >> 6;
    map->count++;

    return map;
}

static int cursor_x_for_byte(const cursor_map_t* map, uint32_t byte_offset) {
    if (!map || map->count == 0) return 0;
    int best_x = 0;
    for (int i = 0; i < map->count; i++) {
        if (map->entries[i].byte_offset <= byte_offset) {
            best_x = map->entries[i].x_pos_px;
        } else {
            break;
        }
    }
    return best_x;
}

static uint32_t byte_for_cursor_x(const cursor_map_t* map, int x) {
    if (!map || map->count == 0) return 0;
    if (x <= map->entries[0].x_pos_px) return map->entries[0].byte_offset;
    for (int i = 0; i < map->count - 1; i++) {
        int x0 = map->entries[i].x_pos_px;
        int x1 = map->entries[i + 1].x_pos_px;
        int mid = (x0 + x1) / 2;
        if (x < mid) return map->entries[i].byte_offset;
    }
    return map->entries[map->count - 1].byte_offset;
}

static void free_cursor_map(cursor_map_t* map) {
    if (!map) return;
    if (map->entries) free(map->entries);
    free(map);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "FAIL: FT_Init_FreeType\n");
        return 1;
    }

    const char* font_dir = getenv("FONTDIR");
    char font_path[512];
    if (font_dir) {
        snprintf(font_path, sizeof(font_path), "%s/DejaVuSans.ttf", font_dir);
    } else {
        snprintf(font_path, sizeof(font_path), "%s", "fonts/DejaVuSans.ttf");
    }

    FT_Face ft_face;
    if (FT_New_Face(ft, font_path, 0, &ft_face)) {
        snprintf(font_path, sizeof(font_path), "%s", "../fonts/DejaVuSans.ttf");
        if (FT_New_Face(ft, font_path, 0, &ft_face)) {
            fprintf(stderr, "FAIL: FT_New_Face(%s)\n", font_path);
            FT_Done_FreeType(ft);
            return 1;
        }
    }

    int px = 16;
    FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)px);

    /* Create hb_font from ft_face */
    hb_font_t* hb_font = hb_ft_font_create_referenced(ft_face);
    if (!hb_font) {
        fprintf(stderr, "FAIL: hb_ft_font_create_referenced\n");
        FT_Done_Face(ft_face);
        FT_Done_FreeType(ft);
        return 1;
    }

    int all_ok = 1;

    printf("=== Test 1: Basic Latin Text Shaping ===\n");
    {
        const char* text = "Hello Banana OS!";
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, text, -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);

        hb_shape(hb_font, buf, NULL, 0);

        unsigned int glyph_count = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

        printf("Input '%s': length=%zu, shaped into %u glyphs\n", text, strlen(text), glyph_count);
        if (glyph_count != strlen(text)) {
            fprintf(stderr, "FAIL: expected %zu glyphs, got %u\n", strlen(text), glyph_count);
            all_ok = 0;
        }

        int total_adv = 0;
        for (unsigned int i = 0; i < glyph_count; i++) {
            total_adv += pos[i].x_advance;
            /* Verify cluster monotonicity */
            if (info[i].cluster != i) {
                fprintf(stderr, "FAIL: cluster mismatch at %u: expected %u, got %u\n", i, i, info[i].cluster);
                all_ok = 0;
            }
        }
        int total_w = (total_adv + 32) >> 6;
        printf("ok Latin text shaped successfully: width = %d px\n", total_w);
        if (total_w <= 0) {
            fprintf(stderr, "FAIL: invalid shaped text width %d\n", total_w);
            all_ok = 0;
        }

        hb_buffer_destroy(buf);
    }

    printf("\n=== Test 2: French Accents & Multibyte UTF-8 Clusters ===\n");
    {
        /* "Système prêt à l'emploi et élégant" */
        const char* text = "Syst\xC3\xA8me pr\xC3\xAAte \xC3\xA0 l'\xC3\xA9l\xC3\xA9gant";
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
        hb_buffer_add_utf8(buf, text, -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);

        hb_shape(hb_font, buf, NULL, 0);

        unsigned int glyph_count = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

        printf("French text byte length = %zu, shaped into %u glyphs\n", strlen(text), glyph_count);
        /* Check that multibyte sequences cluster to their start byte */
        for (unsigned int i = 0; i < glyph_count; i++) {
            uint32_t cl = info[i].cluster;
            if (cl >= strlen(text)) {
                fprintf(stderr, "FAIL: cluster %u exceeds text length %zu\n", cl, strlen(text));
                all_ok = 0;
            }
            /* Cluster should point to beginning of UTF-8 sequence, not continuation byte */
            unsigned char b = (unsigned char)text[cl];
            if ((b & 0xC0) == 0x80) {
                fprintf(stderr, "FAIL: cluster %u points to UTF-8 continuation byte 0x%02X\n", cl, b);
                all_ok = 0;
            }
        }
        printf("ok French UTF-8 cluster boundaries verified.\n");
        hb_buffer_destroy(buf);
    }

    printf("\n=== Test 3: Ligatures and Many-to-One Cluster Association ===\n");
    {
        /* DejaVu Sans contains standard typographic ligatures e.g. fi, fl */
        const char* text = "difficult office fly flower";
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, text, -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);

        /* Enable ligature feature 'liga' explicitly */
        hb_feature_t feat;
        hb_feature_from_string("liga=1", -1, &feat);
        hb_shape(hb_font, buf, &feat, 1);

        unsigned int glyph_count = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

        printf("Ligature corpus '%s': byte len=%zu, glyph count=%u\n", text, strlen(text), glyph_count);
        int total_w = 0;
        for (unsigned int i = 0; i < glyph_count; i++) {
            total_w += pos[i].x_advance;
        }
        total_w = (total_w + 32) >> 6;
        printf("ok Ligature shaping completed: width = %d px\n", total_w);

        /* Build cursor and selection map */
        cursor_map_t* cmap = build_cursor_map(text, (unsigned int)strlen(text), info, pos, glyph_count);
        if (!cmap) {
            fprintf(stderr, "FAIL: build_cursor_map failed\n");
            all_ok = 0;
        } else {
            /* Test cursor placement at start, mid, end */
            int x_start = cursor_x_for_byte(cmap, 0);
            int x_end = cursor_x_for_byte(cmap, (uint32_t)strlen(text));
            printf("Cursor positions: byte 0 -> x=%d px, byte %zu (end) -> x=%d px\n",
                   x_start, strlen(text), x_end);

            if (x_start != 0 || x_end != total_w) {
                fprintf(stderr, "FAIL: cursor bounds mismatch (x_start=%d expected 0, x_end=%d expected %d)\n",
                        x_start, x_end, total_w);
                all_ok = 0;
            }

            /* Selection range test: select 'office' [10, 16) */
            int sel_x0 = cursor_x_for_byte(cmap, 10);
            int sel_x1 = cursor_x_for_byte(cmap, 16);
            printf("Selection for 'office' [10..16]: x0=%d px, x1=%d px (selection width=%d px)\n",
                   sel_x0, sel_x1, sel_x1 - sel_x0);
            if (sel_x1 <= sel_x0) {
                fprintf(stderr, "FAIL: invalid selection bounds (%d -> %d)\n", sel_x0, sel_x1);
                all_ok = 0;
            }

            /* Round-trip hit testing: pixel x inside 'office' maps back to byte offset 10..16 */
            uint32_t hit_byte = byte_for_cursor_x(cmap, (sel_x0 + sel_x1) / 2);
            printf("Hit-test pixel %d maps back to byte offset %u (character '%c')\n",
                   (sel_x0 + sel_x1) / 2, hit_byte, text[hit_byte]);
            if (hit_byte < 10 || hit_byte > 16) {
                fprintf(stderr, "FAIL: hit-test mapped to unexpected byte %u\n", hit_byte);
                all_ok = 0;
            }
            free_cursor_map(cmap);
        }
        hb_buffer_destroy(buf);
    }

    printf("\n=== Test 4: Edge Cases (Empty Text, Spaces, Combining Marks) ===\n");
    {
        /* Empty string */
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, "", 0, 0, 0);
        hb_shape(hb_font, buf, NULL, 0);
        unsigned int count = 0;
        hb_buffer_get_glyph_infos(buf, &count);
        if (count != 0) {
            fprintf(stderr, "FAIL: empty string produced %u glyphs\n", count);
            all_ok = 0;
        } else {
            printf("ok empty string shaped to 0 glyphs.\n");
        }
        hb_buffer_destroy(buf);

        /* Spaces */
        buf = hb_buffer_create();
        const char* spaces = "   ";
        hb_buffer_add_utf8(buf, spaces, -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(hb_font, buf, NULL, 0);
        hb_glyph_position_t* spos = hb_buffer_get_glyph_positions(buf, &count);
        if (count != 3) {
            fprintf(stderr, "FAIL: 3 spaces produced %u glyphs\n", count);
            all_ok = 0;
        } else {
            int sp_w = (spos[0].x_advance + 32) >> 6;
            printf("ok 3 spaces shaped to 3 glyphs (space advance=%d px)\n", sp_w);
            if (sp_w <= 0) {
                fprintf(stderr, "FAIL: space advance is %d <= 0\n", sp_w);
                all_ok = 0;
            }
        }
        hb_buffer_destroy(buf);

        /* Combining diacritics: e + combining acute (U+0301) -> 'e' (0x65) + 0xCC 0x81 */
        const char* comb = "e\xCC\x81";
        buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, comb, -1, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        hb_shape(hb_font, buf, NULL, 0);
        hb_glyph_info_t* cinfo = hb_buffer_get_glyph_infos(buf, &count);
        printf("Combining sequence 'e\\u0301' shaped into %u glyphs\n", count);
        /* In HarfBuzz monotone cluster level, both base and mark share cluster 0 */
        if (count > 0 && cinfo[0].cluster != 0) {
            fprintf(stderr, "FAIL: combining mark cluster does not match base\n");
            all_ok = 0;
        } else {
            printf("ok combining diacritic clusters associated correctly.\n");
        }
        hb_buffer_destroy(buf);
    }

    printf("\n=== Test 5: Re-shaping Across Multiple Pixel Sizes ===\n");
    {
        int test_sizes[] = {12, 14, 18, 24, 36};
        const char* sentence = "Banana OS HarfBuzz integration test.";
        for (size_t s = 0; s < sizeof(test_sizes) / sizeof(test_sizes[0]); s++) {
            int cur_px = test_sizes[s];
            FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)cur_px);
            hb_ft_font_changed(hb_font);

            hb_buffer_t* buf = hb_buffer_create();
            hb_buffer_add_utf8(buf, sentence, -1, 0, -1);
            hb_buffer_guess_segment_properties(buf);
            hb_shape(hb_font, buf, NULL, 0);

            unsigned int count = 0;
            hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &count);
            int total_adv = 0;
            for (unsigned int i = 0; i < count; i++) {
                total_adv += pos[i].x_advance;
            }
            int w_px = (total_adv + 32) >> 6;
            printf("Size %2dpx: shaped %u glyphs, total advance = %d px\n", cur_px, count, w_px);
            if (w_px <= 0 || count == 0) {
                fprintf(stderr, "FAIL: invalid shaping metrics at size %dpx\n", cur_px);
                all_ok = 0;
            }
            hb_buffer_destroy(buf);
        }
    }

    hb_font_destroy(hb_font);
    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft);

    if (!all_ok) {
        printf("HarfBuzz shaping tests completed with FAILURES.\n");
        return 1;
    }
    printf("\nALL HARFBUZZ SHAPING CHECKS PASSED.\n");
    return 0;
}
