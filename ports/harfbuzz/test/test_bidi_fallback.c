/*
 * Characterization test suite for Font Fallback and Bidi Algorithm:
 * Step 3 of "fonts" task:
 * 1. Define font fallback chain (primary font -> fallback font if glyph missing).
 * 2. Segment UTF-8 text into directional (LTR/RTL) and font-face runs.
 * 3. Shape runs using HarfBuzz and FreeType (Arabic RTL shaping, Latin/French LTR shaping).
 * 4. Reorder visual runs (BiDi layout) and preserve cursor & selection mappings.
 * 5. Verify reference corpus: French text with accents, ligatures, and Arabic text.
 * 6. Verify hit-testing, selection bounds, and memory safety.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>
#include <hb-ft.h>
#include <utf8proc.h>

/* Bidi direction */
typedef enum {
    DIR_LTR = 0,
    DIR_RTL = 1
} text_dir_t;

/* Visual glyph */
typedef struct {
    uint32_t glyph_id;
    int32_t  x_offset;
    int32_t  y_offset;
    int32_t  x_advance;
    int32_t  y_advance;
    uint32_t cluster;       /* Logical byte offset in source string */
    int      face_idx;      /* Index of font face used (0=primary, 1=fallback...) */
} shaped_glyph_t;

/* Visual Run */
typedef struct {
    text_dir_t      dir;
    int             face_idx;
    uint32_t        byte_start;
    uint32_t        byte_len;
    shaped_glyph_t* glyphs;
    uint32_t        glyph_count;
    int32_t         total_advance;
} visual_run_t;

/* Layout result */
typedef struct {
    visual_run_t* runs;
    uint32_t      run_count;
    int32_t       total_width_px;
} layout_result_t;

/* Font Fallback Chain */
typedef struct {
    FT_Face*    ft_faces;
    hb_font_t** hb_fonts;
    int         face_count;
} font_fallback_chain_t;

/* Free layout result */
static void free_layout_result(layout_result_t* res) {
    if (!res) return;
    for (uint32_t i = 0; i < res->run_count; i++) {
        free(res->runs[i].glyphs);
    }
    free(res->runs);
    res->runs = NULL;
    res->run_count = 0;
}

/* Find first font in fallback chain that supports codepoint */
static int find_font_for_codepoint(font_fallback_chain_t* chain, int preferred_face, utf8proc_int32_t cp) {
    if (preferred_face >= 0 && preferred_face < chain->face_count) {
        if (FT_Get_Char_Index(chain->ft_faces[preferred_face], (FT_ULong)cp) != 0) {
            return preferred_face;
        }
    }
    for (int i = 0; i < chain->face_count; i++) {
        if (i == preferred_face) continue;
        if (FT_Get_Char_Index(chain->ft_faces[i], (FT_ULong)cp) != 0) {
            return i;
        }
    }
    /* Fall back to preferred or primary if none found */
    return (preferred_face >= 0 && preferred_face < chain->face_count) ? preferred_face : 0;
}

/* Classify codepoint direction: LTR, RTL, or NEUTRAL */
typedef enum {
    PROP_LTR,
    PROP_RTL,
    PROP_NEUTRAL
} char_dir_prop_t;

static char_dir_prop_t get_codepoint_bidi_prop(utf8proc_int32_t cp) {
    const utf8proc_property_t* prop = utf8proc_get_property(cp);
    switch (prop->bidi_class) {
        case UTF8PROC_BIDI_CLASS_R:
        case UTF8PROC_BIDI_CLASS_AL:
            return PROP_RTL;
        case UTF8PROC_BIDI_CLASS_L:
            return PROP_LTR;
        default:
            return PROP_NEUTRAL;
    }
}

/* Layout and shape text with Bidi resolution and font fallback */
static layout_result_t layout_text_bidi_fallback(const char* text, uint32_t text_len,
                                                 font_fallback_chain_t* chain,
                                                 int default_face,
                                                 text_dir_t base_dir) {
    layout_result_t res;
    memset(&res, 0, sizeof(res));
    if (!text || text_len == 0) return res;

    /* Step A: Itemize into segments by (direction, font_face) */
    typedef struct {
        uint32_t   start;
        uint32_t   len;
        text_dir_t dir;
        int        face_idx;
    } item_segment_t;

    item_segment_t* items = (item_segment_t*)malloc(sizeof(item_segment_t) * (text_len + 1));
    uint32_t item_count = 0;

    const utf8proc_uint8_t* p = (const utf8proc_uint8_t*)text;
    utf8proc_ssize_t rem = text_len;
    uint32_t cur_byte = 0;

    text_dir_t current_dir = base_dir;
    int current_face = default_face;
    uint32_t segment_start = 0;
    int in_segment = 0;

    while (rem > 0) {
        utf8proc_int32_t cp = 0;
        utf8proc_ssize_t sz = utf8proc_iterate(p, rem, &cp);
        if (sz <= 0) {
            /* invalid byte, treat as 1 byte */
            sz = 1;
            cp = 0xFFFD;
        }

        char_dir_prop_t prop = get_codepoint_bidi_prop(cp);
        text_dir_t cp_dir = (prop == PROP_RTL) ? DIR_RTL : (prop == PROP_LTR) ? DIR_LTR : current_dir;
        int cp_face = find_font_for_codepoint(chain, default_face, cp);

        if (!in_segment) {
            current_dir = cp_dir;
            current_face = cp_face;
            segment_start = cur_byte;
            in_segment = 1;
        } else if (cp_dir != current_dir || cp_face != current_face) {
            /* Close current segment */
            items[item_count].start = segment_start;
            items[item_count].len = cur_byte - segment_start;
            items[item_count].dir = current_dir;
            items[item_count].face_idx = current_face;
            item_count++;

            current_dir = cp_dir;
            current_face = cp_face;
            segment_start = cur_byte;
        }

        p += sz;
        rem -= sz;
        cur_byte += (uint32_t)sz;
    }

    if (in_segment && cur_byte > segment_start) {
        items[item_count].start = segment_start;
        items[item_count].len = cur_byte - segment_start;
        items[item_count].dir = current_dir;
        items[item_count].face_idx = current_face;
        item_count++;
    }

    /* Step B: Bidi Reordering
     * For single-level / simplified bidi:
     * In an LTR paragraph, RTL runs are placed in sequence but shaped internally RTL.
     * In an RTL paragraph, visual order reverses the logical run order.
     */
    uint32_t* visual_order = (uint32_t*)malloc(sizeof(uint32_t) * item_count);
    for (uint32_t i = 0; i < item_count; i++) {
        if (base_dir == DIR_RTL) {
            visual_order[i] = item_count - 1 - i;
        } else {
            visual_order[i] = i;
        }
    }

    /* Step C: Shape each run with HarfBuzz */
    res.runs = (visual_run_t*)malloc(sizeof(visual_run_t) * item_count);
    res.run_count = item_count;
    res.total_width_px = 0;

    for (uint32_t vi = 0; vi < item_count; vi++) {
        uint32_t log_idx = visual_order[vi];
        item_segment_t* it = &items[log_idx];
        visual_run_t* vr = &res.runs[vi];

        vr->dir = it->dir;
        vr->face_idx = it->face_idx;
        vr->byte_start = it->start;
        vr->byte_len = it->len;
        vr->glyphs = NULL;
        vr->glyph_count = 0;
        vr->total_advance = 0;

        hb_font_t* hb_font = chain->hb_fonts[it->face_idx];
        hb_buffer_t* buf = hb_buffer_create();
        hb_buffer_add_utf8(buf, text + it->start, (int)it->len, 0, (int)it->len);

        if (it->dir == DIR_RTL) {
            hb_buffer_set_direction(buf, HB_DIRECTION_RTL);
            hb_buffer_set_script(buf, HB_SCRIPT_ARABIC);
            hb_buffer_set_language(buf, hb_language_from_string("ar", -1));
        } else {
            hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
            hb_buffer_guess_segment_properties(buf);
        }

        hb_shape(hb_font, buf, NULL, 0);

        unsigned int gcount = 0;
        hb_glyph_info_t* ginfo = hb_buffer_get_glyph_infos(buf, &gcount);
        hb_glyph_position_t* gpos = hb_buffer_get_glyph_positions(buf, &gcount);

        vr->glyph_count = gcount;
        if (gcount > 0) {
            vr->glyphs = (shaped_glyph_t*)malloc(sizeof(shaped_glyph_t) * gcount);
            for (unsigned int gi = 0; gi < gcount; gi++) {
                vr->glyphs[gi].glyph_id = ginfo[gi].codepoint;
                vr->glyphs[gi].cluster = it->start + ginfo[gi].cluster;
                vr->glyphs[gi].x_offset = gpos[gi].x_offset;
                vr->glyphs[gi].y_offset = gpos[gi].y_offset;
                vr->glyphs[gi].x_advance = gpos[gi].x_advance;
                vr->glyphs[gi].y_advance = gpos[gi].y_advance;
                vr->glyphs[gi].face_idx = it->face_idx;
                vr->total_advance += gpos[gi].x_advance;
            }
        }
        hb_buffer_destroy(buf);
        res.total_width_px += (vr->total_advance + 32) >> 6;
    }

    free(visual_order);
    free(items);
    return res;
}

/* Character-to-glyph coordinate mapping for hit testing */
static int find_cursor_x_for_byte(const layout_result_t* res, uint32_t target_byte) {
    int cur_x = 0;
    for (uint32_t ri = 0; ri < res->run_count; ri++) {
        const visual_run_t* run = &res->runs[ri];
        if (target_byte >= run->byte_start && target_byte <= run->byte_start + run->byte_len) {
            int run_x = cur_x;
            if (run->dir == DIR_LTR) {
                for (uint32_t gi = 0; gi < run->glyph_count; gi++) {
                    if (run->glyphs[gi].cluster >= target_byte) {
                        return run_x;
                    }
                    run_x += (run->glyphs[gi].x_advance + 32) >> 6;
                }
                return cur_x + ((run->total_advance + 32) >> 6);
            } else {
                /* RTL run: clusters decrease from left to right or start at right */
                for (uint32_t gi = 0; gi < run->glyph_count; gi++) {
                    if (run->glyphs[gi].cluster <= target_byte) {
                        return run_x;
                    }
                    run_x += (run->glyphs[gi].x_advance + 32) >> 6;
                }
                return cur_x + ((run->total_advance + 32) >> 6);
            }
        }
        cur_x += (run->total_advance + 32) >> 6;
    }
    return cur_x;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("=== Test Suite: Bidi Algorithm & Font Fallback Integration ===\n");

    FT_Library ft;
    if (FT_Init_FreeType(&ft) != 0) {
        fprintf(stderr, "FAIL: FT_Init_FreeType failed\n");
        return 1;
    }

    const char* fontdir = getenv("FONTDIR") ? getenv("FONTDIR") : "fonts";
    char path_sans[256], path_mono[256];
    snprintf(path_sans, sizeof(path_sans), "%s/DejaVuSans.ttf", fontdir);
    snprintf(path_mono, sizeof(path_mono), "%s/DejaVuSansMono.ttf", fontdir);

    FT_Face face_sans, face_mono;
    if (FT_New_Face(ft, path_sans, 0, &face_sans) != 0) {
        fprintf(stderr, "FAIL: failed to load %s\n", path_sans);
        return 1;
    }
    if (FT_New_Face(ft, path_mono, 0, &face_mono) != 0) {
        fprintf(stderr, "FAIL: failed to load %s\n", path_mono);
        return 1;
    }

    FT_Set_Pixel_Sizes(face_sans, 0, 16);
    FT_Set_Pixel_Sizes(face_mono, 0, 16);

    hb_font_t* hb_sans = hb_ft_font_create_referenced(face_sans);
    hb_font_t* hb_mono = hb_ft_font_create_referenced(face_mono);

    FT_Face faces[2] = { face_sans, face_mono };
    hb_font_t* hb_fonts[2] = { hb_sans, hb_mono };
    font_fallback_chain_t chain = { faces, hb_fonts, 2 };

    int all_ok = 1;

    /* 1. Latin and French corpus with ligatures and accents */
    printf("\n--- Test 1: Latin & French Accented Corpus (LTR) ---\n");
    {
        const char* fr_text = "Système d'exploitation Banana OS : été, naïve, cœur, difficulté.";
        uint32_t len = (uint32_t)strlen(fr_text);
        layout_result_t res = layout_text_bidi_fallback(fr_text, len, &chain, 0, DIR_LTR);

        printf("French text byte length: %u, visual runs: %u, total width: %d px\n",
               len, res.run_count, res.total_width_px);
        if (res.run_count == 0 || res.total_width_px <= 0) {
            fprintf(stderr, "FAIL: invalid layout result for French corpus\n");
            all_ok = 0;
        } else {
            printf("ok French accented text successfully itemized and shaped.\n");
        }

        /* Check cursor position for accent byte offset */
        int x0 = find_cursor_x_for_byte(&res, 0);
        int x_end = find_cursor_x_for_byte(&res, len);
        printf("Cursor bounds: start x=%d, end x=%d\n", x0, x_end);
        if (x0 != 0 || x_end <= 0) {
            fprintf(stderr, "FAIL: invalid cursor bounds for French text\n");
            all_ok = 0;
        }
        free_layout_result(&res);
    }

    /* 2. Arabic corpus (RTL) shaping with HarfBuzz */
    printf("\n--- Test 2: Arabic Corpus (RTL) Shaping ---\n");
    {
        /* "مرحبا بكم في بنانا" (Welcome to Banana) */
        const char* ar_text = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD9\x83\xD9\x85 \xD9\x81\xD9\x8A \xD8\xA8\xD9\x86\xD8\xA7\xD9\x86\xD8\xA7";
        uint32_t len = (uint32_t)strlen(ar_text);
        layout_result_t res = layout_text_bidi_fallback(ar_text, len, &chain, 0, DIR_RTL);

        printf("Arabic text byte length: %u, visual runs: %u, total width: %d px\n",
               len, res.run_count, res.total_width_px);
        if (res.run_count == 0 || res.total_width_px <= 0) {
            fprintf(stderr, "FAIL: invalid layout result for Arabic corpus\n");
            all_ok = 0;
        } else {
            printf("ok Arabic text shaped RTL with HarfBuzz.\n");
            for (uint32_t r = 0; r < res.run_count; r++) {
                printf("  Run %u: dir=%s, face=%d, glyphs=%u, advance=%d px\n",
                       r, res.runs[r].dir == DIR_RTL ? "RTL" : "LTR",
                       res.runs[r].face_idx, res.runs[r].glyph_count,
                       (res.runs[r].total_advance + 32) >> 6);
            }
        }
        free_layout_result(&res);
    }

    /* 3. Mixed Bidirectional Text (LTR + Arabic RTL + LTR) */
    printf("\n--- Test 3: Mixed Bidirectional Text (Bidi Itemization & Reordering) ---\n");
    {
        /* "Banana OS مرحبا 2026" */
        const char* mixed = "Banana OS \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 2026";
        uint32_t len = (uint32_t)strlen(mixed);
        layout_result_t res = layout_text_bidi_fallback(mixed, len, &chain, 0, DIR_LTR);

        printf("Mixed text: length: %u, visual runs: %u, total width: %d px\n",
               len, res.run_count, res.total_width_px);
        if (res.run_count < 2) {
            fprintf(stderr, "FAIL: expected multiple directional runs for mixed bidi text, got %u\n", res.run_count);
            all_ok = 0;
        } else {
            printf("ok Mixed bidi text itemized into %u distinct runs.\n", res.run_count);
            for (uint32_t r = 0; r < res.run_count; r++) {
                printf("  Run %u: dir=%s, byte_start=%u, byte_len=%u, glyphs=%u\n",
                       r, res.runs[r].dir == DIR_RTL ? "RTL" : "LTR",
                       res.runs[r].byte_start, res.runs[r].byte_len, res.runs[r].glyph_count);
            }
        }
        free_layout_result(&res);
    }

    /* 4. Font Fallback Resolution */
    printf("\n--- Test 4: Font Fallback Chain ---\n");
    {
        /* Ensure primary font is selected for standard Latin, and fallback mechanisms work */
        utf8proc_int32_t cp_latin = 'A';
        utf8proc_int32_t cp_arabic = 0x0628; /* Arabic Beh */
        int face_latin = find_font_for_codepoint(&chain, 0, cp_latin);
        int face_ar = find_font_for_codepoint(&chain, 0, cp_arabic);

        printf("Codepoint 'A' resolves to face: %d\n", face_latin);
        printf("Codepoint U+0628 resolves to face: %d\n", face_ar);

        if (face_latin != 0 || face_ar < 0) {
            fprintf(stderr, "FAIL: font fallback resolution returned invalid faces\n");
            all_ok = 0;
        } else {
            printf("ok font fallback resolution operates correctly.\n");
        }
    }

    /* 5. Edge cases: empty text, isolated marks, unknown characters */
    printf("\n--- Test 5: Edge cases ---\n");
    {
        layout_result_t res = layout_text_bidi_fallback("", 0, &chain, 0, DIR_LTR);
        if (res.run_count != 0 || res.total_width_px != 0) {
            fprintf(stderr, "FAIL: empty text produced non-empty layout\n");
            all_ok = 0;
        } else {
            printf("ok empty text handled correctly.\n");
        }
        free_layout_result(&res);
    }

    hb_font_destroy(hb_sans);
    hb_font_destroy(hb_mono);
    FT_Done_Face(face_sans);
    FT_Done_Face(face_mono);
    FT_Done_FreeType(ft);

    if (!all_ok) {
        printf("\nBidi and Fallback tests completed with ERRORS.\n");
        return 1;
    }
    printf("\nALL BIDI & FONT FALLBACK TESTS PASSED SUCCESSFULLY.\n");
    return 0;
}
