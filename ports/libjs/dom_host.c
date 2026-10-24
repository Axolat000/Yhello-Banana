/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../web/page.h"
#include "../../web/script_int.h"
#include "../../kernel/kstring.h"
#include "dom.h"
#include "../../web/prelude.h"
typedef struct foreign {unsigned id;int callable;value_t value;struct foreign* next;} foreign_t;
typedef struct {interp_t* I;void* engine;foreign_t* foreign;} session_t;
static bjs_value export_value(session_t*,value_t);
static value_t import_value(session_t*,bjs_value);
static value_t foreign_call(interp_t* I,value_t self,int argc,value_t* argv){
    session_t* s=script_backend_context(I);
    if(argc>128){script_throw(I,"RangeError: host callback arguments");return v_undef();}
    bjs_value args[129],out={0};args[0]=export_value(s,self);
    for(int k=0;k<argc;k++)args[k+1]=export_value(s,argv[k]);
    if(bjs_foreign(s->engine,(unsigned)I->cur_native->data.n,2,NULL,args,(unsigned)argc+1,&out)<0){
        script_throw(I,bjs_error(s->engine));return v_undef();
    }
    return import_value(s,out);
}
static value_t foreign_get(interp_t* I,obj_t* o,const char* key,int* found){
    session_t* s=script_backend_context(I);foreign_t* f=o->host;bjs_value out={0};
    *found=1;
    if(bjs_foreign(s->engine,f->id,0,key,NULL,0,&out)<0){
        script_throw(I,bjs_error(s->engine));return v_undef();
    }
    return import_value(s,out);
}
static int foreign_set(interp_t* I,obj_t* o,const char* key,value_t value){
    session_t* s=script_backend_context(I);foreign_t* f=o->host;
    bjs_value in=export_value(s,value),out={0};
    if(bjs_foreign(s->engine,f->id,1,key,&in,1,&out)<0)script_throw(I,bjs_error(s->engine));
    return 1;
}
static const host_class_t foreign_class={"LibJS value",foreign_get,foreign_set};
static bjs_value export_value(session_t* s,value_t v){
    (void)s;bjs_value w={0};w.kind=v.t;
    if(v.t==V_BOOL)w.number=v.b;
    if(v.t==V_NUM)w.number=(double)v.n;
    if(v.t==V_STR)w.text=v.s->s;
    if(v.t==V_OBJ){
        w.token=(uint64_t)(uintptr_t)v.o;w.array=v.o->kind==OBJ_ARRAY;
        if(v.o->hc==&foreign_class){w.kind=8;w.token=((foreign_t*)v.o->host)->id;}
    }
    if(v.t==V_FUNC){
        w.token=(uint64_t)(uintptr_t)v.f;
        if(v.f->nf==foreign_call){w.kind=9;w.token=(unsigned)v.f->data.n;}
    }
    return w;
}
static value_t import_value(session_t* s,bjs_value w){
    switch(w.kind){
    case V_NULL:return v_null();
    case V_BOOL:return v_bool(w.number!=0);
    case V_NUM:return v_num(w.number);
    case V_STR:return v_str(s->I,w.text?w.text:"");
    case V_OBJ:return v_obj((obj_t*)(uintptr_t)w.token);
    case V_FUNC:{value_t v=v_undef();v.t=V_FUNC;v.f=(func_t*)(uintptr_t)w.token;return v;}
    case 8:case 9:{
        bjs_value v=bjs_host_foreign(s,(unsigned)w.token,w.kind==9);
        return import_value(s,v);
    }
    default:return v_undef();
    }
}
bjs_value bjs_host_foreign(void* host,unsigned id,int callable){
    session_t* s=host;
    for(foreign_t* f=s->foreign;f;f=f->next)if(f->id==id){
        bjs_value w={0};w.kind=f->value.t;
        w.token=(uint64_t)(uintptr_t)(callable?(void*)f->value.f:(void*)f->value.o);return w;
    }
    foreign_t* f=arena_alloc(s->I->A,sizeof(*f));
    if(!f)return (bjs_value){0};
    f->id=id;f->callable=callable;f->next=s->foreign;s->foreign=f;
    if(callable){f->value=v_native(s->I,"LibJS callback",foreign_call);f->value.f->data=v_num(id);}
    else {obj_t* o=obj_new(s->I,OBJ_HOST);o->hc=&foreign_class;o->host=f;f->value=v_obj(o);}
    /* Return the actual legacy token here; subsequent export recognizes it. */
    bjs_value w={0};w.kind=f->value.t;w.token=(uint64_t)(uintptr_t)(callable?(void*)f->value.f:(void*)f->value.o);
    return w;
}
int bjs_host(void* host,int op,bjs_value object,const char* key,bjs_value* args,unsigned argc,bjs_value* out,char* error,unsigned cap){
    session_t* s=host;interp_t* I=s->I;value_t v=import_value(s,object),r=v_undef();
    I->ctl=CTL_NONE;I->err[0]=0;
    if(op==4&&argc){r=import_value(s,args[0]);}
    else if(op==3){
        const script_backend_t* saved=I->backend;I->backend=NULL;
        r=script_get_global(I,key);I->backend=saved;
    }else if(op==5){
        if(v.t==V_OBJ)r=v.o->proto?v_obj(v.o->proto):v_null();
    }else if(op==0){
        obj_t* o=v.t==V_OBJ?v.o:v.t==V_FUNC?v.f->statics:NULL;
        if(o)r=obj_get(I,o,key);
    }else if(op==1&&argc){
        obj_t* o=v.t==V_OBJ?v.o:v.t==V_FUNC?v.f->statics:NULL;
        if(o)obj_set(I,o,key,import_value(s,args[0]));
    }else if((op==2||op==7)&&argc&&argc<=129){
        value_t av[128];
        for(unsigned k=1;k<argc;k++)av[k-1]=import_value(s,args[k]);
        value_t self=import_value(s,args[0]),saved_nt=I->new_target;
        if(op==7){
            /* Native DOM constructors use the same explicit "new" marker as
             * the legacy evaluator; ordinary calls must keep rejecting it. */
            if(v.t!=V_FUNC||!v.f->native){kstrlcpy(error,"Invalid DOM constructor",cap);return -1;}
            I->new_target=self;
            self=v_null();self.b=0x4E57;
        }
        int rc=script_call(I,v,self,(int)argc-1,av,&r);
        I->new_target=saved_nt;
        if(rc<0){
            kstrlcpy(error,script_error(I),cap);return -1;
        }
    }else return -1;
    if(I->ctl==CTL_THROW){kstrlcpy(error,v_cstr(I,I->ret),cap);I->ctl=CTL_NONE;return -1;}
    *out=export_value(s,r);return 0;
}
static int backend_run(interp_t* I,const char* code,uint32_t len,const char* name){
    session_t* s=script_backend_context(I);
    return bjs_run(s->engine,code,len,name,I->err,sizeof(I->err));
}
static value_t backend_get(interp_t* I,const char* name){
    session_t* s=script_backend_context(I);bjs_value out={0};
    if(bjs_global(s->engine,name,&out)<0)return v_undef();
    return import_value(s,out);
}
static void backend_checkpoint(interp_t* I){session_t* s=script_backend_context(I);bjs_checkpoint(s->engine);}
static void backend_destroy(interp_t* I){session_t* s=script_backend_context(I);bjs_destroy(s->engine);}
static const script_backend_t backend={backend_run,backend_get,backend_checkpoint,backend_destroy};
int banana_libjs_install_dom(page_t* p){
    session_t* s=arena_alloc(&p->A,sizeof(*s));if(!s)return -1;
    memset(s,0,sizeof(*s));s->I=p->js;
    /* Install context before creating bindings so callbacks resolve this session. */
    script_set_backend(p->js,&backend,s);
    s->engine=bjs_create(s);
    if(!s->engine){script_set_backend(p->js,NULL,NULL);return -1;}
    if(backend_run(p->js,k_prelude,sizeof(k_prelude)-1,"web prelude")<0){
        kstrlcpy(p->status,script_error(p->js),sizeof(p->status));return -1;
    }
    return 0;
}
