/*
 * OpenCL for Banana OS: the OpenCL 1.2 host API on the processor.
 *
 * clBuildProgram compiles the OpenCL C source with TinyCC into the app's
 * memory: a prelude turns OpenCL C into C (address space qualifiers, the
 * work-item functions reading a hidden argument every kernel gets, the
 * built-ins), and a small trampoline per kernel calls it with its arguments
 * from an array. clEnqueueNDRangeKernel shares the work-groups out between
 * every core (a pool of threads taking the next group in turn) and returns
 * when they are done - queues are in order and commands finish at once.
 *
 * Built into apps that set USE_CL = 1 (sdk/banana.mk), with TinyCC from
 * ports/tcc (sdk/lib/cljit.c). BCL_HOST builds it on Linux with a host
 * libtcc and pthreads, for testing (sdk/lib/cltest/).
 */
#include "CL/cl.h"
#include "libtcc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#ifdef BCL_HOST
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#else
#include <banana.h>
#endif

#define MAXARGS 32
#define MAXTHR  16

/* ══ objects ═════════════════════════════════════════════════════════ */

struct _cl_platform_id { int unused; };
struct _cl_device_id { int unused; };
static struct _cl_platform_id g_platform;
static struct _cl_device_id g_device;

struct _cl_context { int ref; };
struct _cl_command_queue { int ref; cl_context ctx; cl_command_queue_properties props; };
struct _cl_mem { int ref; cl_context ctx; cl_mem_flags flags; size_t size; void* data; int own; void* host_ptr; int maps; };
struct _cl_event { int ref; cl_command_queue q; cl_command_type type; cl_ulong t_queued, t_start, t_end; };

typedef struct {                       /* a kernel of a built program */
    char name[96];
    int nargs, collective;
    int is_local[MAXARGS];             /* __local pointer argument */
    int is_mem[MAXARGS];               /* __global / __constant pointer: a cl_mem */
    void (*call)(const void* wi, void** args);
} KINFO;

struct _cl_program {
    int ref;
    cl_context ctx;
    char* src;
    char* log;
    size_t loglen;
    cl_build_status status;
    char options[256];
    TCCState* tcc;
    KINFO* k;
    int nk;
};

typedef struct { unsigned char b[64]; } ARGV;

struct _cl_kernel {
    int ref;
    cl_program prog;
    KINFO* info;
    ARGV val[MAXARGS];
    size_t local_size[MAXARGS];
    cl_mem mem[MAXARGS];
    int set[MAXARGS];
};

/* the work-item: what get_global_id() and friends read (same layout in the prelude) */
typedef struct { size_t gid[3], lid[3], grp[3], gsz[3], lsz[3], ngr[3], off[3]; unsigned dim; void* group; unsigned lane; } WI;

/* ══ time, threads ═══════════════════════════════════════════════════ */

static cl_ulong now_ns(void) {
#ifdef BCL_HOST
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (cl_ulong)t.tv_sec * 1000000000ull + (cl_ulong)t.tv_nsec;
#else
    return (cl_ulong)banana_ticks() * 1000000ull;
#endif
}

static int ncpus(void) {
#ifdef BCL_HOST
    if (getenv("BCL_THREADS")) return atoi(getenv("BCL_THREADS"));
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? 1 : (int)n;
#else
    return banana_cpus();
#endif
}

typedef struct {
    cl_kernel k;
    size_t gsz[3], lsz[3], off[3], ngr[3];
    unsigned dim;
    size_t total;
    volatile long next;
    volatile int error;
} JOB;

/* Barrier helpers may be called from ordinary OpenCL helper functions too.
 * Work-item IDs still use the hidden argument for the common fast path. */
#ifdef BCL_HOST
static __thread const WI* g_current_wi;
static const WI* bcl_current(void) { return g_current_wi; }
static void set_current_wi(const WI* wi) { g_current_wi = wi; }
#else
static const WI* g_current_wi[65]; /* main thread and up to 64 app threads */
static const WI* bcl_current(void) { int n = banana_thread_id(); return n >= 0 && n <= 64 ? g_current_wi[n] : NULL; }
static void set_current_wi(const WI* wi) { int n = banana_thread_id(); if (n >= 0 && n <= 64) g_current_wi[n] = wi; }
#endif

#include "clgroup.h"

static JOB* g_job;
static int g_nthr;
#ifdef BCL_HOST
static sem_t g_start[MAXTHR], g_done;
static pthread_mutex_t g_build_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
#define DISPATCH_LOCK() pthread_mutex_lock(&g_dispatch_lock)
#define DISPATCH_UNLOCK() pthread_mutex_unlock(&g_dispatch_lock)
#define LOCK() pthread_mutex_lock(&g_build_lock)
#define UNLOCK() pthread_mutex_unlock(&g_build_lock)
#define SPOST(s) sem_post(&(s))
#define SWAIT(s) sem_wait(&(s))
#else
static banana_sem_t g_start[MAXTHR], g_done;
static banana_mutex_t g_build_lock = BANANA_MUTEX_INIT;
static banana_mutex_t g_dispatch_lock = BANANA_MUTEX_INIT;
#define DISPATCH_LOCK() banana_lock(&g_dispatch_lock)
#define DISPATCH_UNLOCK() banana_unlock(&g_dispatch_lock)
#define LOCK() banana_lock(&g_build_lock)
#define UNLOCK() banana_unlock(&g_build_lock)
#define SPOST(s) banana_sem_post(&(s))
#define SWAIT(s) banana_sem_wait(&(s))
#endif

/* runs work-groups until there are none left */
static void run_groups(JOB* j) {
    cl_kernel k = j->k;
    KINFO* ki = k->info;
    void* args[MAXARGS];
    void* ptrs[MAXARGS];
    void* local[MAXARGS] = { 0 };
    size_t local_bytes = 0;
    for (int i = 0; i < ki->nargs; i++) {
        local[i] = NULL;
        if (ki->is_local[i]) {
            local_bytes += k->local_size[i];
            if (posix_memalign(&local[i], 64, k->local_size[i] ? k->local_size[i] : 1)) {
                __sync_val_compare_and_swap(&j->error, 0, CL_OUT_OF_HOST_MEMORY);
                for (int k2 = 0; k2 < i; k2++) free(local[k2]);
                return;
            }
            ptrs[i] = local[i];
            args[i] = &ptrs[i];
        } else if (ki->is_mem[i]) {
            ptrs[i] = k->mem[i] ? k->mem[i]->data : NULL;
            args[i] = &ptrs[i];
        } else args[i] = k->val[i].b;
    }
    BCL_GROUP* group = NULL;
    if (ki->collective) {
        group = group_new(j->lsz[0] * j->lsz[1] * j->lsz[2], ki, args, local_bytes);
        if (!group) {
            __sync_val_compare_and_swap(&j->error, 0, CL_OUT_OF_HOST_MEMORY);
            for (int i = 0; i < ki->nargs; i++) free(local[i]);
            return;
        }
    }
    WI wi;
    memset(&wi, 0, sizeof(wi));
    wi.dim = j->dim;
    if (!group) set_current_wi(&wi);
    for (int d = 0; d < 3; d++) { wi.gsz[d] = j->gsz[d]; wi.lsz[d] = j->lsz[d]; wi.ngr[d] = j->ngr[d]; wi.off[d] = j->off[d]; }
    for (;;) {
        if (j->error) break;
        long g = __sync_fetch_and_add(&j->next, 1);
        if (g < 0 || (size_t)g >= j->total) break;
        size_t gg = (size_t)g;
        wi.grp[0] = gg % j->ngr[0];
        wi.grp[1] = (gg / j->ngr[0]) % j->ngr[1];
        wi.grp[2] = gg / (j->ngr[0] * j->ngr[1]);
        if (group) {
            int rc = group_run(group, &wi);
            if (rc != CL_SUCCESS) { __sync_val_compare_and_swap(&j->error, 0, rc); break; }
            continue;
        }
        for (size_t z = 0; z < j->lsz[2]; z++)
            for (size_t y = 0; y < j->lsz[1]; y++)
                for (size_t x = 0; x < j->lsz[0]; x++) {
                    wi.lid[0] = x; wi.lid[1] = y; wi.lid[2] = z;
                    for (int d = 0; d < 3; d++) wi.gid[d] = wi.grp[d] * j->lsz[d] + wi.lid[d] + j->off[d];
                    ki->call(&wi, args);
                }
    }
    set_current_wi(NULL);
    group_delete(group);
    for (int i = 0; i < ki->nargs; i++) free(local[i]);
}

#ifdef BCL_HOST
static void* worker(void* arg)
#else
static int worker(void* arg)
#endif
{
    int k = (int)(long)arg;
    for (;;) {
        SWAIT(g_start[k]);
        run_groups(g_job);
        SPOST(g_done);
    }
    return 0;
}

static void pool_start(void) {
    if (g_nthr) return;
    int n = ncpus();
    if (n < 1) n = 1;
    if (n > MAXTHR) n = MAXTHR;
    g_nthr = 1;
#ifdef BCL_HOST
    sem_init(&g_done, 0, 0);
#else
    banana_sem_init(&g_done, 0);
#endif
    for (int i = 1; i < n; i++) {
#ifdef BCL_HOST
        sem_init(&g_start[i], 0, 0);
        pthread_t t;
        if (pthread_create(&t, NULL, worker, (void*)(long)i) != 0) break;
        pthread_detach(t);
#else
        banana_sem_init(&g_start[i], 0);
        if (banana_thread(worker, (void*)(long)i) < 0) break;
#endif
        g_nthr++;
    }
}

static void run_job(JOB* j) {
    DISPATCH_LOCK();
    pool_start();
    int helpers = g_nthr - 1;
    if ((size_t)helpers > j->total - 1) helpers = (int)(j->total ? j->total - 1 : 0);
    g_job = j;
    for (int i = 1; i <= helpers; i++) SPOST(g_start[i]);
    run_groups(j);
    for (int i = 1; i <= helpers; i++) SWAIT(g_done);
    DISPATCH_UNLOCK();
}

/* ══ the info helper ═════════════════════════════════════════════════ */

static cl_int info(const void* v, size_t n, size_t size, void* value, size_t* size_ret) {
    if (size_ret) *size_ret = n;
    if (value) {
        if (size < n) return CL_INVALID_VALUE;
        memcpy(value, v, n);
    }
    return CL_SUCCESS;
}
static cl_int info_str(const char* s, size_t size, void* value, size_t* size_ret) { return info(s, strlen(s) + 1, size, value, size_ret); }
#define INFO_T(T, x) do { T _v = (T)(x); return info(&_v, sizeof(_v), size, value, size_ret); } while (0)
static void seterr(cl_int* err, cl_int e) { if (err) *err = e; }

/* ══ platform, device ════════════════════════════════════════════════ */

cl_int clGetPlatformIDs(cl_uint n, cl_platform_id* p, cl_uint* np) {
    if (!p && !np) return CL_INVALID_VALUE;
    if (p && n == 0) return CL_INVALID_VALUE;
    if (p) p[0] = &g_platform;
    if (np) *np = 1;
    return CL_SUCCESS;
}

cl_int clGetPlatformInfo(cl_platform_id pl, cl_platform_info param, size_t size, void* value, size_t* size_ret) {
    if (pl && pl != &g_platform) return CL_INVALID_PLATFORM;
    switch (param) {
    case CL_PLATFORM_PROFILE: return info_str("FULL_PROFILE", size, value, size_ret);
    case CL_PLATFORM_VERSION: return info_str("OpenCL 1.2 Banana OS", size, value, size_ret);
    case CL_PLATFORM_NAME: return info_str("Banana OS OpenCL", size, value, size_ret);
    case CL_PLATFORM_VENDOR: return info_str("Banana OS", size, value, size_ret);
    case CL_PLATFORM_EXTENSIONS: return info_str("", size, value, size_ret);
    }
    return CL_INVALID_VALUE;
}

cl_int clGetDeviceIDs(cl_platform_id pl, cl_device_type type, cl_uint n, cl_device_id* d, cl_uint* nd) {
    if (pl && pl != &g_platform) return CL_INVALID_PLATFORM;
    if (!d && !nd) return CL_INVALID_VALUE;
    if (!(type & (CL_DEVICE_TYPE_CPU | CL_DEVICE_TYPE_DEFAULT))) { if (nd) *nd = 0; return CL_DEVICE_NOT_FOUND; }
    if (d && n == 0) return CL_INVALID_VALUE;
    if (d) d[0] = &g_device;
    if (nd) *nd = 1;
    return CL_SUCCESS;
}

cl_int clGetDeviceInfo(cl_device_id dev, cl_device_info param, size_t size, void* value, size_t* size_ret) {
    if (dev && dev != &g_device) return CL_INVALID_DEVICE;
    pool_start();
    char name[96];
    switch (param) {
    case CL_DEVICE_TYPE: INFO_T(cl_device_type, CL_DEVICE_TYPE_CPU);
    case CL_DEVICE_VENDOR_ID: INFO_T(cl_uint, 0xBA4A);
    case CL_DEVICE_MAX_COMPUTE_UNITS: INFO_T(cl_uint, g_nthr);
    case CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS: INFO_T(cl_uint, 3);
    case CL_DEVICE_MAX_WORK_GROUP_SIZE: INFO_T(size_t, 1024);
    case CL_DEVICE_MAX_WORK_ITEM_SIZES: { size_t s[3] = { 1024, 1024, 1024 }; return info(s, sizeof(s), size, value, size_ret); }
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR: case CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT: case CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG:
    case CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT: case CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE: INFO_T(cl_uint, 1);
    case CL_DEVICE_MAX_CLOCK_FREQUENCY: INFO_T(cl_uint, 0);
    case CL_DEVICE_ADDRESS_BITS: INFO_T(cl_uint, sizeof(void*) * 8);
    case CL_DEVICE_MAX_READ_IMAGE_ARGS: case CL_DEVICE_MAX_WRITE_IMAGE_ARGS: case CL_DEVICE_MAX_SAMPLERS: INFO_T(cl_uint, 0);
    case CL_DEVICE_MAX_MEM_ALLOC_SIZE: INFO_T(cl_ulong, 256ull << 20);
    case CL_DEVICE_IMAGE_SUPPORT: INFO_T(cl_bool, CL_FALSE);
    case CL_DEVICE_MAX_PARAMETER_SIZE: INFO_T(size_t, MAXARGS * 64);
    case CL_DEVICE_MEM_BASE_ADDR_ALIGN: INFO_T(cl_uint, 128 * 8);
    case CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE: INFO_T(cl_uint, 128);
    case CL_DEVICE_SINGLE_FP_CONFIG: case CL_DEVICE_DOUBLE_FP_CONFIG:
        INFO_T(cl_device_fp_config, CL_FP_DENORM | CL_FP_INF_NAN | CL_FP_ROUND_TO_NEAREST);
    case CL_DEVICE_GLOBAL_MEM_CACHE_TYPE: INFO_T(cl_device_mem_cache_type, CL_READ_WRITE_CACHE);
    case CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE: INFO_T(cl_uint, 64);
    case CL_DEVICE_GLOBAL_MEM_CACHE_SIZE: INFO_T(cl_ulong, 256 * 1024);
    case CL_DEVICE_GLOBAL_MEM_SIZE: INFO_T(cl_ulong, 1024ull << 20);
    case CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE: INFO_T(cl_ulong, 64 * 1024);
    case CL_DEVICE_MAX_CONSTANT_ARGS: INFO_T(cl_uint, 8);
    case CL_DEVICE_LOCAL_MEM_TYPE: INFO_T(cl_device_local_mem_type, CL_GLOBAL);
    case CL_DEVICE_LOCAL_MEM_SIZE: INFO_T(cl_ulong, 32 * 1024);
    case CL_DEVICE_ERROR_CORRECTION_SUPPORT: INFO_T(cl_bool, CL_FALSE);
    case CL_DEVICE_PROFILING_TIMER_RESOLUTION: INFO_T(size_t, 1000000);
    case CL_DEVICE_ENDIAN_LITTLE: case CL_DEVICE_AVAILABLE: case CL_DEVICE_COMPILER_AVAILABLE:
    case CL_DEVICE_HOST_UNIFIED_MEMORY: INFO_T(cl_bool, CL_TRUE);
    case CL_DEVICE_LINKER_AVAILABLE: INFO_T(cl_bool, CL_FALSE);
    case CL_DEVICE_EXECUTION_CAPABILITIES: INFO_T(cl_device_exec_capabilities, CL_EXEC_KERNEL);
    case CL_DEVICE_QUEUE_PROPERTIES: INFO_T(cl_command_queue_properties, CL_QUEUE_PROFILING_ENABLE);
    case CL_DEVICE_NAME:
        snprintf(name, sizeof(name), "Banana OS CPU (%d core%s)", g_nthr, g_nthr == 1 ? "" : "s");
        return info_str(name, size, value, size_ret);
    case CL_DEVICE_VENDOR: return info_str("Banana OS", size, value, size_ret);
    case CL_DRIVER_VERSION: return info_str("1.0", size, value, size_ret);
    case CL_DEVICE_PROFILE: return info_str("FULL_PROFILE", size, value, size_ret);
    case CL_DEVICE_VERSION: return info_str("OpenCL 1.2 Banana OS", size, value, size_ret);
    case CL_DEVICE_OPENCL_C_VERSION: return info_str("OpenCL C 1.2", size, value, size_ret);
    case CL_DEVICE_EXTENSIONS: return info_str("", size, value, size_ret);
    case CL_DEVICE_PLATFORM: INFO_T(cl_platform_id, &g_platform);
    }
    return CL_INVALID_VALUE;
}
cl_int clRetainDevice(cl_device_id d) { return d == &g_device ? CL_SUCCESS : CL_INVALID_DEVICE; }
cl_int clReleaseDevice(cl_device_id d) { return d == &g_device ? CL_SUCCESS : CL_INVALID_DEVICE; }

/* ══ contexts and queues ═════════════════════════════════════════════ */

cl_context clCreateContext(const cl_context_properties* props, cl_uint nd, const cl_device_id* d,
                           void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* ud, cl_int* err) {
    (void)props; (void)notify; (void)ud;
    if (!nd || !d) { seterr(err, CL_INVALID_VALUE); return NULL; }
    for (cl_uint i = 0; i < nd; i++) if (d[i] != &g_device) { seterr(err, CL_INVALID_DEVICE); return NULL; }
    cl_context c = (cl_context)calloc(1, sizeof(*c));
    if (!c) { seterr(err, CL_OUT_OF_HOST_MEMORY); return NULL; }
    c->ref = 1;
    seterr(err, CL_SUCCESS);
    return c;
}
cl_context clCreateContextFromType(const cl_context_properties* props, cl_device_type type,
                                   void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* ud, cl_int* err) {
    if (!(type & (CL_DEVICE_TYPE_CPU | CL_DEVICE_TYPE_DEFAULT))) { seterr(err, CL_DEVICE_NOT_FOUND); return NULL; }
    cl_device_id d = &g_device;
    return clCreateContext(props, 1, &d, notify, ud, err);
}
cl_int clRetainContext(cl_context c) { if (!c) return CL_INVALID_CONTEXT; c->ref++; return CL_SUCCESS; }
cl_int clReleaseContext(cl_context c) { if (!c) return CL_INVALID_CONTEXT; if (--c->ref == 0) free(c); return CL_SUCCESS; }
cl_int clGetContextInfo(cl_context c, cl_context_info param, size_t size, void* value, size_t* size_ret) {
    if (!c) return CL_INVALID_CONTEXT;
    switch (param) {
    case CL_CONTEXT_REFERENCE_COUNT: INFO_T(cl_uint, c->ref);
    case CL_CONTEXT_DEVICES: INFO_T(cl_device_id, &g_device);
    case CL_CONTEXT_NUM_DEVICES: INFO_T(cl_uint, 1);
    case CL_CONTEXT_PROPERTIES: if (size_ret) *size_ret = 0; return CL_SUCCESS;
    }
    return CL_INVALID_VALUE;
}

cl_command_queue clCreateCommandQueue(cl_context c, cl_device_id d, cl_command_queue_properties props, cl_int* err) {
    if (!c) { seterr(err, CL_INVALID_CONTEXT); return NULL; }
    if (d != &g_device) { seterr(err, CL_INVALID_DEVICE); return NULL; }
    cl_command_queue q = (cl_command_queue)calloc(1, sizeof(*q));
    if (!q) { seterr(err, CL_OUT_OF_HOST_MEMORY); return NULL; }
    q->ref = 1;
    q->ctx = c;
    q->props = props;
    c->ref++;
    seterr(err, CL_SUCCESS);
    return q;
}
cl_command_queue clCreateCommandQueueWithProperties(cl_context c, cl_device_id d, const cl_queue_properties* props, cl_int* err) {
    cl_command_queue_properties p = 0;
    for (int i = 0; props && props[i]; i += 2) if (props[i] == CL_QUEUE_PROPERTIES) p = props[i + 1];
    return clCreateCommandQueue(c, d, p, err);
}
cl_int clRetainCommandQueue(cl_command_queue q) { if (!q) return CL_INVALID_COMMAND_QUEUE; q->ref++; return CL_SUCCESS; }
cl_int clReleaseCommandQueue(cl_command_queue q) {
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    if (--q->ref == 0) { clReleaseContext(q->ctx); free(q); }
    return CL_SUCCESS;
}
cl_int clGetCommandQueueInfo(cl_command_queue q, cl_command_queue_info param, size_t size, void* value, size_t* size_ret) {
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    switch (param) {
    case CL_QUEUE_CONTEXT: INFO_T(cl_context, q->ctx);
    case CL_QUEUE_DEVICE: INFO_T(cl_device_id, &g_device);
    case CL_QUEUE_REFERENCE_COUNT: INFO_T(cl_uint, q->ref);
    case CL_QUEUE_PROPERTIES: INFO_T(cl_command_queue_properties, q->props);
    }
    return CL_INVALID_VALUE;
}
cl_int clFlush(cl_command_queue q) { return q ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE; }
cl_int clFinish(cl_command_queue q) { return q ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE; }

/* ══ events: every command is done when its call returns ════════════ */

static cl_event make_event(cl_command_queue q, cl_command_type t, cl_ulong t0, cl_event* out) {
    if (!out) return NULL;
    cl_event e = (cl_event)calloc(1, sizeof(*e));
    if (!e) { *out = NULL; return NULL; }
    e->ref = 1;
    e->q = q;
    e->type = t;
    e->t_queued = e->t_start = t0;
    e->t_end = now_ns();
    *out = e;
    return e;
}
cl_int clWaitForEvents(cl_uint n, const cl_event* ev) { if (!n || !ev) return CL_INVALID_VALUE; return CL_SUCCESS; }
cl_int clGetEventInfo(cl_event e, cl_event_info param, size_t size, void* value, size_t* size_ret) {
    if (!e) return CL_INVALID_EVENT;
    switch (param) {
    case CL_EVENT_COMMAND_QUEUE: INFO_T(cl_command_queue, e->q);
    case CL_EVENT_CONTEXT: INFO_T(cl_context, e->q ? e->q->ctx : NULL);
    case CL_EVENT_COMMAND_TYPE: INFO_T(cl_command_type, e->type);
    case CL_EVENT_REFERENCE_COUNT: INFO_T(cl_uint, e->ref);
    case CL_EVENT_COMMAND_EXECUTION_STATUS: INFO_T(cl_int, CL_COMPLETE);
    }
    return CL_INVALID_VALUE;
}
cl_int clGetEventProfilingInfo(cl_event e, cl_profiling_info param, size_t size, void* value, size_t* size_ret) {
    if (!e) return CL_INVALID_EVENT;
    switch (param) {
    case CL_PROFILING_COMMAND_QUEUED: case CL_PROFILING_COMMAND_SUBMIT: INFO_T(cl_ulong, e->t_queued);
    case CL_PROFILING_COMMAND_START: INFO_T(cl_ulong, e->t_start);
    case CL_PROFILING_COMMAND_END: INFO_T(cl_ulong, e->t_end);
    }
    return CL_INVALID_VALUE;
}
cl_int clRetainEvent(cl_event e) { if (!e) return CL_INVALID_EVENT; e->ref++; return CL_SUCCESS; }
cl_int clReleaseEvent(cl_event e) { if (!e) return CL_INVALID_EVENT; if (--e->ref == 0) free(e); return CL_SUCCESS; }
cl_int clEnqueueMarker(cl_command_queue q, cl_event* ev) { if (!q) return CL_INVALID_COMMAND_QUEUE; make_event(q, CL_COMMAND_MARKER, now_ns(), ev); return CL_SUCCESS; }
cl_int clEnqueueBarrier(cl_command_queue q) { return q ? CL_SUCCESS : CL_INVALID_COMMAND_QUEUE; }
cl_int clEnqueueMarkerWithWaitList(cl_command_queue q, cl_uint n, const cl_event* w, cl_event* ev) { (void)n; (void)w; return clEnqueueMarker(q, ev); }
cl_int clEnqueueBarrierWithWaitList(cl_command_queue q, cl_uint n, const cl_event* w, cl_event* ev) {
    (void)n; (void)w;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    make_event(q, CL_COMMAND_BARRIER, now_ns(), ev);
    return CL_SUCCESS;
}

/* ══ buffers ═════════════════════════════════════════════════════════ */

cl_mem clCreateBuffer(cl_context c, cl_mem_flags flags, size_t size, void* host_ptr, cl_int* err) {
    if (!c) { seterr(err, CL_INVALID_CONTEXT); return NULL; }
    if (size == 0) { seterr(err, CL_INVALID_BUFFER_SIZE); return NULL; }
    int use = (flags & CL_MEM_USE_HOST_PTR) != 0, copy = (flags & CL_MEM_COPY_HOST_PTR) != 0;
    if ((use || copy) != (host_ptr != NULL) || (use && copy)) { seterr(err, CL_INVALID_HOST_PTR); return NULL; }
    cl_mem m = (cl_mem)calloc(1, sizeof(*m));
    if (!m) { seterr(err, CL_OUT_OF_HOST_MEMORY); return NULL; }
    m->ref = 1;
    m->ctx = c;
    m->flags = flags;
    m->size = size;
    m->host_ptr = host_ptr;
    if (use) m->data = host_ptr;
    else {
        m->data = calloc(1, size);
        if (!m->data) { free(m); seterr(err, CL_MEM_OBJECT_ALLOCATION_FAILURE); return NULL; }
        m->own = 1;
        if (copy) memcpy(m->data, host_ptr, size);
    }
    c->ref++;
    seterr(err, CL_SUCCESS);
    return m;
}
cl_int clRetainMemObject(cl_mem m) { if (!m) return CL_INVALID_MEM_OBJECT; m->ref++; return CL_SUCCESS; }
cl_int clReleaseMemObject(cl_mem m) {
    if (!m) return CL_INVALID_MEM_OBJECT;
    if (--m->ref == 0) { if (m->own) free(m->data); clReleaseContext(m->ctx); free(m); }
    return CL_SUCCESS;
}
cl_int clGetMemObjectInfo(cl_mem m, cl_mem_info param, size_t size, void* value, size_t* size_ret) {
    if (!m) return CL_INVALID_MEM_OBJECT;
    switch (param) {
    case CL_MEM_TYPE: INFO_T(cl_mem_object_type, CL_MEM_OBJECT_BUFFER);
    case CL_MEM_FLAGS: INFO_T(cl_mem_flags, m->flags);
    case CL_MEM_SIZE: INFO_T(size_t, m->size);
    case CL_MEM_HOST_PTR: INFO_T(void*, m->host_ptr);
    case CL_MEM_MAP_COUNT: INFO_T(cl_uint, m->maps);
    case CL_MEM_REFERENCE_COUNT: INFO_T(cl_uint, m->ref);
    case CL_MEM_CONTEXT: INFO_T(cl_context, m->ctx);
    }
    return CL_INVALID_VALUE;
}
static cl_int range_ok(cl_mem m, size_t off, size_t size) {
    if (!m) return CL_INVALID_MEM_OBJECT;
    if (off > m->size || size > m->size - off) return CL_INVALID_VALUE;
    return CL_SUCCESS;
}
cl_int clEnqueueReadBuffer(cl_command_queue q, cl_mem m, cl_bool blocking, size_t off, size_t size, void* ptr,
                           cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)blocking; (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    cl_int r = range_ok(m, off, size);
    if (r) return r;
    if (!ptr) return CL_INVALID_VALUE;
    cl_ulong t0 = now_ns();
    memmove(ptr, (char*)m->data + off, size);
    make_event(q, CL_COMMAND_READ_BUFFER, t0, ev);
    return CL_SUCCESS;
}
cl_int clEnqueueWriteBuffer(cl_command_queue q, cl_mem m, cl_bool blocking, size_t off, size_t size, const void* ptr,
                            cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)blocking; (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    cl_int r = range_ok(m, off, size);
    if (r) return r;
    if (!ptr) return CL_INVALID_VALUE;
    cl_ulong t0 = now_ns();
    memmove((char*)m->data + off, ptr, size);
    make_event(q, CL_COMMAND_WRITE_BUFFER, t0, ev);
    return CL_SUCCESS;
}
cl_int clEnqueueCopyBuffer(cl_command_queue q, cl_mem s, cl_mem d, size_t so, size_t dof, size_t size,
                           cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    cl_int r = range_ok(s, so, size);
    if (!r) r = range_ok(d, dof, size);
    if (r) return r;
    if (s == d && ((so <= dof && dof < so + size) || (dof <= so && so < dof + size))) return CL_MEM_COPY_OVERLAP;
    cl_ulong t0 = now_ns();
    memmove((char*)d->data + dof, (char*)s->data + so, size);
    make_event(q, CL_COMMAND_COPY_BUFFER, t0, ev);
    return CL_SUCCESS;
}
cl_int clEnqueueFillBuffer(cl_command_queue q, cl_mem m, const void* pat, size_t ps, size_t off, size_t size,
                           cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    cl_int r = range_ok(m, off, size);
    if (r) return r;
    if (!pat || !ps || size % ps || off % ps) return CL_INVALID_VALUE;
    cl_ulong t0 = now_ns();
    for (size_t i = 0; i < size; i += ps) memcpy((char*)m->data + off + i, pat, ps);
    make_event(q, CL_COMMAND_FILL_BUFFER, t0, ev);
    return CL_SUCCESS;
}
void* clEnqueueMapBuffer(cl_command_queue q, cl_mem m, cl_bool blocking, cl_map_flags flags, size_t off, size_t size,
                         cl_uint ne, const cl_event* wl, cl_event* ev, cl_int* err) {
    (void)blocking; (void)flags; (void)ne; (void)wl;
    if (!q) { seterr(err, CL_INVALID_COMMAND_QUEUE); return NULL; }
    cl_int r = range_ok(m, off, size);
    if (r) { seterr(err, r); return NULL; }
    m->maps++;
    make_event(q, CL_COMMAND_MAP_BUFFER, now_ns(), ev);
    seterr(err, CL_SUCCESS);
    return (char*)m->data + off;                 /* (one memory: the buffer itself) */
}
cl_int clEnqueueUnmapMemObject(cl_command_queue q, cl_mem m, void* ptr, cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)ptr; (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    if (!m) return CL_INVALID_MEM_OBJECT;
    if (m->maps > 0) m->maps--;
    make_event(q, CL_COMMAND_UNMAP_MEM_OBJECT, now_ns(), ev);
    return CL_SUCCESS;
}

/* ══ programs: OpenCL C through TinyCC ═══════════════════════════════ */

static const char* PRELUDE =
    "#define __OPENCL_VERSION__ 120\n#define __OPENCL_C_VERSION__ 120\n#define CL_VERSION_1_0 100\n"
    "#define CL_VERSION_1_1 110\n#define CL_VERSION_1_2 120\n#define __ENDIAN_LITTLE__ 1\n#define __BANANA_OS__ 1\n"
    "#define __kernel\n#define kernel\n#define __global\n#define global\n#define __constant const\n#define constant const\n"
    "#define __local\n#define local\n#define __private\n#define private\n#define __read_only\n#define __write_only\n"
    "#define __read_write\n#define restrict\n#define __restrict\n"
    "typedef __SIZE_TYPE__ size_t;\ntypedef unsigned char uchar;\ntypedef unsigned short ushort;\n"
    "typedef unsigned int uint;\ntypedef unsigned long ulong;\ntypedef unsigned short half;\n"
    "#define bool _Bool\n#define true 1\n#define false 0\n#define NULL ((void*)0)\n"
    "typedef struct { size_t gid[3], lid[3], grp[3], gsz[3], lsz[3], ngr[3], off[3]; unsigned dim; void* group; unsigned lane; } __bcl_wi;\n"
    "#define get_global_id(d) (__wi->gid[(d)])\n#define get_local_id(d) (__wi->lid[(d)])\n"
    "#define get_group_id(d) (__wi->grp[(d)])\n#define get_global_size(d) (__wi->gsz[(d)])\n"
    "#define get_local_size(d) (__wi->lsz[(d)])\n#define get_num_groups(d) (__wi->ngr[(d)])\n"
    "#define get_global_offset(d) (__wi->off[(d)])\n#define get_work_dim() (__wi->dim)\n"
    "#define CLK_LOCAL_MEM_FENCE 1\n#define CLK_GLOBAL_MEM_FENCE 2\n"
    "void __bcl_barrier(const __bcl_wi*, int, int); void* __bcl_local(const __bcl_wi*, unsigned, size_t);\n"
    "const __bcl_wi* __bcl_current(void);\n#define barrier(f) __bcl_barrier(__bcl_current(), (f), __COUNTER__)\n"
    "void __bcl_fence(void);\n#define mem_fence(f) ((void)(f),__bcl_fence())\n#define read_mem_fence(f) mem_fence(f)\n#define write_mem_fence(f) mem_fence(f)\n"
    "double sqrt(double); double sin(double); double cos(double); double tan(double); double asin(double);\n"
    "double acos(double); double atan(double); double atan2(double, double); double exp(double); double exp2(double);\n"
    "double log(double); double log2(double); double log10(double); double pow(double, double); double fabs(double);\n"
    "double floor(double); double ceil(double); double round(double); double trunc(double); double fmod(double, double);\n"
    "double fmin(double, double); double fmax(double, double); double hypot(double, double); double sinh(double);\n"
    "double cosh(double); double tanh(double); double cbrt(double); double copysign(double, double);\n"
    "float sqrtf(float); float sinf(float); float cosf(float); float expf(float); float logf(float); float powf(float, float);\n"
    "float fabsf(float); float floorf(float); float ceilf(float);\n"
    "#define rsqrt(x) (1.0f / sqrt(x))\n#define native_sqrt(x) sqrt(x)\n#define native_rsqrt(x) rsqrt(x)\n"
    "#define native_sin(x) sin(x)\n#define native_cos(x) cos(x)\n#define native_tan(x) tan(x)\n#define native_exp(x) exp(x)\n"
    "#define native_log(x) log(x)\n#define native_powr(x, y) pow(x, y)\n#define powr(x, y) pow(x, y)\n#define pown(x, n) pow(x, n)\n"
    "#define native_divide(a, b) ((a) / (b))\n#define native_recip(x) (1.0f / (x))\n"
    "#define half_sqrt(x) sqrt(x)\n#define half_exp(x) exp(x)\n#define half_log(x) log(x)\n"
    "#define mad(a, b, c) ((a) * (b) + (c))\n#define fma(a, b, c) ((a) * (b) + (c))\n"
    "#define min(a, b) ((a) < (b) ? (a) : (b))\n#define max(a, b) ((a) > (b) ? (a) : (b))\n"
    "#define clamp(x, lo, hi) min(max((x), (lo)), (hi))\n#define mix(a, b, t) ((a) + ((b) - (a)) * (t))\n"
    "#define step(e, x) ((x) < (e) ? 0.0f : 1.0f)\n#define sign(x) ((x) > 0 ? 1.0f : (x) < 0 ? -1.0f : 0.0f)\n"
    "#define degrees(r) ((r) * 57.295779513f)\n#define radians(d) ((d) * 0.0174532925f)\n"
    "#define abs(x) ((x) < 0 ? -(x) : (x))\n#define select(a, b, c) ((c) ? (b) : (a))\n"
    "#define smoothstep(e0, e1, x) __bcl_smooth((float)(e0), (float)(e1), (float)(x))\n"
    "static float __bcl_smooth(float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }\n"
    "#define M_PI_F 3.14159265f\n#define M_E_F 2.71828183f\n#define M_PI 3.14159265358979323846\n"
    "#define MAXFLOAT 3.402823466e+38f\n#define FLT_MAX 3.402823466e+38f\n#define FLT_MIN 1.175494351e-38f\n"
    "#define FLT_EPSILON 1.192092896e-07f\n#define INT_MAX 2147483647\n#define INT_MIN (-2147483647 - 1)\n#define UINT_MAX 4294967295u\n"
    "int atomic_add(volatile int*, int); int atomic_sub(volatile int*, int); int atomic_xchg(volatile int*, int);\n"
    "int atomic_inc(volatile int*); int atomic_dec(volatile int*); int atomic_min(volatile int*, int);\n"
    "int atomic_max(volatile int*, int); int atomic_and(volatile int*, int); int atomic_or(volatile int*, int);\n"
    "int atomic_xor(volatile int*, int); int atomic_cmpxchg(volatile int*, int, int);\n"
    "#define atom_add atomic_add\n#define atom_sub atomic_sub\n#define atom_xchg atomic_xchg\n#define atom_inc atomic_inc\n"
    "#define atom_dec atomic_dec\n#define atom_min atomic_min\n#define atom_max atomic_max\n#define atom_cmpxchg atomic_cmpxchg\n"
    "int printf(const char*, ...);\n"
    "#line 1 \"program.cl\"\n";

static int a_add(volatile int* p, int v) { return __sync_fetch_and_add(p, v); }
static int a_sub(volatile int* p, int v) { return __sync_fetch_and_sub(p, v); }
static int a_xchg(volatile int* p, int v) { return __sync_lock_test_and_set(p, v); }
static int a_inc(volatile int* p) { return __sync_fetch_and_add(p, 1); }
static int a_dec(volatile int* p) { return __sync_fetch_and_sub(p, 1); }
static int a_and(volatile int* p, int v) { return __sync_fetch_and_and(p, v); }
static int a_or(volatile int* p, int v) { return __sync_fetch_and_or(p, v); }
static int a_xor(volatile int* p, int v) { return __sync_fetch_and_xor(p, v); }
static int a_cmpxchg(volatile int* p, int c, int v) { return __sync_val_compare_and_swap(p, c, v); }
static int a_min(volatile int* p, int v) {
    int o = *p;
    while (v < o) { int s = __sync_val_compare_and_swap(p, o, v); if (s == o) break; o = s; }
    return o;
}
static int a_max(volatile int* p, int v) {
    int o = *p;
    while (v > o) { int s = __sync_val_compare_and_swap(p, o, v); if (s == o) break; o = s; }
    return o;
}

static float rt_floatundisf(unsigned long long v) { return (float)v; }
static double rt_floatundidf(unsigned long long v) { return (double)v; }
static long double rt_floatundixf(unsigned long long v) { return (long double)v; }
static float rt_floatdisf(long long v) { return (float)v; }
static double rt_floatdidf(long long v) { return (double)v; }
static unsigned long long rt_fixunssfdi(float f) { return (unsigned long long)f; }
static unsigned long long rt_fixunsdfdi(double d) { return (unsigned long long)d; }
static unsigned long long rt_fixunsxfdi(long double d) { return (unsigned long long)d; }
static long long rt_fixsfdi(float f) { return (long long)f; }
static long long rt_fixdfdi(double d) { return (long long)d; }
static long long rt_fixxfdi(long double d) { return (long long)d; }
static long long rt_divdi3(long long a, long long b) { return a / b; }
static unsigned long long rt_udivdi3(unsigned long long a, unsigned long long b) { return a / b; }
static long long rt_moddi3(long long a, long long b) { return a % b; }
static unsigned long long rt_umoddi3(unsigned long long a, unsigned long long b) { return a % b; }
static long long rt_ashldi3(long long a, int b) { return (long long)((unsigned long long)a << b); }
static long long rt_ashrdi3(long long a, int b) { return a >> b; }
static unsigned long long rt_lshrdi3(unsigned long long a, int b) { return a >> b; }

static void log_add(cl_program p, const char* s) {
    size_t n = strlen(s);
    char* l = (char*)realloc(p->log, p->loglen + n + 2);
    if (!l) return;
    memcpy(l + p->loglen, s, n);
    p->loglen += n;
    l[p->loglen++] = '\n';
    l[p->loglen] = 0;
    p->log = l;
}
static void tcc_err(void* opaque, const char* msg) { log_add((cl_program)opaque, msg); }

cl_program clCreateProgramWithSource(cl_context c, cl_uint count, const char** strings, const size_t* lengths, cl_int* err) {
    if (!c) { seterr(err, CL_INVALID_CONTEXT); return NULL; }
    if (!count || !strings) { seterr(err, CL_INVALID_VALUE); return NULL; }
    size_t total = 0;
    for (cl_uint i = 0; i < count; i++) {
        if (!strings[i]) { seterr(err, CL_INVALID_VALUE); return NULL; }
        total += lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
    }
    cl_program p = (cl_program)calloc(1, sizeof(*p));
    char* s = (char*)malloc(total + 2);
    if (!p || !s) { free(p); free(s); seterr(err, CL_OUT_OF_HOST_MEMORY); return NULL; }
    size_t o = 0;
    for (cl_uint i = 0; i < count; i++) {
        size_t n = lengths && lengths[i] ? lengths[i] : strlen(strings[i]);
        memcpy(s + o, strings[i], n);
        o += n;
    }
    s[o] = 0;
    p->ref = 1;
    p->ctx = c;
    p->src = s;
    p->status = CL_BUILD_NONE;
    c->ref++;
    seterr(err, CL_SUCCESS);
    return p;
}

static int is_ident(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static const char* skip_ws(const char* s) {
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (s[0] == '/' && s[1] == '/') { while (*s && *s != '\n') s++; continue; }
        if (s[0] == '/' && s[1] == '*') { s += 2; while (*s && !(s[0] == '*' && s[1] == '/')) s++; if (*s) s += 2; continue; }
        return s;
    }
}
static int word_at(const char* src, const char* s, const char* w) {
    size_t n = strlen(w);
    return strncmp(s, w, n) == 0 && !is_ident((unsigned char)s[n]) && (s == src || !is_ident((unsigned char)s[-1]));
}

typedef struct { char* buf; size_t n, cap; int oom; } SB;
static void sb_put(SB* b, const char* s, size_t n) {
    if (b->oom) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = (b->n + n + 1) * 2;
        char* nb = (char*)realloc(b->buf, cap);
        if (!nb) { b->oom = 1; return; }
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->n, s, n);
    b->n += n;
    b->buf[b->n] = 0;
}
static void sb_str(SB* b, const char* s) { sb_put(b, s, strlen(s)); }

#include "cllocal.h"
#include "clvector-prelude.h"

/* one parameter's text: its type (cast form) and what it is */
static int param(KINFO* k, int i, const char* s, size_t n, SB* tramp) {
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\n')) n--;
    while (n && (*s == ' ' || *s == '\t' || *s == '\n')) { s++; n--; }
    int arr = n && s[n - 1] == ']';
    if (arr) { while (n && s[n - 1] != '[') n--; if (n) n--; while (n && s[n - 1] == ' ') n--; }
    size_t e = n;
    while (e && is_ident((unsigned char)s[e - 1])) e--;      /* the name */
    char type[256];
    size_t tl = e < sizeof(type) - 4 ? e : sizeof(type) - 4;
    memcpy(type, s, tl);
    type[tl] = 0;
    if (arr) strcat(type, "*");
    int ptr = strchr(type, '*') != NULL;
    int local = 0;
    for (const char* q = type; *q; q++)
        if ((word_at(type, q, "__local") || word_at(type, q, "local")) && ptr) local = 1;
    k->is_local[i] = local;
    k->is_mem[i] = ptr && !local;
    if (tramp) {
        char tmp[320];
        snprintf(tmp, sizeof(tmp), "%s*(%s*)a[%d]", i ? ", " : "", type, i);
        sb_str(tramp, tmp);
    }
    return 0;
}

/* finds the kernels, rewrites them to take the work-item first, writes their trampolines */
static int scan_kernels(cl_program p, SB* out, SB* tramp) {
    const char* src = p->src;
    const char* s = src;
    const char* copied = src;
    int nk = 0, cap = 0;
    while (*s) {
        if (s[0] == '/' && (s[1] == '/' || s[1] == '*')) { s = skip_ws(s); continue; }
        if (*s == '"') { s++; while (*s && *s != '"') { if (*s == '\\' && s[1]) s++; s++; } if (*s) s++; continue; }
        if (!(word_at(src, s, "__kernel") || word_at(src, s, "kernel"))) { s++; continue; }
        const char* q = skip_ws(s + (s[0] == '_' ? 8 : 6));
        while (word_at(src, q, "__attribute__")) {             /* __attribute__((reqd_work_group_size(...))) */
            q = skip_ws(q + 13);
            int depth = 0;
            do { if (*q == '(') depth++; else if (*q == ')') depth--; q++; } while (*q && depth > 0);
            q = skip_ws(q);
        }
        if (!word_at(src, q, "void")) { s = q; continue; }
        q = skip_ws(q + 4);
        const char* name = q;
        while (is_ident((unsigned char)*q)) q++;
        size_t nl = (size_t)(q - name);
        q = skip_ws(q);
        if (!nl || *q != '(') { s = q; continue; }
        const char* open = q;
        int depth = 0;
        const char* close = open;
        do { if (*close == '(') depth++; else if (*close == ')') depth--; close++; } while (*close && depth > 0);
        const char* after = skip_ws(close);
        int is_def = *after == '{';
        /* copy up to the parameters, then the hidden one */
        sb_put(out, copied, (size_t)(open + 1 - copied));
        const char* ps = open + 1;
        size_t pn = (size_t)(close - 1 - ps);
        const char* pt = skip_ws(ps);
        int none = (size_t)(pt - ps) >= pn || (word_at(src, pt, "void") && *skip_ws(pt + 4) == ')');
        sb_str(out, none ? "const __bcl_wi* __wi" : "const __bcl_wi* __wi, ");
        if (!none) sb_put(out, ps, pn);
        sb_str(out, ")");
        copied = close;
        s = close;
        if (!is_def) continue;
        if (nk == cap) {
            cap = cap ? cap * 2 : 8;
            KINFO* nk2 = (KINFO*)realloc(p->k, sizeof(KINFO) * (size_t)cap);
            if (!nk2) return -1;
            p->k = nk2;
        }
        KINFO* k = &p->k[nk];
        memset(k, 0, sizeof(*k));
        /* Any synchronization/local declaration in this program can also
         * be in a helper or macro. Conservatively schedule its kernels
         * collectively rather than silently executing barriers serially. */
        k->collective = strstr(src, "__bcl_local") != NULL || strstr(src, "barrier") != NULL || strstr(src, "__local") != NULL || strstr(src, "local ") != NULL ||
                        strstr(p->options, "barrier") != NULL || strstr(p->options, "local") != NULL;
        snprintf(k->name, sizeof(k->name), "%.*s", (int)nl, name);
        char head[256];
        snprintf(head, sizeof(head), "void __bcl_call_%s(const __bcl_wi* w, void** a) { %s(w%s", k->name, k->name, none ? "" : ", ");
        sb_str(tramp, head);
        if (!none) {
            int d2 = 0, i = 0;
            const char* st = ps;
            for (const char* c = ps; c <= ps + pn; c++) {
                if (c < ps + pn && (*c == '(' || *c == '[')) d2++;
                else if (c < ps + pn && (*c == ')' || *c == ']')) d2--;
                if (c == ps + pn || (*c == ',' && d2 == 0)) {
                    if (i >= MAXARGS) { log_add(p, "too many kernel arguments (32 at most)"); return -1; }
                    param(k, i, st, (size_t)(c - st), tramp);
                    i++;
                    st = c + 1;
                }
            }
            k->nargs = i;
        }
        sb_str(tramp, "); }\n");
        nk++;
    }
    sb_put(out, copied, strlen(copied));
    p->nk = nk;
    return 0;
}

static void bcl_fence(void) { __sync_synchronize(); }

static void add_symbols(TCCState* t) {
    tcc_add_symbol(t, "__bcl_current", (const void*)bcl_current);
    tcc_add_symbol(t, "__bcl_barrier", (const void*)bcl_barrier);
    tcc_add_symbol(t, "__bcl_local", (const void*)bcl_local);
    tcc_add_symbol(t, "__bcl_fence", (const void*)bcl_fence);
#define SYM(n) tcc_add_symbol(t, #n, (const void*)n)
    SYM(sqrt); SYM(sin); SYM(cos); SYM(tan); SYM(asin); SYM(acos); SYM(atan); SYM(atan2); SYM(exp); SYM(exp2);
    SYM(log); SYM(log2); SYM(log10); SYM(pow); SYM(fabs); SYM(floor); SYM(ceil); SYM(round); SYM(trunc); SYM(fmod);
    SYM(fmin); SYM(fmax); SYM(hypot); SYM(sinh); SYM(cosh); SYM(tanh); SYM(cbrt); SYM(copysign);
    SYM(sqrtf); SYM(sinf); SYM(cosf); SYM(expf); SYM(logf); SYM(powf); SYM(fabsf); SYM(floorf); SYM(ceilf);
    SYM(memcpy); SYM(memset); SYM(memmove); SYM(printf);
#undef SYM
    tcc_add_symbol(t, "atomic_add", (const void*)a_add);
    tcc_add_symbol(t, "atomic_sub", (const void*)a_sub);
    tcc_add_symbol(t, "atomic_xchg", (const void*)a_xchg);
    tcc_add_symbol(t, "atomic_inc", (const void*)a_inc);
    tcc_add_symbol(t, "atomic_dec", (const void*)a_dec);
    tcc_add_symbol(t, "atomic_min", (const void*)a_min);
    tcc_add_symbol(t, "atomic_max", (const void*)a_max);
    tcc_add_symbol(t, "atomic_and", (const void*)a_and);
    tcc_add_symbol(t, "atomic_or", (const void*)a_or);
    tcc_add_symbol(t, "atomic_xor", (const void*)a_xor);
    tcc_add_symbol(t, "atomic_cmpxchg", (const void*)a_cmpxchg);
    /* what TinyCC's code calls for 64-bit arithmetic and conversions (its
     * libtcc1 is not linked in: these come from the app, compiled by gcc) */
    tcc_add_symbol(t, "__floatundisf", (const void*)rt_floatundisf);
    tcc_add_symbol(t, "__floatundidf", (const void*)rt_floatundidf);
    tcc_add_symbol(t, "__floatundixf", (const void*)rt_floatundixf);
    tcc_add_symbol(t, "__floatdisf", (const void*)rt_floatdisf);
    tcc_add_symbol(t, "__floatdidf", (const void*)rt_floatdidf);
    tcc_add_symbol(t, "__fixunssfdi", (const void*)rt_fixunssfdi);
    tcc_add_symbol(t, "__fixunsdfdi", (const void*)rt_fixunsdfdi);
    tcc_add_symbol(t, "__fixunsxfdi", (const void*)rt_fixunsxfdi);
    tcc_add_symbol(t, "__fixsfdi", (const void*)rt_fixsfdi);
    tcc_add_symbol(t, "__fixdfdi", (const void*)rt_fixdfdi);
    tcc_add_symbol(t, "__fixxfdi", (const void*)rt_fixxfdi);
    tcc_add_symbol(t, "__divdi3", (const void*)rt_divdi3);
    tcc_add_symbol(t, "__udivdi3", (const void*)rt_udivdi3);
    tcc_add_symbol(t, "__moddi3", (const void*)rt_moddi3);
    tcc_add_symbol(t, "__umoddi3", (const void*)rt_umoddi3);
    tcc_add_symbol(t, "__ashldi3", (const void*)rt_ashldi3);
    tcc_add_symbol(t, "__ashrdi3", (const void*)rt_ashrdi3);
    tcc_add_symbol(t, "__lshrdi3", (const void*)rt_lshrdi3);
}

cl_int clBuildProgram(cl_program p, cl_uint nd, const cl_device_id* d, const char* options,
                      void (CL_CALLBACK* notify)(cl_program, void*), void* ud) {
    if (!p) return CL_INVALID_PROGRAM;
    if ((nd && !d) || (!nd && d)) return CL_INVALID_VALUE;
    for (cl_uint i = 0; i < nd; i++) if (d[i] != &g_device) return CL_INVALID_DEVICE;
    LOCK();
    if (p->tcc) { tcc_delete(p->tcc); p->tcc = NULL; }
    free(p->log);
    p->log = NULL;
    p->loglen = 0;
    free(p->k);
    p->k = NULL;
    p->nk = 0;
    snprintf(p->options, sizeof(p->options), "%s", options ? options : "");
    SB code = { 0, 0, 0, 0 }, tramp = { 0, 0, 0, 0 }, local_code = { 0, 0, 0, 0 };
    char* original = p->src;
    int lowered = lower_local_arrays(p, &local_code);
    if (!lowered) p->src = local_code.buf;
    sb_str(&code, PRELUDE);
    sb_str(&code, VECTOR_PRELUDE);
    cl_int rc = CL_BUILD_PROGRAM_FAILURE;
    if (!lowered && scan_kernels(p, &code, &tramp) == 0 && !code.oom && !tramp.oom) {
        sb_str(&code, "\n#line 1 \"(kernel entry points)\"\n");
        if (tramp.buf) sb_str(&code, tramp.buf);
        TCCState* t = tcc_new();
        if (t) {
            tcc_set_error_func(t, p, tcc_err);
            tcc_set_options(t, "-nostdlib");
            tcc_set_output_type(t, TCC_OUTPUT_MEMORY);
            /* -D options */
            const char* o = p->options;
            while (o && *o) {
                while (*o == ' ') o++;
                if (o[0] == '-' && o[1] == 'D') {
                    o += 2;
                    while (*o == ' ') o++;
                    char def[128];
                    int n = 0;
                    while (*o && *o != ' ' && n < 127) def[n++] = *o++;
                    def[n] = 0;
                    char* eq = strchr(def, '=');
                    if (eq) { *eq = 0; tcc_define_symbol(t, def, eq + 1); }
                    else if (n) tcc_define_symbol(t, def, "1");
                } else while (*o && *o != ' ') o++;
            }
            add_symbols(t);
            if (tcc_compile_string(t, code.buf) == 0 && tcc_relocate(t) >= 0) {
                int ok = 1;
                for (int i = 0; i < p->nk; i++) {
                    char sym[128];
                    snprintf(sym, sizeof(sym), "__bcl_call_%s", p->k[i].name);
                    p->k[i].call = (void (*)(const void*, void**))tcc_get_symbol(t, sym);
                    if (!p->k[i].call) { ok = 0; log_add(p, "a kernel's entry point is missing"); }
                }
                if (ok) { p->tcc = t; t = NULL; rc = CL_SUCCESS; }
            }
            if (t) tcc_delete(t);
        } else log_add(p, "out of memory for the compiler");
    }
    if (rc != CL_SUCCESS && !p->log) log_add(p, "the program could not be built");
    p->src = original;
    free(local_code.buf);
    free(code.buf);
    free(tramp.buf);
    p->status = rc == CL_SUCCESS ? CL_BUILD_SUCCESS : CL_BUILD_ERROR;
    UNLOCK();
    if (notify) notify(p, ud);
    return rc;
}

cl_int clRetainProgram(cl_program p) { if (!p) return CL_INVALID_PROGRAM; p->ref++; return CL_SUCCESS; }
cl_int clReleaseProgram(cl_program p) {
    if (!p) return CL_INVALID_PROGRAM;
    if (--p->ref == 0) {
        if (p->tcc) tcc_delete(p->tcc);
        free(p->src);
        free(p->log);
        free(p->k);
        clReleaseContext(p->ctx);
        free(p);
    }
    return CL_SUCCESS;
}
cl_int clGetProgramInfo(cl_program p, cl_program_info param, size_t size, void* value, size_t* size_ret) {
    if (!p) return CL_INVALID_PROGRAM;
    switch (param) {
    case CL_PROGRAM_REFERENCE_COUNT: INFO_T(cl_uint, p->ref);
    case CL_PROGRAM_CONTEXT: INFO_T(cl_context, p->ctx);
    case CL_PROGRAM_NUM_DEVICES: INFO_T(cl_uint, 1);
    case CL_PROGRAM_DEVICES: INFO_T(cl_device_id, &g_device);
    case CL_PROGRAM_SOURCE: return info_str(p->src, size, value, size_ret);
    case CL_PROGRAM_NUM_KERNELS:
        if (p->status != CL_BUILD_SUCCESS) return CL_INVALID_PROGRAM_EXECUTABLE;
        INFO_T(size_t, p->nk);
    case CL_PROGRAM_KERNEL_NAMES: {
        if (p->status != CL_BUILD_SUCCESS) return CL_INVALID_PROGRAM_EXECUTABLE;
        SB b = { 0, 0, 0, 0 };
        for (int i = 0; i < p->nk; i++) { if (i) sb_str(&b, ";"); sb_str(&b, p->k[i].name); }
        cl_int r = info_str(b.buf ? b.buf : "", size, value, size_ret);
        free(b.buf);
        return r;
    }
    case CL_PROGRAM_BINARY_SIZES: INFO_T(size_t, 0);
    }
    return CL_INVALID_VALUE;
}
cl_int clGetProgramBuildInfo(cl_program p, cl_device_id d, cl_program_build_info param, size_t size, void* value, size_t* size_ret) {
    if (!p) return CL_INVALID_PROGRAM;
    if (d != &g_device) return CL_INVALID_DEVICE;
    switch (param) {
    case CL_PROGRAM_BUILD_STATUS: INFO_T(cl_build_status, p->status);
    case CL_PROGRAM_BUILD_OPTIONS: return info_str(p->options, size, value, size_ret);
    case CL_PROGRAM_BUILD_LOG: return info_str(p->log ? p->log : "", size, value, size_ret);
    }
    return CL_INVALID_VALUE;
}
cl_int clUnloadCompiler(void) { return CL_SUCCESS; }
cl_int clUnloadPlatformCompiler(cl_platform_id pl) { (void)pl; return CL_SUCCESS; }

/* ══ kernels ═════════════════════════════════════════════════════════ */

cl_kernel clCreateKernel(cl_program p, const char* name, cl_int* err) {
    if (!p) { seterr(err, CL_INVALID_PROGRAM); return NULL; }
    if (p->status != CL_BUILD_SUCCESS) { seterr(err, CL_INVALID_PROGRAM_EXECUTABLE); return NULL; }
    if (!name) { seterr(err, CL_INVALID_VALUE); return NULL; }
    for (int i = 0; i < p->nk; i++) {
        if (strcmp(p->k[i].name, name) != 0) continue;
        cl_kernel k = (cl_kernel)calloc(1, sizeof(*k));
        if (!k) { seterr(err, CL_OUT_OF_HOST_MEMORY); return NULL; }
        k->ref = 1;
        k->prog = p;
        k->info = &p->k[i];
        p->ref++;
        seterr(err, CL_SUCCESS);
        return k;
    }
    seterr(err, CL_INVALID_KERNEL_NAME);
    return NULL;
}
cl_int clCreateKernelsInProgram(cl_program p, cl_uint n, cl_kernel* ks, cl_uint* nr) {
    if (!p) return CL_INVALID_PROGRAM;
    if (p->status != CL_BUILD_SUCCESS) return CL_INVALID_PROGRAM_EXECUTABLE;
    if (ks && n < (cl_uint)p->nk) return CL_INVALID_VALUE;
    if (nr) *nr = (cl_uint)p->nk;
    if (ks) for (int i = 0; i < p->nk; i++) ks[i] = clCreateKernel(p, p->k[i].name, NULL);
    return CL_SUCCESS;
}
cl_int clRetainKernel(cl_kernel k) { if (!k) return CL_INVALID_KERNEL; k->ref++; return CL_SUCCESS; }
cl_int clReleaseKernel(cl_kernel k) {
    if (!k) return CL_INVALID_KERNEL;
    if (--k->ref == 0) {
        for (int i = 0; i < MAXARGS; i++) if (k->mem[i]) clReleaseMemObject(k->mem[i]);
        clReleaseProgram(k->prog);
        free(k);
    }
    return CL_SUCCESS;
}
cl_int clSetKernelArg(cl_kernel k, cl_uint i, size_t size, const void* value) {
    if (!k) return CL_INVALID_KERNEL;
    if (i >= (cl_uint)k->info->nargs) return CL_INVALID_ARG_INDEX;
    if (k->info->is_local[i]) {
        if (value) return CL_INVALID_ARG_VALUE;
        if (!size) return CL_INVALID_ARG_SIZE;
        k->local_size[i] = size;
    } else if (k->info->is_mem[i]) {
        if (size != sizeof(cl_mem)) return CL_INVALID_ARG_SIZE;
        cl_mem m = value ? *(const cl_mem*)value : NULL;
        if (m) m->ref++;
        if (k->mem[i]) clReleaseMemObject(k->mem[i]);
        k->mem[i] = m;
    } else {
        if (!value) return CL_INVALID_ARG_VALUE;
        if (size == 0 || size > sizeof(k->val[i].b)) return CL_INVALID_ARG_SIZE;
        memset(k->val[i].b, 0, sizeof(k->val[i].b));
        memcpy(k->val[i].b, value, size);
    }
    k->set[i] = 1;
    return CL_SUCCESS;
}
cl_int clGetKernelInfo(cl_kernel k, cl_kernel_info param, size_t size, void* value, size_t* size_ret) {
    if (!k) return CL_INVALID_KERNEL;
    switch (param) {
    case CL_KERNEL_FUNCTION_NAME: return info_str(k->info->name, size, value, size_ret);
    case CL_KERNEL_NUM_ARGS: INFO_T(cl_uint, k->info->nargs);
    case CL_KERNEL_REFERENCE_COUNT: INFO_T(cl_uint, k->ref);
    case CL_KERNEL_CONTEXT: INFO_T(cl_context, k->prog->ctx);
    case CL_KERNEL_PROGRAM: INFO_T(cl_program, k->prog);
    case CL_KERNEL_ATTRIBUTES: return info_str("", size, value, size_ret);
    }
    return CL_INVALID_VALUE;
}
cl_int clGetKernelWorkGroupInfo(cl_kernel k, cl_device_id d, cl_kernel_work_group_info param, size_t size, void* value, size_t* size_ret) {
    if (!k) return CL_INVALID_KERNEL;
    if (d && d != &g_device) return CL_INVALID_DEVICE;
    switch (param) {
    case CL_KERNEL_WORK_GROUP_SIZE: INFO_T(size_t, 1024);
    case CL_KERNEL_COMPILE_WORK_GROUP_SIZE: { size_t z[3] = { 0, 0, 0 }; return info(z, sizeof(z), size, value, size_ret); }
    case CL_KERNEL_LOCAL_MEM_SIZE: case CL_KERNEL_PRIVATE_MEM_SIZE: INFO_T(cl_ulong, 0);
    case CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE: INFO_T(size_t, 1);
    }
    return CL_INVALID_VALUE;
}

cl_int clEnqueueNDRangeKernel(cl_command_queue q, cl_kernel k, cl_uint dim, const size_t* goff, const size_t* gsz,
                              const size_t* lsz, cl_uint ne, const cl_event* wl, cl_event* ev) {
    (void)ne; (void)wl;
    if (!q) return CL_INVALID_COMMAND_QUEUE;
    if (!k) return CL_INVALID_KERNEL;
    if (dim < 1 || dim > 3) return CL_INVALID_WORK_DIMENSION;
    if (!gsz) return CL_INVALID_GLOBAL_WORK_SIZE;
    for (int i = 0; i < k->info->nargs; i++) if (!k->set[i]) return CL_INVALID_KERNEL_ARGS;
    JOB j;
    memset(&j, 0, sizeof(j));
    j.k = k;
    j.dim = dim;
    size_t group_items = 1;
    for (unsigned d = 0; d < 3; d++) {
        j.gsz[d] = d < dim ? gsz[d] : 1;
        j.off[d] = d < dim && goff ? goff[d] : 0;
        if (j.gsz[d] == 0) return CL_INVALID_GLOBAL_WORK_SIZE;
        if (d < dim && lsz) {
            if (!lsz[d] || j.gsz[d] % lsz[d]) return CL_INVALID_WORK_GROUP_SIZE;
            j.lsz[d] = lsz[d];
        } else if (d == 0) {
            /* a group size dividing the range: up to 64 items */
            size_t g = 64;
            while (g > 1 && j.gsz[0] % g) g--;
            j.lsz[0] = g;
        } else j.lsz[d] = 1;
        if (j.lsz[d] > 1024 / group_items) return CL_INVALID_WORK_GROUP_SIZE;
        if (j.off[d] > (size_t)-1 - j.gsz[d]) return CL_INVALID_GLOBAL_OFFSET;
        group_items *= j.lsz[d];
        j.ngr[d] = j.gsz[d] / j.lsz[d];
    }
    if (group_items > 1024) return CL_INVALID_WORK_GROUP_SIZE;
    size_t bytes = 0;
    for (int i = 0; i < k->info->nargs; i++) if (k->info->is_local[i]) {
        if (k->local_size[i] > BCL_LOCAL_LIMIT - bytes) return CL_OUT_OF_RESOURCES;
        bytes += k->local_size[i];
    }
    j.total = 1;
    for (unsigned d = 0; d < 3; d++) {
        if (j.ngr[d] > (((size_t)-1) >> 1) / j.total) return CL_INVALID_GLOBAL_WORK_SIZE;
        j.total *= j.ngr[d];
    }
    cl_ulong t0 = now_ns();
    run_job(&j);
    if (j.error) return j.error;
    make_event(q, CL_COMMAND_NDRANGE_KERNEL, t0, ev);
    return CL_SUCCESS;
}
cl_int clEnqueueTask(cl_command_queue q, cl_kernel k, cl_uint ne, const cl_event* wl, cl_event* ev) {
    size_t one = 1;
    return clEnqueueNDRangeKernel(q, k, 1, NULL, &one, &one, ne, wl, ev);
}
