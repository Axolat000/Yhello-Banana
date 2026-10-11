/* Tests for sdk/lib/cl.c: the usual first OpenCL programs. On Linux:
 *   gcc -O2 -DBCL_HOST -I<CL headers> -I<libtcc.h dir> cltest.c cl.c libtcc.a -lm -lpthread -ldl
 * On Banana OS it is the "cltest" example app (prints to its terminal). */
#include <CL/cl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static const char* SRC =
    "// vector addition\n"
    "__kernel void vadd(__global const float* a, __global const float* b, __global float* c, const int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) c[i] = a[i] + b[i];\n"
    "}\n"
    "/* the Mandelbrot set, one pixel per work-item, 2D */\n"
    "kernel void mandel(global uchar* out, int w, int h, float scale) {\n"
    "    int x = get_global_id(0), y = get_global_id(1);\n"
    "    float cr = (x - w * 0.6f) * scale, ci = (y - h * 0.5f) * scale, zr = 0, zi = 0;\n"
    "    int it = 0;\n"
    "    while (it < ITER && zr * zr + zi * zi < 4.0f) { float t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; it++; }\n"
    "    out[y * w + x] = (uchar)(it * 255 / ITER);\n"
    "}\n"
    "__kernel void histo(__global const int* v, __global int* bins, int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) atomic_inc(&bins[v[i] & 15]);\n"
    "}\n"
    "__kernel void math(__global float* o) {\n"
    "    size_t i = get_global_id(0);\n"
    "    float x = (float)i / 10.0f;\n"
    "    o[i] = clamp(sqrt(x) + sin(x) * mad(x, 2.0f, 1.0f), -100.0f, 100.0f) + max(get_global_size(0), (size_t)0) * 0;\n"
    "}\n"
    "__kernel void scratch(__global int* o, __local int* tmp) {\n"
    "    int i = get_global_id(0);\n"
    "    tmp[get_local_id(0)] = i * 3;\n"
    "    o[i] = tmp[get_local_id(0)] + (int)get_group_id(0) * 0 + (int)get_local_size(0) * 0;\n"
    "}\n";

int main(void) {
    cl_platform_id pl;
    cl_device_id dev;
    cl_uint n;
    char buf[256];
    cl_int err = clGetPlatformIDs(1, &pl, &n);
    CHECK(err == CL_SUCCESS && n == 1, "a platform");
    clGetPlatformInfo(pl, CL_PLATFORM_NAME, sizeof(buf), buf, NULL);
    printf("     platform: %s\n", buf);
    err = clGetDeviceIDs(pl, CL_DEVICE_TYPE_GPU, 1, &dev, &n);
    CHECK(err == CL_DEVICE_NOT_FOUND, "no GPU device (as expected)");
    err = clGetDeviceIDs(pl, CL_DEVICE_TYPE_DEFAULT, 1, &dev, &n);
    CHECK(err == CL_SUCCESS, "the CPU device");
    cl_uint cu;
    clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(buf), buf, NULL);
    clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, NULL);
    printf("     device: %s, %u compute units\n", buf, cu);

    cl_context ctx = clCreateContext(NULL, 1, &dev, NULL, NULL, &err);
    CHECK(ctx && err == CL_SUCCESS, "context");
    cl_command_queue q = clCreateCommandQueue(ctx, dev, CL_QUEUE_PROFILING_ENABLE, &err);
    CHECK(q && err == CL_SUCCESS, "command queue");

    /* a build error comes back in the log */
    const char* bad = "__kernel void broken(__global int* a) { a[0] = undefined_thing; }";
    cl_program pb = clCreateProgramWithSource(ctx, 1, &bad, NULL, &err);
    err = clBuildProgram(pb, 1, &dev, NULL, NULL, NULL);
    char log[1024] = "";
    clGetProgramBuildInfo(pb, dev, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
    CHECK(err == CL_BUILD_PROGRAM_FAILURE && strstr(log, "undefined_thing"), "build errors in the log");
    printf("     log: %s", log);
    clReleaseProgram(pb);

    cl_program prog = clCreateProgramWithSource(ctx, 1, &SRC, NULL, &err);
    err = clBuildProgram(prog, 1, &dev, "-DITER=64", NULL, NULL);
    if (err != CL_SUCCESS) {
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, sizeof(log), log, NULL);
        printf("build log:\n%s\n", log);
    }
    CHECK(err == CL_SUCCESS, "build (with -DITER=64)");
    if (err != CL_SUCCESS) return 1;
    clGetProgramInfo(prog, CL_PROGRAM_KERNEL_NAMES, sizeof(buf), buf, NULL);
    printf("     kernels: %s\n", buf);

    /* vector add */
    enum { N = 100000 };
    float* a = malloc(N * 4), * b = malloc(N * 4), * c = malloc(N * 4);
    for (int i = 0; i < N; i++) { a[i] = (float)i; b[i] = (float)(2 * i); }
    cl_mem ma = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * 4, a, &err);
    cl_mem mb = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * 4, b, &err);
    cl_mem mc = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, N * 4, NULL, &err);
    cl_kernel kv = clCreateKernel(prog, "vadd", &err);
    CHECK(kv && err == CL_SUCCESS, "kernel vadd");
    int nn = N;
    clSetKernelArg(kv, 0, sizeof(cl_mem), &ma);
    clSetKernelArg(kv, 1, sizeof(cl_mem), &mb);
    clSetKernelArg(kv, 2, sizeof(cl_mem), &mc);
    clSetKernelArg(kv, 3, sizeof(int), &nn);
    size_t gs = N;
    cl_event ev;
    err = clEnqueueNDRangeKernel(q, kv, 1, NULL, &gs, NULL, 0, NULL, &ev);
    clEnqueueReadBuffer(q, mc, CL_TRUE, 0, N * 4, c, 0, NULL, NULL);
    int good = err == CL_SUCCESS;
    for (int i = 0; i < N; i++) if (c[i] != 3.0f * i) { good = 0; break; }
    CHECK(good, "vadd: 100000 sums right");
    cl_ulong t0, t1;
    clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_START, sizeof(t0), &t0, NULL);
    clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_END, sizeof(t1), &t1, NULL);
    printf("     vadd took %.3f ms\n", (t1 - t0) / 1e6);
    clReleaseEvent(ev);

    /* mandelbrot, 2D */
    int w = 256, h = 192;
    float scale = 3.0f / w;
    cl_mem mo = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, (size_t)w * h, NULL, &err);
    cl_kernel km = clCreateKernel(prog, "mandel", &err);
    clSetKernelArg(km, 0, sizeof(cl_mem), &mo);
    clSetKernelArg(km, 1, sizeof(int), &w);
    clSetKernelArg(km, 2, sizeof(int), &h);
    clSetKernelArg(km, 3, sizeof(float), &scale);
    size_t g2[2] = { (size_t)w, (size_t)h }, l2[2] = { 16, 8 };
    err = clEnqueueNDRangeKernel(q, km, 2, NULL, g2, l2, 0, NULL, NULL);
    unsigned char* img = malloc((size_t)w * h);
    clEnqueueReadBuffer(q, mo, CL_TRUE, 0, (size_t)w * h, img, 0, NULL, NULL);
    /* the same on the host, to compare */
    /* (on i686 the x87 FPU keeps more precision in some places than others:
     * a few pixels on the set's edge may land one iteration away) */
    int diff = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float cr = (x - w * 0.6f) * scale, ci = (y - h * 0.5f) * scale, zr = 0, zi = 0;
        int it = 0;
        while (it < 64 && zr * zr + zi * zi < 4.0f) { float t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; it++; }
        if (img[y * w + x] != (unsigned char)(it * 255 / 64)) diff++;
    }
    printf("     mandel: %d of %d pixels differ from the host\n", diff, w * h);
    CHECK(err == CL_SUCCESS && diff * 100 < w * h, "mandel: 2D range, 16x8 groups, matches the host");
    FILE* f = fopen("mandel.pgm", "wb");
    if (f) { fprintf(f, "P5\n%d %d\n255\n", w, h); fwrite(img, 1, (size_t)w * h, f); fclose(f); }

    /* atomics */
    int* vals = malloc(N * 4);
    for (int i = 0; i < N; i++) vals[i] = i * 7;
    int bins[16] = { 0 }, expect[16] = { 0 };
    for (int i = 0; i < N; i++) expect[vals[i] & 15]++;
    cl_mem mv = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, N * 4, vals, &err);
    cl_mem mbins = clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(bins), bins, &err);
    cl_kernel kh = clCreateKernel(prog, "histo", &err);
    clSetKernelArg(kh, 0, sizeof(cl_mem), &mv);
    clSetKernelArg(kh, 1, sizeof(cl_mem), &mbins);
    clSetKernelArg(kh, 2, sizeof(int), &nn);
    err = clEnqueueNDRangeKernel(q, kh, 1, NULL, &gs, NULL, 0, NULL, NULL);
    clEnqueueReadBuffer(q, mbins, CL_TRUE, 0, sizeof(bins), bins, 0, NULL, NULL);
    CHECK(err == CL_SUCCESS && !memcmp(bins, expect, sizeof(bins)), "histo: atomic_inc from every core");

    /* built-in math */
    cl_mem mm = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, 64 * 4, NULL, &err);
    cl_kernel kmath = clCreateKernel(prog, "math", &err);
    clSetKernelArg(kmath, 0, sizeof(cl_mem), &mm);
    size_t g64 = 64;
    clEnqueueNDRangeKernel(q, kmath, 1, NULL, &g64, NULL, 0, NULL, NULL);
    float mo2[64];
    clEnqueueReadBuffer(q, mm, CL_TRUE, 0, sizeof(mo2), mo2, 0, NULL, NULL);
    int mok = 1;
    for (int i = 0; i < 64; i++) {
        float x = i / 10.0f, e = sqrtf(x) + sinf(x) * (x * 2 + 1);
        e = e < -100 ? -100 : e > 100 ? 100 : e;
        if (fabsf(mo2[i] - e) > 1e-3f) { mok = 0; printf("     math[%d] = %f, want %f\n", i, mo2[i], e); break; }
    }
    CHECK(mok, "math: sqrt, sin, mad, clamp, max");

    /* __local scratch */
    cl_mem ms = clCreateBuffer(ctx, CL_MEM_WRITE_ONLY, 256 * 4, NULL, &err);
    cl_kernel ks = clCreateKernel(prog, "scratch", &err);
    clSetKernelArg(ks, 0, sizeof(cl_mem), &ms);
    err = clSetKernelArg(ks, 1, 32 * sizeof(int), NULL);
    size_t g256 = 256, l32 = 32;
    cl_int e2 = clEnqueueNDRangeKernel(q, ks, 1, NULL, &g256, &l32, 0, NULL, NULL);
    int so[256];
    clEnqueueReadBuffer(q, ms, CL_TRUE, 0, sizeof(so), so, 0, NULL, NULL);
    int sok = err == CL_SUCCESS && e2 == CL_SUCCESS;
    for (int i = 0; i < 256; i++) if (so[i] != i * 3) sok = 0;
    CHECK(sok, "scratch: a __local argument");

    size_t bad_l = 7;
    CHECK(clEnqueueNDRangeKernel(q, ks, 1, NULL, &g256, &bad_l, 0, NULL, NULL) == CL_INVALID_WORK_GROUP_SIZE, "a group size that does not divide: refused");

    clReleaseKernel(kv); clReleaseKernel(km); clReleaseKernel(kh); clReleaseKernel(kmath); clReleaseKernel(ks);
    clReleaseMemObject(ma); clReleaseMemObject(mb); clReleaseMemObject(mc); clReleaseMemObject(mo);
    clReleaseMemObject(mv); clReleaseMemObject(mbins); clReleaseMemObject(mm); clReleaseMemObject(ms);
    clReleaseProgram(prog);
    clReleaseCommandQueue(q);
    clReleaseContext(ctx);
    printf("%s (%d failed)\n", fails ? "SOME TESTS FAILED" : "all tests passed", fails);
    return fails != 0;
}
