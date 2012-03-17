#define __GEGL_CL_INIT_MAIN__
#include "gegl-cl-init.h"
#undef __GEGL_CL_INIT_MAIN__

#include <gmodule.h>
#include <string.h>
#include <stdio.h>

guint
gegl_cl_count_lines(const char* kernel_source[])
{
  guint count = 0;
  while (kernel_source[count++] != NULL);
  return count-1;
}

/* http://forums.amd.com/forum/messageview.cfm?catid=390&threadid=128536 */
char *gegl_cl_errstring(cl_int err) {
  switch (err) {
    case CL_SUCCESS:                          return strdup("Success!");
    case CL_DEVICE_NOT_FOUND:                 return strdup("Device not found.");
    case CL_DEVICE_NOT_AVAILABLE:             return strdup("Device not available");
    case CL_COMPILER_NOT_AVAILABLE:           return strdup("Compiler not available");
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:    return strdup("Memory object allocation failure");
    case CL_OUT_OF_RESOURCES:                 return strdup("Out of resources");
    case CL_OUT_OF_HOST_MEMORY:               return strdup("Out of host memory");
    case CL_PROFILING_INFO_NOT_AVAILABLE:     return strdup("Profiling information not available");
    case CL_MEM_COPY_OVERLAP:                 return strdup("Memory copy overlap");
    case CL_IMAGE_FORMAT_MISMATCH:            return strdup("Image format mismatch");
    case CL_IMAGE_FORMAT_NOT_SUPPORTED:       return strdup("Image format not supported");
    case CL_BUILD_PROGRAM_FAILURE:            return strdup("Program build failure");
    case CL_MAP_FAILURE:                      return strdup("Map failure");
    case CL_INVALID_VALUE:                    return strdup("Invalid value");
    case CL_INVALID_DEVICE_TYPE:              return strdup("Invalid device type");
    case CL_INVALID_PLATFORM:                 return strdup("Invalid platform");
    case CL_INVALID_DEVICE:                   return strdup("Invalid device");
    case CL_INVALID_CONTEXT:                  return strdup("Invalid context");
    case CL_INVALID_QUEUE_PROPERTIES:         return strdup("Invalid queue properties");
    case CL_INVALID_COMMAND_QUEUE:            return strdup("Invalid command queue");
    case CL_INVALID_HOST_PTR:                 return strdup("Invalid host pointer");
    case CL_INVALID_MEM_OBJECT:               return strdup("Invalid memory object");
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:  return strdup("Invalid image format descriptor");
    case CL_INVALID_IMAGE_SIZE:               return strdup("Invalid image size");
    case CL_INVALID_SAMPLER:                  return strdup("Invalid sampler");
    case CL_INVALID_BINARY:                   return strdup("Invalid binary");
    case CL_INVALID_BUILD_OPTIONS:            return strdup("Invalid build options");
    case CL_INVALID_PROGRAM:                  return strdup("Invalid program");
    case CL_INVALID_PROGRAM_EXECUTABLE:       return strdup("Invalid program executable");
    case CL_INVALID_KERNEL_NAME:              return strdup("Invalid kernel name");
    case CL_INVALID_KERNEL_DEFINITION:        return strdup("Invalid kernel definition");
    case CL_INVALID_KERNEL:                   return strdup("Invalid kernel");
    case CL_INVALID_ARG_INDEX:                return strdup("Invalid argument index");
    case CL_INVALID_ARG_VALUE:                return strdup("Invalid argument value");
    case CL_INVALID_ARG_SIZE:                 return strdup("Invalid argument size");
    case CL_INVALID_KERNEL_ARGS:              return strdup("Invalid kernel arguments");
    case CL_INVALID_WORK_DIMENSION:           return strdup("Invalid work dimension");
    case CL_INVALID_WORK_GROUP_SIZE:          return strdup("Invalid work group size");
    case CL_INVALID_WORK_ITEM_SIZE:           return strdup("Invalid work item size");
    case CL_INVALID_GLOBAL_OFFSET:            return strdup("Invalid global offset");
    case CL_INVALID_EVENT_WAIT_LIST:          return strdup("Invalid event wait list");
    case CL_INVALID_EVENT:                    return strdup("Invalid event");
    case CL_INVALID_OPERATION:                return strdup("Invalid operation");
    case CL_INVALID_GL_OBJECT:                return strdup("Invalid OpenGL object");
    case CL_INVALID_BUFFER_SIZE:              return strdup("Invalid buffer size");
    case CL_INVALID_MIP_LEVEL:                return strdup("Invalid mip-map level");
    default:                                  return strdup("Unknown");
  }
}

gboolean
gegl_cl_is_accelerated (void)
{
  return cl_state.is_accelerated;
}

cl_platform_id
gegl_cl_get_platform (void)
{
  return cl_state.platform;
}

cl_device_id
gegl_cl_get_device (void)
{
  return cl_state.device;
}

cl_context
gegl_cl_get_context (void)
{
  return cl_state.ctx;
}

cl_command_queue
gegl_cl_get_command_queue (void)
{
  return cl_state.cq;
}

#define CL_LOAD_FUNCTION(func)                                                    \
if (!g_module_symbol (module, #func, (gpointer *)& gegl_##func))                  \
  {                                                                               \
    g_set_error (error, 0, 0,                                                     \
                 "%s: %s", "libOpenCL.so", g_module_error ());                    \
    if (!g_module_close (module))                                                 \
      g_warning ("%s: %s", "libOpenCL.so", g_module_error ());                    \
    return FALSE;                                                                 \
  }                                                                               \
if (gegl_##func == NULL)                                                          \
  {                                                                               \
    g_set_error (error, 0, 0, "symbol gegl_##func is NULL");                      \
    if (!g_module_close (module))                                                 \
      g_warning ("%s: %s", "libOpenCL.so", g_module_error ());                    \
    return FALSE;                                                                 \
  }

gboolean
gegl_cl_init (GError **error)
{
  GModule *module;
  cl_int err;

  if (!cl_state.is_accelerated)
    {
      module = g_module_open ("libOpenCL.so", G_MODULE_BIND_LAZY);

      if (!module)
        {
          g_set_error (error, 0, 0, "%s", g_module_error ());
          return FALSE;
        }

      CL_LOAD_FUNCTION (clGetPlatformIDs)
      CL_LOAD_FUNCTION (clGetPlatformInfo)
      CL_LOAD_FUNCTION (clGetDeviceIDs)
      CL_LOAD_FUNCTION (clGetDeviceInfo)

      CL_LOAD_FUNCTION (clCreateContext)
      CL_LOAD_FUNCTION (clCreateContextFromType)
      CL_LOAD_FUNCTION (clCreateCommandQueue)
      CL_LOAD_FUNCTION (clCreateProgramWithSource)
      CL_LOAD_FUNCTION (clBuildProgram)
      CL_LOAD_FUNCTION (clGetProgramBuildInfo)

      CL_LOAD_FUNCTION (clCreateKernel)
      CL_LOAD_FUNCTION (clSetKernelArg)
      CL_LOAD_FUNCTION (clGetKernelWorkGroupInfo)
      CL_LOAD_FUNCTION (clCreateBuffer)
      CL_LOAD_FUNCTION (clEnqueueWriteBuffer)
      CL_LOAD_FUNCTION (clEnqueueReadBuffer)
      CL_LOAD_FUNCTION (clEnqueueCopyBuffer)
      CL_LOAD_FUNCTION (clCreateImage2D)
      CL_LOAD_FUNCTION (clEnqueueWriteImage)
      CL_LOAD_FUNCTION (clEnqueueReadImage)
      CL_LOAD_FUNCTION (clEnqueueCopyImage)
      CL_LOAD_FUNCTION (clEnqueueCopyBufferToImage)
      CL_LOAD_FUNCTION (clEnqueueCopyImageToBuffer)
      CL_LOAD_FUNCTION (clEnqueueNDRangeKernel)
      CL_LOAD_FUNCTION (clEnqueueBarrier)
      CL_LOAD_FUNCTION (clFinish)

      CL_LOAD_FUNCTION (clEnqueueMapBuffer)
      CL_LOAD_FUNCTION (clEnqueueMapImage)
      CL_LOAD_FUNCTION (clEnqueueUnmapMemObject)

      CL_LOAD_FUNCTION (clReleaseKernel)
      CL_LOAD_FUNCTION (clReleaseProgram)
      CL_LOAD_FUNCTION (clReleaseCommandQueue)
      CL_LOAD_FUNCTION (clReleaseContext)
      CL_LOAD_FUNCTION (clReleaseMemObject)

      gegl_clGetPlatformIDs (1, &cl_state.platform, NULL);

      gegl_clGetPlatformInfo (cl_state.platform, CL_PLATFORM_NAME,       sizeof(cl_state.platform_name),    cl_state.platform_name,    NULL);
      gegl_clGetPlatformInfo (cl_state.platform, CL_PLATFORM_VERSION,    sizeof(cl_state.platform_version), cl_state.platform_version, NULL);
      gegl_clGetPlatformInfo (cl_state.platform, CL_PLATFORM_EXTENSIONS, sizeof(cl_state.platform_ext),     cl_state.platform_ext,     NULL);

      gegl_clGetDeviceIDs (cl_state.platform, CL_DEVICE_TYPE_DEFAULT, 1, &cl_state.device, NULL);
      gegl_clGetDeviceInfo(cl_state.device, CL_DEVICE_NAME, sizeof(cl_state.device_name), cl_state.device_name, NULL);

      gegl_clGetDeviceInfo (cl_state.device, CL_DEVICE_IMAGE_SUPPORT,      sizeof(cl_bool),  &cl_state.image_support,    NULL);
      gegl_clGetDeviceInfo (cl_state.device, CL_DEVICE_IMAGE2D_MAX_HEIGHT, sizeof(size_t),   &cl_state.max_image_height, NULL);
      gegl_clGetDeviceInfo (cl_state.device, CL_DEVICE_IMAGE2D_MAX_WIDTH,  sizeof(size_t),   &cl_state.max_image_width,  NULL);
      gegl_clGetDeviceInfo (cl_state.device, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(cl_ulong), &cl_state.max_mem_alloc,    NULL);

      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Platform Name:%s",       cl_state.platform_name);
      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Version:%s",             cl_state.platform_version);
      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Extensions:%s",          cl_state.platform_ext);
      g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Default Device Name:%s", cl_state.device_name);

      if (cl_state.image_support)
        {
          g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Image Support OK");
        }
      else
        {
          g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Image Support Error");
          return FALSE;
        }

      cl_state.ctx = gegl_clCreateContext(0, 1, &cl_state.device, NULL, NULL, &err);
      if(err != CL_SUCCESS)
        {
          g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Could not create context");
          return FALSE;
        }

      cl_state.cq  = gegl_clCreateCommandQueue(cl_state.ctx, cl_state.device, 0, &err);

      if(err != CL_SUCCESS)
        {
          g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "[OpenCL] Could not create command queue");
          return FALSE;
        }

    }

  cl_state.is_accelerated = TRUE;
  return TRUE;
}

#undef CL_LOAD_FUNCTION
