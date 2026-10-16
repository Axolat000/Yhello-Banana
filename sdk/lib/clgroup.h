/* Stackful work-items for collective barriers. Included by cl.c after WI,
 * KINFO and JOB. A worker owns one group at a time; groups still run on all
 * CPU cores. A barrier yields to that group's scheduler rather than blocking
 * an OS thread (and therefore works with one CPU too). */
#include <setjmp.h>
#define BCL_ITEM_STACK (16u * 1024u)
#define BCL_LOCAL_LIMIT (32u * 1024u)
#define BCL_LOCAL_SLOTS 32

typedef struct bcl_group BCL_GROUP;
typedef struct {
    jmp_buf context;
    WI wi;
    unsigned char* stack;
    int state, site;
} BCL_FIBER;
struct bcl_group {
    jmp_buf scheduler;
    BCL_FIBER* items;
    unsigned char* stacks;
    size_t n, current, local_bytes;
    KINFO* info;
    void** args;
    void* locals[BCL_LOCAL_SLOTS];
    size_t local_sizes[BCL_LOCAL_SLOTS];
    int error;
};

/* The only machine-dependent part: start a function on an aligned stack.
 * It leaves through longjmp, never through this frame. The scheduler's
 * setjmp saves the original stack and all callee-saved registers. */
void __bcl_start_item(void (*fn)(void*), void* arg, void* stack) __attribute__((noreturn));
#ifdef __x86_64__
__asm__(".text\n.globl __bcl_start_item\n.hidden __bcl_start_item\n"
        "__bcl_start_item:\nmov %rdi,%rax\nmov %rdx,%rsp\nmov %rsi,%rdi\n"
        "and $-16,%rsp\ncall *%rax\nud2\n");
#else
__asm__(".text\n.globl __bcl_start_item\n.hidden __bcl_start_item\n"
        "__bcl_start_item:\nmov 4(%esp),%eax\nmov 8(%esp),%ecx\nmov 12(%esp),%esp\n"
        "and $-16,%esp\nsub $12,%esp\npush %ecx\ncall *%eax\nud2\n");
#endif

static void group_error(BCL_GROUP* g, int error) {
    g->error = error;
    longjmp(g->scheduler, 1);
}

static void bcl_barrier(const WI* wi, int flags, int site) {
    BCL_GROUP* g = wi->group;
    if (!g) return; /* single item / noncollective fast path */
    if (!flags || (flags & ~3)) group_error(g, CL_INVALID_VALUE);
    BCL_FIBER* f = &g->items[wi->lane];
    f->site = site;
    f->state = 1; /* waiting */
    __sync_synchronize();
    if (!setjmp(f->context)) longjmp(g->scheduler, 1);
    __sync_synchronize();
}

static void* bcl_local(const WI* wi, unsigned slot, size_t bytes) {
    BCL_GROUP* g = wi->group;
    if (!g || slot >= BCL_LOCAL_SLOTS || !bytes) {
        if (g) group_error(g, CL_OUT_OF_RESOURCES);
        return NULL;
    }
    if (g->locals[slot]) {
        if (g->local_sizes[slot] != bytes) group_error(g, CL_INVALID_WORK_GROUP_SIZE);
        return g->locals[slot];
    }
    if (bytes > BCL_LOCAL_LIMIT - g->local_bytes) group_error(g, CL_OUT_OF_RESOURCES);
    /* vector types can need up to 64-byte alignment */
    void* p = NULL;
    if (posix_memalign(&p, 64, bytes)) group_error(g, CL_OUT_OF_HOST_MEMORY);
    g->locals[slot] = p; g->local_sizes[slot] = bytes; g->local_bytes += bytes;
    return p;
}

static void item_entry(void* opaque) {
    BCL_FIBER* f = opaque;
    BCL_GROUP* g = f->wi.group;
    g->info->call(&f->wi, g->args);
    f->state = 2; /* finished */
    longjmp(g->scheduler, 1);
}

static BCL_GROUP* group_new(size_t n, KINFO* info, void** args, size_t arg_local) {
    BCL_GROUP* g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->items = calloc(n, sizeof(*g->items));
    g->stacks = malloc(n * BCL_ITEM_STACK + 16);
    if (!g->items || !g->stacks) { free(g->items); free(g->stacks); free(g); return NULL; }
    g->n = n; g->info = info; g->args = args; g->local_bytes = arg_local;
    for (size_t i = 0; i < n; i++)
        g->items[i].stack = g->stacks + i * BCL_ITEM_STACK;
    return g;
}

static void group_delete(BCL_GROUP* g) {
    if (!g) return;
    for (int i = 0; i < BCL_LOCAL_SLOTS; i++) free(g->locals[i]);
    free(g->stacks); free(g->items); free(g);
}

static int group_run(BCL_GROUP* g, const WI* base) {
    g->error = 0;
    for (size_t i = 0; i < g->n; i++) {
        BCL_FIBER* f = &g->items[i];
        f->wi = *base; f->wi.group = g; f->wi.lane = (unsigned)i;
        f->wi.lid[0] = i % base->lsz[0];
        f->wi.lid[1] = (i / base->lsz[0]) % base->lsz[1];
        f->wi.lid[2] = i / (base->lsz[0] * base->lsz[1]);
        for (int d = 0; d < 3; d++) f->wi.gid[d] = base->grp[d] * base->lsz[d] + f->wi.lid[d] + base->off[d];
        f->state = 0;
        memset(f->stack, 0xB6, 64); /* detects modest private-stack overflows */
    }
    for (;;) {
        for (g->current = 0; g->current < g->n; g->current++) {
            BCL_FIBER* f = &g->items[g->current];
            set_current_wi(&f->wi);
            if (!setjmp(g->scheduler)) {
                if (!f->state) __bcl_start_item(item_entry, f, f->stack + BCL_ITEM_STACK);
                else longjmp(f->context, 1);
            }
            if (g->error) return g->error;
            for (int k = 0; k < 64; k++) if (g->items[g->current].stack[k] != 0xB6) return CL_OUT_OF_RESOURCES;
        }
        size_t done = 0, wait = 0;
        int site = g->items[0].site;
        for (size_t i = 0; i < g->n; i++) {
            BCL_FIBER* f = &g->items[i];
            if (f->state == 2) done++;
            else if (f->state == 1) {
                wait++;
                if (f->site != site) return CL_INVALID_OPERATION;
            }
        }
        if (done == g->n) return CL_SUCCESS;
        /* Divergent barriers are invalid kernels, not a reason to hang. */
        if (done || wait != g->n) return CL_INVALID_OPERATION;
    }
}
