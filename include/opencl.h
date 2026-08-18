#ifndef _SQUASH_OPENCL_H
#define _SQUASH_OPENCL_H
/* Minimal OpenCL 1.2 host API -- just enough to pick a platform/device,
 * build a program from source at runtime (no offline compiler needed,
 * unlike Vulkan's SPIR-V), create a kernel, upload/download buffers, and
 * enqueue an NDRange dispatch. Flat, non-COM exports from the real system
 * OpenCL.dll (the Khronos ICD loader, present here via the GPU driver),
 * so this uses squash's ordinary import-call codegen exactly like
 * vulkan_core.h does for vulkan-1.dll. Hand-written to match the real
 * ABI (struct layout, enum values, function signatures).
 *
 * The two callback parameters every real cl.h declares (pfn_notify on
 * clCreateContext/clBuildProgram) are typed here as plain "void *" rather
 * than their real function-pointer types: every caller in this codebase
 * always passes NULL for them, and a null pointer's on-the-wire
 * representation doesn't depend on its declared type, so this keeps the
 * header simple without affecting ABI correctness for the calls this
 * project actually makes. */
#include "include/windows.h"
#include <stddef.h>

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef uint32_t cl_bool;
typedef uint64_t cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_command_queue_properties;
typedef uint32_t cl_program_build_info;
typedef uint32_t cl_platform_info;
typedef uint32_t cl_device_info;
typedef int64_t cl_context_properties;

#define CL_DEFINE_HANDLE(name) typedef struct name##_st *name
CL_DEFINE_HANDLE(cl_platform_id);
CL_DEFINE_HANDLE(cl_device_id);
CL_DEFINE_HANDLE(cl_context);
CL_DEFINE_HANDLE(cl_command_queue);
CL_DEFINE_HANDLE(cl_mem);
CL_DEFINE_HANDLE(cl_program);
CL_DEFINE_HANDLE(cl_kernel);
CL_DEFINE_HANDLE(cl_event);

#define CL_SUCCESS 0
#define CL_TRUE 1
#define CL_FALSE 0

#define CL_DEVICE_TYPE_DEFAULT (1<<0)
#define CL_DEVICE_TYPE_CPU     (1<<1)
#define CL_DEVICE_TYPE_GPU     (1<<2)
#define CL_DEVICE_TYPE_ALL     0xFFFFFFFF

#define CL_MEM_READ_WRITE (1<<0)
#define CL_MEM_WRITE_ONLY (1<<1)
#define CL_MEM_READ_ONLY  (1<<2)
#define CL_MEM_COPY_HOST_PTR (1<<5)

#define CL_CONTEXT_PLATFORM 0x1084
#define CL_PROGRAM_BUILD_LOG 0x1183

cl_int WINAPI clGetPlatformIDs(cl_uint num_entries, cl_platform_id *platforms, cl_uint *num_platforms);
cl_int WINAPI clGetDeviceIDs(cl_platform_id platform, cl_device_type device_type, cl_uint num_entries, cl_device_id *devices, cl_uint *num_devices);
cl_int WINAPI clGetPlatformInfo(cl_platform_id platform, cl_platform_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret);
cl_int WINAPI clGetDeviceInfo(cl_device_id device, cl_device_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret);

cl_context WINAPI clCreateContext(const cl_context_properties *properties, cl_uint num_devices, const cl_device_id *devices, void *pfn_notify, void *user_data, cl_int *errcode_ret);
cl_int WINAPI clReleaseContext(cl_context context);

cl_command_queue WINAPI clCreateCommandQueue(cl_context context, cl_device_id device, cl_command_queue_properties properties, cl_int *errcode_ret);
cl_int WINAPI clReleaseCommandQueue(cl_command_queue command_queue);
cl_int WINAPI clFinish(cl_command_queue command_queue);

cl_mem WINAPI clCreateBuffer(cl_context context, cl_mem_flags flags, size_t size, void *host_ptr, cl_int *errcode_ret);
cl_int WINAPI clReleaseMemObject(cl_mem memobj);
cl_int WINAPI clEnqueueReadBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_read, size_t offset, size_t cb, void *ptr, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event);
cl_int WINAPI clEnqueueWriteBuffer(cl_command_queue command_queue, cl_mem buffer, cl_bool blocking_write, size_t offset, size_t cb, const void *ptr, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event);

cl_program WINAPI clCreateProgramWithSource(cl_context context, cl_uint count, const char **strings, const size_t *lengths, cl_int *errcode_ret);
cl_int WINAPI clBuildProgram(cl_program program, cl_uint num_devices, const cl_device_id *device_list, const char *options, void *pfn_notify, void *user_data);
cl_int WINAPI clGetProgramBuildInfo(cl_program program, cl_device_id device, cl_program_build_info param_name, size_t param_value_size, void *param_value, size_t *param_value_size_ret);
cl_int WINAPI clReleaseProgram(cl_program program);

cl_kernel WINAPI clCreateKernel(cl_program program, const char *kernel_name, cl_int *errcode_ret);
cl_int WINAPI clSetKernelArg(cl_kernel kernel, cl_uint arg_index, size_t arg_size, const void *arg_value);
cl_int WINAPI clReleaseKernel(cl_kernel kernel);

cl_int WINAPI clEnqueueNDRangeKernel(cl_command_queue command_queue, cl_kernel kernel, cl_uint work_dim, const size_t *global_work_offset, const size_t *global_work_size, const size_t *local_work_size, cl_uint num_events_in_wait_list, const cl_event *event_wait_list, cl_event *event);

#endif /* _SQUASH_OPENCL_H */
