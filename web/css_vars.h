/* Custom properties cascade as token values, then resolve on their own
 * element. Inherited values are already computed: child overrides must
 * not retroactively change a parent's aliases. Included only by css.c. */
#define CSS_VAR_DEPTH 48
#define CSS_VAR_VALUE_MAX 4096

static css_var_t* var_find(const style_t* st,const char* name,uint32_t n) {
    for(css_var_t* v=st->vars;v;v=v->next)
        if(strlen(v->name)==n && !strncmp(v->name,name,n))return v;
    return NULL;
}

/* Function tokens only: text inside quoted strings is not substituted. */
static const char* var_next(const char* s) {
    char quote=0;
    for(const char* p=s;*p;p++) {
        if(*p=='\\' && p[1]) {p++;continue;}
        if(quote) {if(*p==quote)quote=0;continue;}
        if(*p=='\'' || *p=='"') {quote=*p;continue;}
        if((p==s || !is_ident(p[-1])) && !strncasecmp(p,"var(",4))return p;
    }
    return NULL;
}

typedef struct {const char* name;uint32_t n;const char* fallback;uint32_t nf;const char* end;} var_call_t;
static int var_call(const char* p,var_call_t* c) {
    p+=4;
    while(is_ws(*p))p++;
    c->name=p;
    while(*p && is_ident(*p))p++;
    c->n=(uint32_t)(p-c->name);
    if(c->n<3 || c->name[0]!='-' || c->name[1]!='-')return 0;
    while(is_ws(*p))p++;
    c->fallback=NULL;c->nf=0;
    if(*p==',')c->fallback=++p;
    else if(*p!=')')return 0;
    int depth=1;char quote=0;
    for(;*p;p++) {
        if(*p=='\\' && p[1]) {p++;continue;}
        if(quote) {if(*p==quote)quote=0;continue;}
        if(*p=='\'' || *p=='"') {quote=*p;continue;}
        if(*p=='(')depth++;
        else if(*p==')' && !--depth) {
            if(c->fallback)c->nf=(uint32_t)(p-c->fallback);
            c->end=p+1;return 1;
        }
    }
    return 0;
}

static const char* subst_vars(arena_t* A,const style_t* st,const char* value,int depth) {
    const char* at=var_next(value);
    if(!at)return value;
    if(depth>=CSS_VAR_DEPTH)return NULL;
    char out[CSS_VAR_VALUE_MAX+1];uint32_t used=0;
    const char* cursor=value;
    do {
        uint32_t prefix=(uint32_t)(at-cursor);
        if(prefix>CSS_VAR_VALUE_MAX-used)return NULL;
        memcpy(out+used,cursor,prefix);used+=prefix;
        var_call_t call;
        if(!var_call(at,&call))return NULL;
        css_var_t* v=var_find(st,call.name,call.n);
        const char* replacement=v && v->state==2?v->value:NULL;
        if(!replacement && call.fallback) {
            if(call.nf>CSS_VAR_VALUE_MAX)return NULL;
            const char* fallback=arena_strdup(A,call.fallback,call.nf);
            replacement=subst_vars(A,st,fallback,depth+1);
        }
        if(!replacement)return NULL;
        uint32_t n=(uint32_t)strlen(replacement);
        if(n+2>CSS_VAR_VALUE_MAX-used)return NULL;
        /* Preserve token boundaries: var(--number)px must not turn into
         * a dimension by concatenating the substituted tokens. */
        out[used++]=' ';
        memcpy(out+used,replacement,n);used+=n;
        out[used++]=' ';
        cursor=call.end;
    } while((at=var_next(cursor))!=NULL);
    uint32_t tail=(uint32_t)strlen(cursor);
    if(tail>CSS_VAR_VALUE_MAX-used)return NULL;
    memcpy(out+used,cursor,tail);used+=tail;
    uint32_t start=0;
    while(start<used && is_ws(out[start]))start++;
    while(used>start && is_ws(out[used-1]))used--;
    return arena_strdup(A,out+start,used-start);
}

static void var_resolve(arena_t* A,style_t* st,css_var_t* v,css_var_t** path,int depth) {
    if(v->state==2 || v->state==3)return;
    if(v->state==1) {
        int start=0;
        while(start<depth && path[start]!=v)start++;
        for(int i=start;i<depth;i++)path[i]->state=3;
        return;
    }
    if(depth>=CSS_VAR_DEPTH) {v->state=3;return;}
    v->state=1;path[depth++]=v;
    /* Fallbacks participate in the dependency graph too, even if unused.
     * Scan inside var() instead of skipping its whole argument list. */
    for(const char* p=var_next(v->raw);p;p=var_next(p+4)) {
        var_call_t call;
        if(!var_call(p,&call)) {v->state=3;break;}
        css_var_t* dep=var_find(st,call.name,call.n);
        if(dep)var_resolve(A,st,dep,path,depth);
    }
    if(v->state==3)return;
    v->value=subst_vars(A,st,v->raw,0);
    v->state=v->value?2:3;
}

static void var_store(arena_t* A,style_t* st,const style_t* parent,const decl_t* d) {
    if(d->prop[0]!='-' || d->prop[1]!='-')return;
    css_var_t* inherited=parent?parent->vars:NULL;
    css_var_t* v=st->vars;
    for(;v && v!=inherited;v=v->next)if(!strcmp(v->name,d->prop))break;
    if(!v || v==inherited) {
        v=arena_alloc(A,sizeof(*v));v->name=d->prop;v->next=st->vars;st->vars=v;
    }
    v->raw=d->val;v->value=NULL;v->state=0;
    if(!strcasecmp(d->val,"initial"))v->state=3;
    else if(!strcasecmp(d->val,"inherit") || !strcasecmp(d->val,"unset") || !strcasecmp(d->val,"revert")) {
        css_var_t* pv=parent?var_find(parent,d->prop,(uint32_t)strlen(d->prop)):NULL;
        v->value=pv && pv->state==2?pv->value:NULL;v->state=v->value?2:3;
    }
}
