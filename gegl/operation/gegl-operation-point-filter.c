/* This file is part of GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006 Øyvind Kolås
 */


#include "config.h"

#include <glib-object.h>

#include "gegl.h"
#include "gegl-types-internal.h"
#include "gegl-operation-point-filter.h"
#include "graph/gegl-pad.h"
#include "graph/gegl-node.h"
#include "gegl-utils.h"
#include <string.h>

#include "gegl-buffer-private.h"
#include "gegl-tile-storage.h"

#include "opencl/gegl-cl.h"

static gboolean gegl_operation_point_filter_process
                              (GeglOperation       *operation,
                               GeglBuffer          *input,
                               GeglBuffer          *output,
                               const GeglRectangle *result);

static gboolean gegl_operation_point_filter_op_process
                              (GeglOperation       *operation,
                               GeglOperationContext *context,
                               const gchar          *output_pad,
                               const GeglRectangle  *roi);

G_DEFINE_TYPE (GeglOperationPointFilter, gegl_operation_point_filter, GEGL_TYPE_OPERATION_FILTER)

static void prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "input", babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

static void
gegl_operation_point_filter_class_init (GeglOperationPointFilterClass *klass)
{
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  operation_class->process = gegl_operation_point_filter_op_process;
  operation_class->prepare = prepare;
  operation_class->no_cache = TRUE;

  klass->process = NULL;
  klass->cl_process = NULL;
}

static void
gegl_operation_point_filter_init (GeglOperationPointFilter *self)
{
}

struct buf_tex
{
  GeglBuffer *buf;
  GeglRectangle *region;
  cl_mem *tex;
};

//#define CL_ERROR {g_assert(0);}
#define CL_ERROR {g_printf("[OpenCL] Error in %s:%d@%s - %s\n", __FILE__, __LINE__, __func__, gegl_cl_errstring(errcode)); goto error;}

static gboolean
gegl_operation_point_filter_cl_process_full (GeglOperation       *operation,
                                             GeglBuffer          *input,
                                             GeglBuffer          *output,
                                             const GeglRectangle *result)
{
  const Babl *in_format  = gegl_operation_get_format (operation, "input");
  const Babl *out_format = gegl_operation_get_format (operation, "output");

  GeglOperationPointFilterClass *point_filter_class = GEGL_OPERATION_POINT_FILTER_GET_CLASS (operation);

  int y, x, i;
  int errcode = 0;

  gfloat** out_data = NULL;

  int ntex = 0;
  struct buf_tex input_tex;
  struct buf_tex output_tex;

  const size_t origin_zero[3] = {0, 0, 0};

  gegl_cl_color_op conv_in;
  gegl_cl_color_op conv_out;

  cl_image_format in_image_format;
  cl_image_format out_image_format;

  size_t bpp;

  /* non-texturizable format! */
  if (!gegl_cl_color_babl (in_format,  NULL, NULL) ||
      !gegl_cl_color_babl (out_format, NULL, NULL))
    {
      g_warning ("[OpenCL] Non-texturizable input of output format!");
      return FALSE;
    }

  if (!gegl_cl_color_babl (input->format, &in_image_format, &bpp))
    gegl_cl_color_babl (in_format, &in_image_format, &bpp);

  if (!gegl_cl_color_babl (output->format, &out_image_format, &bpp))
    gegl_cl_color_babl (out_format, &out_image_format, &bpp);

  conv_in  = gegl_cl_color_supported (input->format,   in_format);
  conv_out = gegl_cl_color_supported (out_format, output->format);

  g_printf("[OpenCL] BABL formats: (%s,%s:%d) (%s,%s:%d)\n \t Tile Size:(%d, %d)\n",
           babl_get_name(input->format),
           babl_get_name(in_format),
           conv_in,
           babl_get_name(out_format),
           babl_get_name(output->format),
           conv_out,
           input->tile_storage->tile_width,
           input->tile_storage->tile_height);

  for (y=result->y; y < result->y + result->height; y += cl_state.max_image_height)
   for (x=result->x; x < result->x + result->width;  x += cl_state.max_image_width)
     ntex++;

  input_tex.region  = (GeglRectangle *) g_new0 (GeglRectangle, ntex);
  output_tex.region = (GeglRectangle *) g_new0 (GeglRectangle, ntex);
  input_tex.tex     = (cl_mem *)        g_new0 (cl_mem,        ntex);
  output_tex.tex    = (cl_mem *)        g_new0 (cl_mem,        ntex);

  i = 0;
  for (y=result->y; y < result->y + result->height; y += cl_state.max_image_height)
    for (x=result->x; x < result->x + result->width;  x += cl_state.max_image_width)
      {
        const size_t region[3] = {MIN(cl_state.max_image_width,  result->x + result->width  - x),
                                  MIN(cl_state.max_image_height, result->y + result->height - y),
                                  1};

        GeglRectangle r = {x, y, region[0], region[1]};
        input_tex.region[i] = output_tex.region[i] = r;

        input_tex.tex[i]  = gegl_clCreateImage2D (gegl_cl_get_context(),
                                                  CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_WRITE,
                                                  &in_image_format,
                                                  region[0], region[1],
                                                  0,  NULL, &errcode);
        if (errcode != CL_SUCCESS) CL_ERROR;

        output_tex.tex[i] = gegl_clCreateImage2D (gegl_cl_get_context(),
                                                  CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_WRITE,
                                                  &out_image_format,
                                                  region[0], region[1],
                                                  0,  NULL, &errcode);
        if (errcode != CL_SUCCESS) CL_ERROR;

        i++;
      }

  for (i=0; i < ntex; i++)
    {
      gpointer data;
      size_t pitch;
      const size_t region[3] = {input_tex.region[i].width, input_tex.region[i].height, 1};

      /* pre-pinned memory */
      data = gegl_clEnqueueMapImage(gegl_cl_get_command_queue(), input_tex.tex[i], CL_TRUE,
                                    CL_MAP_WRITE,
                                    origin_zero, region, &pitch, NULL,
                                    0, NULL, NULL, &errcode);
      if (errcode != CL_SUCCESS) CL_ERROR;

      /* un-tile */
      if (conv_in == CL_COLOR_NOT_SUPPORTED)
        /* color conversion using BABL */
        gegl_buffer_get (input, 1.0, &input_tex.region[i], in_format, data, pitch);
      else
        /* color conversion will be performed in the GPU later */
        gegl_buffer_get (input, 1.0, &input_tex.region[i], input->format, data, pitch);

      errcode = gegl_clEnqueueUnmapMemObject (gegl_cl_get_command_queue(), input_tex.tex[i], data,
                                              0, NULL, NULL);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* color conversion in the GPU (input) */
  if (conv_in == CL_COLOR_CONVERT)
    for (i=0; i < ntex; i++)
     {
       const size_t size[2] = {input_tex.region[i].width, input_tex.region[i].height};
       errcode = gegl_cl_color_conv (&input_tex.tex[i], &output_tex.tex[i], size, input->format, in_format);
       if (errcode == FALSE) CL_ERROR;
     }

  /* Process */
  for (i=0; i < ntex; i++)
    {
      const size_t region[3] = {input_tex.region[i].width, input_tex.region[i].height, 1};
      const size_t global_worksize[2] = {region[0], region[1]};

      errcode = point_filter_class->cl_process(operation, input_tex.tex[i], output_tex.tex[i], global_worksize, &input_tex.region[i]);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  /* Wait Processing */
  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* color conversion in the GPU (output) */
  if (conv_out == CL_COLOR_CONVERT)
    for (i=0; i < ntex; i++)
     {
       const size_t size[2] = {output_tex.region[i].width, output_tex.region[i].height};
       errcode = gegl_cl_color_conv (&output_tex.tex[i], &input_tex.tex[i], size, out_format, output->format);
     }

  /* Wait Processing */
  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* GPU -> CPU */
  for (i=0; i < ntex; i++)
    {
      gpointer data;
      size_t pitch;
      const size_t region[3] = {output_tex.region[i].width, output_tex.region[i].height, 1};

      data = gegl_clEnqueueMapImage(gegl_cl_get_command_queue(), output_tex.tex[i], CL_TRUE,
                                    CL_MAP_READ,
                                    origin_zero, region, &pitch, NULL,
                                    0, NULL, NULL, &errcode);
      if (errcode != CL_SUCCESS) CL_ERROR;

      /* tile-ize */
      if (conv_out == CL_COLOR_NOT_SUPPORTED)
        /* color conversion using BABL */
        gegl_buffer_set (output, &output_tex.region[i], out_format, data, pitch);
      else
        /* color conversion has already been be performed in the GPU */
        gegl_buffer_set (output, &output_tex.region[i], output->format, data, pitch);

      errcode = gegl_clEnqueueUnmapMemObject (gegl_cl_get_command_queue(), output_tex.tex[i], data,
                                              0, NULL, NULL);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  /* Run! */
  errcode = gegl_clFinish(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  for (i=0; i < ntex; i++)
    {
      gegl_clReleaseMemObject (input_tex.tex[i]);
      gegl_clReleaseMemObject (output_tex.tex[i]);
    }

  g_free(input_tex.tex);
  g_free(output_tex.tex);
  g_free(input_tex.region);
  g_free(output_tex.region);

  return TRUE;

error:

    for (i=0; i < ntex; i++)
      {
        if (input_tex.tex[i])  gegl_clReleaseMemObject (input_tex.tex[i]);
        if (output_tex.tex[i]) gegl_clReleaseMemObject (output_tex.tex[i]);
      }

  if (input_tex.tex)     g_free(input_tex.tex);
  if (output_tex.tex)    g_free(output_tex.tex);
  if (input_tex.region)  g_free(input_tex.region);
  if (output_tex.region) g_free(output_tex.region);

  return FALSE;
}

#undef CL_ERROR

static gboolean
gegl_operation_point_filter_process (GeglOperation       *operation,
                                     GeglBuffer          *input,
                                     GeglBuffer          *output,
                                     const GeglRectangle *result)
{
  const Babl *in_format  = gegl_operation_get_format (operation, "input");
  const Babl *out_format = gegl_operation_get_format (operation, "output");
  GeglOperationPointFilterClass *point_filter_class;

  point_filter_class = GEGL_OPERATION_POINT_FILTER_GET_CLASS (operation);

  if ((result->width > 0) && (result->height > 0))
    {
      if (cl_state.is_accelerated && point_filter_class->cl_process)
        {
          if (gegl_operation_point_filter_cl_process_full (operation, input, output, result))
            return TRUE;
        }

      {
        GeglBufferIterator *i = gegl_buffer_iterator_new (output, result, out_format, GEGL_BUFFER_WRITE);
        gint read = /*output == input ? 0 :*/ gegl_buffer_iterator_add (i, input,  result, in_format, GEGL_BUFFER_READ);
        /* using separate read and write iterators for in-place ideally a single
         * readwrite indice would be sufficient
         */
          while (gegl_buffer_iterator_next (i))
            point_filter_class->process (operation, i->data[read], i->data[0], i->length, &i->roi[0]);
      }
    }
  return TRUE;
}

gboolean gegl_can_do_inplace_processing (GeglOperation       *operation,
                                         GeglBuffer          *input,
                                         const GeglRectangle *result);

gboolean gegl_can_do_inplace_processing (GeglOperation       *operation,
                                         GeglBuffer          *input,
                                         const GeglRectangle *result)
{
  if (!input ||
      GEGL_IS_CACHE (input))
    return FALSE;
  if (gegl_object_get_has_forked (input))
    return FALSE;

  if (input->format == gegl_operation_get_format (operation, "output") &&
      gegl_rectangle_contains (gegl_buffer_get_extent (input), result))
    return TRUE;
  return FALSE;
}


static gboolean gegl_operation_point_filter_op_process
                              (GeglOperation       *operation,
                               GeglOperationContext *context,
                               const gchar          *output_pad,
                               const GeglRectangle  *roi)
{
  GeglBuffer               *input;
  GeglBuffer               *output;
  gboolean                  success = FALSE;

  input = gegl_operation_context_get_source (context, "input");

  if (gegl_can_do_inplace_processing (operation, input, roi))
    {
      output = g_object_ref (input);
      gegl_operation_context_take_object (context, "output", G_OBJECT (output));
    }
  else
    {
      output = gegl_operation_context_get_target (context, "output");
    }

  success = gegl_operation_point_filter_process (operation, input, output, roi);
  if (output == GEGL_BUFFER (operation->node->cache))
    gegl_cache_computed (operation->node->cache, roi);

  if (input != NULL)
    g_object_unref (input);
  return success;
}
