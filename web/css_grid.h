/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bounded explicit column lists: fixed lengths, percentages, fr, minmax and
 * numeric repeat. Unsupported lists are ignored atomically, never half applied. */
static int grid_track(const char* value, int font, grid_track_t* out) {
    char token[CSS_VALUE_TOKEN];
    size_t n = strlen(value);
    *out = (grid_track_t){0, 0, GRID_PX};
    if (!strcasecmp(value, "auto")) { out->kind = GRID_FR; out->value = 100; return 1; }
    if (n > 8 && !strncmp(value, "minmax(", 7) && value[n-1] == ')') {
        memcpy(token, value+7, n-8); token[n-8] = 0;
        css_value_tokens it = {token, CSS_VALUE_TOKEN};
        char lo[CSS_VALUE_TOKEN], comma[CSS_VALUE_TOKEN], hi[CSS_VALUE_TOKEN], extra[CSS_VALUE_TOKEN];
        if (css_next_value(&it, lo) != 1 || css_next_value(&it, comma) != 1 || strcmp(comma, ",") ||
            css_next_value(&it, hi) != 1 || css_next_value(&it, extra) != 0) return 0;
        int minimum = !strcmp(lo,"auto") ? 0 : parse_len(lo,font,-1,NULL);
        if (minimum < 0 || minimum > 1000000 || !strncmp(hi,"minmax(",7) || !grid_track(hi,font,out)) return 0;
        out->min = minimum;
        if (out->kind == GRID_PX && out->value < minimum) out->value = minimum;
        return 1;
    }
    if (n > 2 && !strcmp(value+n-2, "fr")) {
        memcpy(token,value,n-2); token[n-2] = 0;
        if (!flex_number(token,&out->value) || out->value <= 0 || out->value > 1000000) return 0;
        out->kind = GRID_FR; return 1;
    }
    int pct = 0, kind;
    int px = parse_len_kind(value,font,-1,&pct,&kind);
    if (kind == CSS_MATH_PERCENT && pct >= 0 && pct <= 1000000) { out->kind = GRID_PERCENT; out->value = pct; return 1; }
    if (px == LEN_AUTO || px < 0 || px > 1000000 || (kind == CSS_MATH_NUMBER && px)) return 0;
    out->value = px; return 1;
}
static int grid_list(const char* value, int font, grid_track_t* tracks, int* count, int depth) {
    css_value_tokens it = {value,4096}; char token[CSS_VALUE_TOKEN]; int got;
    while ((got = css_next_value(&it,token)) == 1) {
        size_t n = strlen(token);
        if (n > 8 && !strncmp(token,"repeat(",7) && token[n-1] == ')') {
            if (depth >= 3) return 0;
            token[n-1] = 0;
            const char* list = token+7; int repeats = 0;
            while (*list >= '0' && *list <= '9') { repeats = repeats*10 + *list++-'0'; if(repeats>GRID_MAX_COLS)return 0; }
            while(is_ws(*list))list++;
            if(!repeats || *list++ != ',')return 0;
            int before = *count;
            if(!grid_list(list,font,tracks,count,depth+1))return 0;
            int added = *count-before;
            if(!added || added*repeats > GRID_MAX_COLS-before)return 0;
            for(int r=1;r<repeats;r++)for(int j=0;j<added;j++)tracks[(*count)++] = tracks[before+j];
        } else {
            if(*count == GRID_MAX_COLS || !grid_track(token,font,&tracks[*count]))return 0;
            (*count)++;
        }
    }
    return got == 0;
}
static void grid_columns(style_t* st, const char* value) {
    if(!strcasecmp(value,"none") || !strcasecmp(value,"initial") || !strcasecmp(value,"unset")) { st->grid_ncols=0; return; }
    grid_track_t tracks[GRID_MAX_COLS];int n=0;
    if(grid_list(value,st->font_px,tracks,&n,0) && n) {
        memcpy(st->grid_cols,tracks,(size_t)n*sizeof(*tracks));st->grid_ncols=(uint8_t)n;
    }
}
