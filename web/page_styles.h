/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dynamic stylesheets. Parsed rules have their own arena so replacing a
 * <style> never leaves another permanent copy of its selectors behind. */
typedef struct page_css_source {
    const char* url;
    const char* text;
    uint32_t len;
    struct page_css_source* next;
} page_css_source_t;

static uint32_t style_hash_bytes(uint32_t h,const char* text,uint32_t n) {
    for(uint32_t i=0;i<n;i++)h=(h^(uint8_t)text[i])*16777619u;
    return (h^0xFFu)*16777619u;
}
static uint32_t style_hash_text(uint32_t h,dom_node_t* n) {
    for(dom_node_t* c=n->first;c;c=c->next) {
        if(c->type==DOM_TEXT)h=style_hash_bytes(h,c->text,c->text_len);
        else h=style_hash_text(h,c);
    }
    return h;
}
static uint32_t style_hash_tree(uint32_t h,dom_node_t* n) {
    for(dom_node_t* c=n->first;c;c=c->next) {
        if(c->type!=DOM_ELEM)continue;
        if(!strcmp(c->tag,"style") || !strcmp(c->tag,"link")) {
            h=style_hash_bytes(h,c->tag,(uint32_t)strlen(c->tag));
            static const char* attrs[]={"media","disabled","rel","href","type"};
            for(unsigned i=0;i<sizeof(attrs)/sizeof(attrs[0]);i++) {
                const char* value=dom_attr(c,attrs[i]);
                h=style_hash_bytes(h,value?value:"",value?(uint32_t)strlen(value):0);
                h=(h^(value?1:0))*16777619u;
            }
            h=style_hash_text(h,c);
        } else h=style_hash_tree(h,c);
    }
    return h;
}
static const char* style_text(page_t* p,dom_node_t* n,uint32_t* len) {
    /* Concatenate every text node, including appendChild(createTextNode()). */
    const char* text=dom_text(&p->SA,n);
    *len=(uint32_t)strlen(text);return text;
}
static void style_add(page_t* p,dom_node_t* node,const char* text,uint32_t len) {
    const char* media=dom_attr(node,"media");
    if(media && *media) {
        uint32_t nm=(uint32_t)strlen(media);
        char* wrapped=arena_alloc(&p->SA,nm+len+12);
        memcpy(wrapped,"@media ",7);memcpy(wrapped+7,media,nm);
        wrapped[7+nm]='{';memcpy(wrapped+8+nm,text,len);
        wrapped[8+nm+len]='}';wrapped[9+nm+len]=0;
        text=wrapped;len+=nm+9;
    }
    p->sheet_hash[p->nsheets]=0;
    p->sheets[p->nsheets++]=css_parse(&p->SA,text,len,1);
}
static void collect_sheets(page_t* p,dom_node_t* n) {
    for(dom_node_t* c=n->first;c && p->nsheets<PAGE_MAX_SHEETS;c=c->next) {
        if(c->type!=DOM_ELEM)continue;
        if(!strcmp(c->tag,"style")) {
            const char* type=dom_attr(c,"type");
            if(dom_attr(c,"disabled") || (type && *type && strcasecmp(type,"text/css")))continue;
            uint32_t len;const char* text=style_text(p,c,&len);
            style_add(p,c,text,len);continue;
        }
        if(!strcmp(c->tag,"link")) {
            const char* rel=dom_attr(c,"rel"),*href=dom_attr(c,"href");
            if(!rel || !href || strcasecmp(rel,"stylesheet") || dom_attr(c,"disabled"))continue;
            char url[1024],ctype[96];url_resolve(p->base,href,url,sizeof(url));
            page_css_source_t* source=p->css_sources;
            for(;source;source=source->next)if(!strcmp(source->url,url))break;
            if(!source) {
                source=arena_alloc(&p->A,sizeof(*source));
                source->url=arena_strdup(&p->A,url,(uint32_t)strlen(url));
                source->next=p->css_sources;p->css_sources=source;
                char* data=NULL;uint32_t len=0;
                if(!fetch(p,url,&data,&len,ctype,sizeof(ctype))) {
                    source->text=arena_strdup(&p->A,data,len);source->len=len;kfree(data);
                }
            }
            if(source->text)style_add(p,c,source->text,source->len);
            continue;
        }
        collect_sheets(p,c);
    }
}
static int refresh_sheets(page_t* p) {
    uint32_t hash=style_hash_tree(2166136261u,p->doc);
    if(p->style_snapshot_valid && hash==p->style_fingerprint)return 0;
    arena_free_all(&p->SA);arena_init(&p->SA,LAYOUT_MEM);
    p->sheets[0]=css_default_sheet(&p->SA);p->sheet_hash[0]=0;p->nsheets=1;
    collect_sheets(p,p->doc);
    p->style_fingerprint=hash;p->style_snapshot_valid=1;
    return 1;
}
