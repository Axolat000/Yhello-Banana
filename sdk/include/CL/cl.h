#ifndef __OPENCL_CL_H
#define __OPENCL_CL_H

/*
 * OpenCL for Banana OS (USE_CL = 1 in your app's Makefile): the OpenCL 1.2
 * host API. One device - the computer's processor, every core: kernels
 * (OpenCL C) are compiled when the program is built, by the TinyCC inside
 * the app, and their work-groups shared out between the cores.
 * The constants have their standard values. What OpenCL C has here: the
 * address space qualifiers, the work-item functions, scalar types, the
 * math functions, min / max / clamp / mad / mix / step / sign,
 * atomic_* (32-bit), printf. Not (yet): vector types (float4...), images,
 * barrier() and __local shared between work-items.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_API_ENTRY
#define CL_API_CALL
#define CL_CALLBACK
#define CL_API_SUFFIX__VERSION_1_2

typedef int8_t   cl_char;
typedef uint8_t  cl_uchar;
typedef int16_t  cl_short;
typedef uint16_t cl_ushort;
typedef int32_t  cl_int;
typedef uint32_t cl_uint;
typedef int64_t  cl_long;
typedef uint64_t cl_ulong;
typedef uint16_t cl_half;
typedef float    cl_float;
typedef double   cl_double;

typedef struct _cl_platform_id*   cl_platform_id;
typedef struct _cl_device_id*     cl_device_id;
typedef struct _cl_context*       cl_context;
typedef struct _cl_command_queue* cl_command_queue;
typedef struct _cl_mem*           cl_mem;
typedef struct _cl_program*       cl_program;
typedef struct _cl_kernel*        cl_kernel;
typedef struct _cl_event*         cl_event;
typedef struct _cl_sampler*       cl_sampler;

typedef cl_uint  cl_bool;
typedef cl_ulong cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_uint  cl_platform_info;
typedef cl_uint  cl_device_info;
typedef cl_bitfield cl_device_fp_config;
typedef cl_uint  cl_device_mem_cache_type;
typedef cl_uint  cl_device_local_mem_type;
typedef cl_bitfield cl_device_exec_capabilities;
typedef cl_bitfield cl_command_queue_properties;
typedef intptr_t cl_context_properties;
typedef cl_uint  cl_context_info;
typedef cl_uint  cl_command_queue_info;
typedef cl_bitfield cl_mem_flags;
typedef cl_uint  cl_mem_object_type;
typedef cl_uint  cl_mem_info;
typedef cl_bitfield cl_map_flags;
typedef cl_uint  cl_program_info;
typedef cl_uint  cl_program_build_info;
typedef cl_int   cl_build_status;
typedef cl_uint  cl_kernel_info;
typedef cl_uint  cl_kernel_work_group_info;
typedef cl_uint  cl_event_info;
typedef cl_uint  cl_command_type;
typedef cl_uint  cl_profiling_info;
typedef cl_bitfield cl_queue_properties_khr;
typedef cl_bitfield cl_queue_properties;

/* errors */
#define CL_SUCCESS                         0
#define CL_DEVICE_NOT_FOUND               -1
#define CL_DEVICE_NOT_AVAILABLE           -2
#define CL_COMPILER_NOT_AVAILABLE         -3
#define CL_MEM_OBJECT_ALLOCATION_FAILURE  -4
#define CL_OUT_OF_RESOURCES               -5
#define CL_OUT_OF_HOST_MEMORY             -6
#define CL_PROFILING_INFO_NOT_AVAILABLE   -7
#define CL_MEM_COPY_OVERLAP               -8
#define CL_IMAGE_FORMAT_MISMATCH          -9
#define CL_IMAGE_FORMAT_NOT_SUPPORTED     -10
#define CL_BUILD_PROGRAM_FAILURE          -11
#define CL_MAP_FAILURE                    -12
#define CL_INVALID_VALUE                  -30
#define CL_INVALID_DEVICE_TYPE            -31
#define CL_INVALID_PLATFORM               -32
#define CL_INVALID_DEVICE                 -33
#define CL_INVALID_CONTEXT                -34
#define CL_INVALID_QUEUE_PROPERTIES       -35
#define CL_INVALID_COMMAND_QUEUE          -36
#define CL_INVALID_HOST_PTR               -37
#define CL_INVALID_MEM_OBJECT             -38
#define CL_INVALID_BINARY                 -42
#define CL_INVALID_BUILD_OPTIONS          -43
#define CL_INVALID_PROGRAM                -44
#define CL_INVALID_PROGRAM_EXECUTABLE     -45
#define CL_INVALID_KERNEL_NAME            -46
#define CL_INVALID_KERNEL_DEFINITION      -47
#define CL_INVALID_KERNEL                 -48
#define CL_INVALID_ARG_INDEX              -49
#define CL_INVALID_ARG_VALUE              -50
#define CL_INVALID_ARG_SIZE               -51
#define CL_INVALID_KERNEL_ARGS            -52
#define CL_INVALID_WORK_DIMENSION         -53
#define CL_INVALID_WORK_GROUP_SIZE        -54
#define CL_INVALID_WORK_ITEM_SIZE         -55
#define CL_INVALID_GLOBAL_OFFSET          -56
#define CL_INVALID_EVENT_WAIT_LIST        -57
#define CL_INVALID_EVENT                  -58
#define CL_INVALID_OPERATION              -59
#define CL_INVALID_BUFFER_SIZE            -61
#define CL_INVALID_GLOBAL_WORK_SIZE       -63

#define CL_FALSE    0
#define CL_TRUE     1
#define CL_BLOCKING CL_TRUE
#define CL_NON_BLOCKING CL_FALSE

/* platform */
#define CL_PLATFORM_PROFILE    0x0900
#define CL_PLATFORM_VERSION    0x0901
#define CL_PLATFORM_NAME       0x0902
#define CL_PLATFORM_VENDOR     0x0903
#define CL_PLATFORM_EXTENSIONS 0x0904

/* device types */
#define CL_DEVICE_TYPE_DEFAULT     (1 << 0)
#define CL_DEVICE_TYPE_CPU         (1 << 1)
#define CL_DEVICE_TYPE_GPU         (1 << 2)
#define CL_DEVICE_TYPE_ACCELERATOR (1 << 3)
#define CL_DEVICE_TYPE_ALL         0xFFFFFFFF

/* device info */
#define CL_DEVICE_TYPE                          0x1000
#define CL_DEVICE_VENDOR_ID                     0x1001
#define CL_DEVICE_MAX_COMPUTE_UNITS             0x1002
#define CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS      0x1003
#define CL_DEVICE_MAX_WORK_GROUP_SIZE           0x1004
#define CL_DEVICE_MAX_WORK_ITEM_SIZES           0x1005
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_CHAR   0x1006
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_SHORT  0x1007
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_INT    0x1008
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_LONG   0x1009
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_FLOAT  0x100A
#define CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE 0x100B
#define CL_DEVICE_MAX_CLOCK_FREQUENCY           0x100C
#define CL_DEVICE_ADDRESS_BITS                  0x100D
#define CL_DEVICE_MAX_READ_IMAGE_ARGS           0x100E
#define CL_DEVICE_MAX_WRITE_IMAGE_ARGS          0x100F
#define CL_DEVICE_MAX_MEM_ALLOC_SIZE            0x1010
#define CL_DEVICE_IMAGE_SUPPORT                 0x1016
#define CL_DEVICE_MAX_PARAMETER_SIZE            0x1017
#define CL_DEVICE_MAX_SAMPLERS                  0x1018
#define CL_DEVICE_MEM_BASE_ADDR_ALIGN           0x1019
#define CL_DEVICE_MIN_DATA_TYPE_ALIGN_SIZE      0x101A
#define CL_DEVICE_SINGLE_FP_CONFIG              0x101B
#define CL_DEVICE_GLOBAL_MEM_CACHE_TYPE         0x101C
#define CL_DEVICE_GLOBAL_MEM_CACHELINE_SIZE     0x101D
#define CL_DEVICE_GLOBAL_MEM_CACHE_SIZE         0x101E
#define CL_DEVICE_GLOBAL_MEM_SIZE               0x101F
#define CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE      0x1020
#define CL_DEVICE_MAX_CONSTANT_ARGS             0x1021
#define CL_DEVICE_LOCAL_MEM_TYPE                0x1022
#define CL_DEVICE_LOCAL_MEM_SIZE                0x1023
#define CL_DEVICE_ERROR_CORRECTION_SUPPORT      0x1024
#define CL_DEVICE_PROFILING_TIMER_RESOLUTION    0x1025
#define CL_DEVICE_ENDIAN_LITTLE                 0x1026
#define CL_DEVICE_AVAILABLE                     0x1027
#define CL_DEVICE_COMPILER_AVAILABLE            0x1028
#define CL_DEVICE_EXECUTION_CAPABILITIES        0x1029
#define CL_DEVICE_QUEUE_PROPERTIES              0x102A
#define CL_DEVICE_NAME                          0x102B
#define CL_DEVICE_VENDOR                        0x102C
#define CL_DRIVER_VERSION                       0x102D
#define CL_DEVICE_PROFILE                       0x102E
#define CL_DEVICE_VERSION                       0x102F
#define CL_DEVICE_EXTENSIONS                    0x1030
#define CL_DEVICE_PLATFORM                      0x1031
#define CL_DEVICE_DOUBLE_FP_CONFIG              0x1032
#define CL_DEVICE_HOST_UNIFIED_MEMORY           0x1035
#define CL_DEVICE_OPENCL_C_VERSION              0x103D
#define CL_DEVICE_LINKER_AVAILABLE              0x103E

#define CL_FP_DENORM           (1 << 0)
#define CL_FP_INF_NAN          (1 << 1)
#define CL_FP_ROUND_TO_NEAREST (1 << 2)
#define CL_FP_FMA              (1 << 5)
#define CL_READ_WRITE_CACHE    0x2
#define CL_GLOBAL              0x2
#define CL_EXEC_KERNEL         (1 << 0)

/* context */
#define CL_CONTEXT_REFERENCE_COUNT 0x1080
#define CL_CONTEXT_DEVICES         0x1081
#define CL_CONTEXT_PROPERTIES      0x1082
#define CL_CONTEXT_NUM_DEVICES     0x1083
#define CL_CONTEXT_PLATFORM        0x1084

/* command queues */
#define CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE (1 << 0)
#define CL_QUEUE_PROFILING_ENABLE              (1 << 1)
#define CL_QUEUE_CONTEXT         0x1090
#define CL_QUEUE_DEVICE          0x1091
#define CL_QUEUE_REFERENCE_COUNT 0x1092
#define CL_QUEUE_PROPERTIES      0x1093

/* memory */
#define CL_MEM_READ_WRITE     (1 << 0)
#define CL_MEM_WRITE_ONLY     (1 << 1)
#define CL_MEM_READ_ONLY      (1 << 2)
#define CL_MEM_USE_HOST_PTR   (1 << 3)
#define CL_MEM_ALLOC_HOST_PTR (1 << 4)
#define CL_MEM_COPY_HOST_PTR  (1 << 5)
#define CL_MEM_HOST_WRITE_ONLY (1 << 7)
#define CL_MEM_HOST_READ_ONLY  (1 << 8)
#define CL_MEM_HOST_NO_ACCESS  (1 << 9)
#define CL_MEM_OBJECT_BUFFER  0x10F0
#define CL_MEM_TYPE            0x1100
#define CL_MEM_FLAGS           0x1101
#define CL_MEM_SIZE            0x1102
#define CL_MEM_HOST_PTR        0x1103
#define CL_MEM_MAP_COUNT       0x1104
#define CL_MEM_REFERENCE_COUNT 0x1105
#define CL_MEM_CONTEXT         0x1106
#define CL_MAP_READ  (1 << 0)
#define CL_MAP_WRITE (1 << 1)

/* programs */
#define CL_PROGRAM_REFERENCE_COUNT 0x1160
#define CL_PROGRAM_CONTEXT         0x1161
#define CL_PROGRAM_NUM_DEVICES     0x1162
#define CL_PROGRAM_DEVICES         0x1163
#define CL_PROGRAM_SOURCE          0x1164
#define CL_PROGRAM_BINARY_SIZES    0x1165
#define CL_PROGRAM_BINARIES        0x1166
#define CL_PROGRAM_NUM_KERNELS     0x1167
#define CL_PROGRAM_KERNEL_NAMES    0x1168
#define CL_PROGRAM_BUILD_STATUS    0x1181
#define CL_PROGRAM_BUILD_OPTIONS   0x1182
#define CL_PROGRAM_BUILD_LOG       0x1183
#define CL_BUILD_SUCCESS      0
#define CL_BUILD_NONE        -1
#define CL_BUILD_ERROR       -2
#define CL_BUILD_IN_PROGRESS -3

/* kernels */
#define CL_KERNEL_FUNCTION_NAME   0x1190
#define CL_KERNEL_NUM_ARGS        0x1191
#define CL_KERNEL_REFERENCE_COUNT 0x1192
#define CL_KERNEL_CONTEXT         0x1193
#define CL_KERNEL_PROGRAM         0x1194
#define CL_KERNEL_ATTRIBUTES      0x1195
#define CL_KERNEL_WORK_GROUP_SIZE                    0x11B0
#define CL_KERNEL_COMPILE_WORK_GROUP_SIZE            0x11B1
#define CL_KERNEL_LOCAL_MEM_SIZE                     0x11B2
#define CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE 0x11B3
#define CL_KERNEL_PRIVATE_MEM_SIZE                   0x11B4

/* events */
#define CL_EVENT_COMMAND_QUEUE            0x11D0
#define CL_EVENT_COMMAND_TYPE             0x11D1
#define CL_EVENT_REFERENCE_COUNT          0x11D2
#define CL_EVENT_COMMAND_EXECUTION_STATUS 0x11D3
#define CL_EVENT_CONTEXT                  0x11D4
#define CL_COMMAND_NDRANGE_KERNEL 0x11F0
#define CL_COMMAND_TASK           0x11F1
#define CL_COMMAND_READ_BUFFER    0x11F3
#define CL_COMMAND_WRITE_BUFFER   0x11F4
#define CL_COMMAND_COPY_BUFFER    0x11F5
#define CL_COMMAND_MAP_BUFFER     0x11FB
#define CL_COMMAND_UNMAP_MEM_OBJECT 0x11FD
#define CL_COMMAND_MARKER         0x11FE
#define CL_COMMAND_BARRIER        0x1205
#define CL_COMMAND_FILL_BUFFER    0x1207
#define CL_COMPLETE  0x0
#define CL_RUNNING   0x1
#define CL_SUBMITTED 0x2
#define CL_QUEUED    0x3
#define CL_PROFILING_COMMAND_QUEUED 0x1280
#define CL_PROFILING_COMMAND_SUBMIT 0x1281
#define CL_PROFILING_COMMAND_START  0x1282
#define CL_PROFILING_COMMAND_END    0x1283

/* platforms and devices */
cl_int clGetPlatformIDs(cl_uint num_entries, cl_platform_id* platforms, cl_uint* num_platforms);
cl_int clGetPlatformInfo(cl_platform_id platform, cl_platform_info param, size_t size, void* value, size_t* size_ret);
cl_int clGetDeviceIDs(cl_platform_id platform, cl_device_type type, cl_uint num_entries, cl_device_id* devices, cl_uint* num_devices);
cl_int clGetDeviceInfo(cl_device_id device, cl_device_info param, size_t size, void* value, size_t* size_ret);
cl_int clRetainDevice(cl_device_id device);
cl_int clReleaseDevice(cl_device_id device);

/* contexts */
cl_context clCreateContext(const cl_context_properties* props, cl_uint num_devices, const cl_device_id* devices,
                           void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* user_data, cl_int* err);
cl_context clCreateContextFromType(const cl_context_properties* props, cl_device_type type,
                                   void (CL_CALLBACK* notify)(const char*, const void*, size_t, void*), void* user_data, cl_int* err);
cl_int clRetainContext(cl_context context);
cl_int clReleaseContext(cl_context context);
cl_int clGetContextInfo(cl_context context, cl_context_info param, size_t size, void* value, size_t* size_ret);

/* command queues */
cl_command_queue clCreateCommandQueue(cl_context context, cl_device_id device, cl_command_queue_properties props, cl_int* err);
cl_command_queue clCreateCommandQueueWithProperties(cl_context context, cl_device_id device, const cl_queue_properties* props, cl_int* err);
cl_int clRetainCommandQueue(cl_command_queue queue);
cl_int clReleaseCommandQueue(cl_command_queue queue);
cl_int clGetCommandQueueInfo(cl_command_queue queue, cl_command_queue_info param, size_t size, void* value, size_t* size_ret);
cl_int clFlush(cl_command_queue queue);
cl_int clFinish(cl_command_queue queue);

/* buffers */
cl_mem clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size, void* host_ptr, cl_int* err);
cl_int clRetainMemObject(cl_mem mem);
cl_int clReleaseMemObject(cl_mem mem);
cl_int clGetMemObjectInfo(cl_mem mem, cl_mem_info param, size_t size, void* value, size_t* size_ret);
cl_int clEnqueueReadBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking, size_t offset, size_t size, void* ptr,
                           cl_uint num_events, const cl_event* wait_list, cl_event* event);
cl_int clEnqueueWriteBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking, size_t offset, size_t size, const void* ptr,
                            cl_uint num_events, const cl_event* wait_list, cl_event* event);
cl_int clEnqueueCopyBuffer(cl_command_queue queue, cl_mem src, cl_mem dst, size_t src_offset, size_t dst_offset, size_t size,
                           cl_uint num_events, const cl_event* wait_list, cl_event* event);
cl_int clEnqueueFillBuffer(cl_command_queue queue, cl_mem buffer, const void* pattern, size_t pattern_size, size_t offset, size_t size,
                           cl_uint num_events, const cl_event* wait_list, cl_event* event);
void* clEnqueueMapBuffer(cl_command_queue queue, cl_mem buffer, cl_bool blocking, cl_map_flags flags, size_t offset, size_t size,
                         cl_uint num_events, const cl_event* wait_list, cl_event* event, cl_int* err);
cl_int clEnqueueUnmapMemObject(cl_command_queue queue, cl_mem mem, void* ptr, cl_uint num_events, const cl_event* wait_list, cl_event* event);

/* programs */
cl_program clCreateProgramWithSource(cl_context context, cl_uint count, const char** strings, const size_t* lengths, cl_int* err);
cl_int clBuildProgram(cl_program program, cl_uint num_devices, const cl_device_id* devices, const char* options,
                      void (CL_CALLBACK* notify)(cl_program, void*), void* user_data);
cl_int clRetainProgram(cl_program program);
cl_int clReleaseProgram(cl_program program);
cl_int clGetProgramInfo(cl_program program, cl_program_info param, size_t size, void* value, size_t* size_ret);
cl_int clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param, size_t size, void* value, size_t* size_ret);
cl_int clUnloadCompiler(void);
cl_int clUnloadPlatformCompiler(cl_platform_id platform);

/* kernels */
cl_kernel clCreateKernel(cl_program program, const char* name, cl_int* err);
cl_int clCreateKernelsInProgram(cl_program program, cl_uint num_kernels, cl_kernel* kernels, cl_uint* num_ret);
cl_int clRetainKernel(cl_kernel kernel);
cl_int clReleaseKernel(cl_kernel kernel);
cl_int clSetKernelArg(cl_kernel kernel, cl_uint index, size_t size, const void* value);
cl_int clGetKernelInfo(cl_kernel kernel, cl_kernel_info param, size_t size, void* value, size_t* size_ret);
cl_int clGetKernelWorkGroupInfo(cl_kernel kernel, cl_device_id device, cl_kernel_work_group_info param, size_t size, void* value, size_t* size_ret);
cl_int clEnqueueNDRangeKernel(cl_command_queue queue, cl_kernel kernel, cl_uint work_dim, const size_t* global_offset,
                              const size_t* global_size, const size_t* local_size,
                              cl_uint num_events, const cl_event* wait_list, cl_event* event);
cl_int clEnqueueTask(cl_command_queue queue, cl_kernel kernel, cl_uint num_events, const cl_event* wait_list, cl_event* event);

/* events */
cl_int clWaitForEvents(cl_uint num_events, const cl_event* events);
cl_int clGetEventInfo(cl_event event, cl_event_info param, size_t size, void* value, size_t* size_ret);
cl_int clGetEventProfilingInfo(cl_event event, cl_profiling_info param, size_t size, void* value, size_t* size_ret);
cl_int clRetainEvent(cl_event event);
cl_int clReleaseEvent(cl_event event);
cl_int clEnqueueMarker(cl_command_queue queue, cl_event* event);
cl_int clEnqueueBarrier(cl_command_queue queue);
cl_int clEnqueueMarkerWithWaitList(cl_command_queue queue, cl_uint num_events, const cl_event* wait_list, cl_event* event);
cl_int clEnqueueBarrierWithWaitList(cl_command_queue queue, cl_uint num_events, const cl_event* wait_list, cl_event* event);

#ifdef __cplusplus
}
#endif

#endif
