/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcodec.h"
#include "binding.h"
#include <dlfcn.h>
#include <string.h>
#include "HardwareAPI.h"

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

static GstDroidCodec *codec = NULL;
G_LOCK_DEFINE_STATIC (codec);

// TODO: hardcoded
#define CONFIG_DIR   "/etc/gst-droid/droidcodec.d"

struct _GstDroidCodecHandle
{
  void *handle;

  int count;

  gchar *type;
  gchar *role;
  gchar *name;

  gboolean is_decoder;

  int in_port;
  int out_port;

    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
    OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
    OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
  // TODO:

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  // TODO:

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  // TODO:

  return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

static void
gst_droid_codec_destroy_handle (GstDroidCodecHandle * handle)
{
  OMX_ERRORTYPE err;

  GST_DEBUG ("destroying handle %p", handle);

  if (handle->type) {
    g_free (handle->type);
  }

  if (handle->role) {
    g_free (handle->role);
  }

  if (handle->name) {
    g_free (handle->name);
  }

  /* deinit */
  if (handle->deinit) {
    err = handle->deinit ();
    if (err != OMX_ErrorNone) {
      // TODO:
    }
  }

  /* unload */
  if (handle->handle) {
    /* TODO: this is crashing */
    /*    android_dlclose (handle->handle); */
    handle->handle = NULL;
  }

  /* free */
  g_slice_free (GstDroidCodecHandle, handle);
}

static GstDroidCodecHandle *
gst_droid_codec_create_and_insert_handle_locked (GstDroidCodec * codec,
    const gchar * type)
{
  OMX_ERRORTYPE err;
  gboolean res;
  gchar *path = NULL;
  GKeyFile *file = NULL;
  gchar *core_path = NULL;
  int in_port = 0;
  int out_port = 0;
  gchar *name = NULL;
  gchar *role = NULL;
  GstDroidCodecHandle *handle = NULL;
  gboolean is_decoder;
  GError *error = NULL;

  GST_DEBUG ("create and insert handle locked");

  file = g_key_file_new ();
  path = g_strdup_printf ("%s/%s.conf", CONFIG_DIR, type);

  /* read info from configuration */
  res = g_key_file_load_from_file (file, path, 0, &error);
  if (!res) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  core_path = g_key_file_get_string (file, "droidcodec", "core", &error);
  if (!core_path) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  name = g_key_file_get_string (file, "droidcodec", "component", &error);
  if (!name) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  is_decoder = strstr (name, "decoder") != NULL;

  role = g_key_file_get_string (file, "droidcodec", "role", &error);
  if (!role) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  in_port = g_key_file_get_integer (file, "droidcodec", "in-port", &error);
  if (error) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  out_port = g_key_file_get_integer (file, "droidcodec", "out-port", &error);
  if (error) {
    GST_ERROR ("error %s reading %s", error->message, path);
  }

  if (in_port == out_port) {
    GST_ERROR ("in port and out port can not be equal");
    goto error;
  }

  handle = g_slice_new0 (GstDroidCodecHandle);
  handle->count = 1;
  handle->type = g_strdup (type);
  handle->role = g_strdup (role);
  handle->name = g_strdup (name);
  handle->in_port = in_port;
  handle->out_port = out_port;
  handle->is_decoder = is_decoder;
  handle->handle = android_dlopen (core_path, RTLD_NOW);
  if (!handle->handle) {
    GST_ERROR ("error loading core %s", core_path);
    goto error;
  }

  /* dlsym */
  handle->init = android_dlsym (handle->handle, "OMX_Init");
  handle->deinit = android_dlsym (handle->handle, "OMX_Deinit");
  handle->get_handle = android_dlsym (handle->handle, "OMX_GetHandle");
  handle->free_handle = android_dlsym (handle->handle, "OMX_FreeHandle");

  if (!handle->init) {
    GST_ERROR ("OMX_Init not found");
    goto error;
  }

  if (!handle->deinit) {
    GST_ERROR ("OMX_Deinit not found");
    goto error;
  }

  if (!handle->get_handle) {
    GST_ERROR ("OMX_GetHandle not found");
    goto error;
  }

  if (!handle->free_handle) {
    GST_ERROR ("OMX_FreeHandle not found");
    goto error;
  }

  err = handle->init ();
  if (err != OMX_ErrorNone) {
    GST_ERROR ("got error %s (0x%08x) while initialization",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  g_hash_table_insert (codec->cores, (gpointer) g_strdup (type), handle);

  GST_DEBUG ("created handle %p", handle);

  return handle;

error:
  if (error) {
    g_error_free (error);
  }

  if (file) {
    g_key_file_unref (file);
  }

  if (path) {
    g_free (path);
  }

  if (name) {
    g_free (name);
  }

  if (core_path) {
    g_free (core_path);
  }

  if (role) {
    g_free (role);
  }

  /* unset deinit to prevent calling it from _destroy_handle */
  handle->deinit = NULL;
  gst_droid_codec_destroy_handle (handle);
  return NULL;
}

static void
gst_droid_codec_destroy_component (GstDroidComponent * component)
{
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (component->parent, "destroy component %p", component);

  if (component->omx) {
    err = component->handle->free_handle (component->omx);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (component->parent,
          "got error %s (0x%08x) freeing component handle",
          gst_omx_error_to_string (err), err);
    }
  }

  /* free */
  g_slice_free (GstDroidComponentPort, component->in_port);
  g_slice_free (GstDroidComponentPort, component->out_port);
  g_slice_free (GstDroidComponent, component);
}

static gboolean
gst_droid_codec_enable_android_native_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port)
{
  OMX_ERRORTYPE err;
  OMX_INDEXTYPE extension;
  struct EnableAndroidNativeBuffersParams param;
  struct GetAndroidNativeBufferUsageParams usage_param;
  OMX_STRING ext1 = "OMX.google.android.index.enableAndroidNativeBuffers2";
  OMX_STRING ext2 = "OMX.google.android.index.getAndroidNativeBufferUsage";

  GST_DEBUG_OBJECT (comp->parent, "enable android native buffers");

  /* enable */
  err = OMX_GetExtensionIndex (comp->omx, ext1, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext1);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->def.nPortIndex;
  param.enable = OMX_TRUE;
  err = gst_droid_codec_set_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while enabling android buffers",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  /* get usage for gralloc allocation */
  err = OMX_GetExtensionIndex (comp->omx, ext2, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext1);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&usage_param);
  usage_param.nPortIndex = port->def.nPortIndex;
  err = gst_droid_codec_get_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting native buffer usage",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  port->usage = usage_param.nUsage;

  return TRUE;
}

static gboolean
gst_droid_codec_enable_metadata_in_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port)
{
  OMX_ERRORTYPE err;
  OMX_INDEXTYPE extension;
  struct StoreMetaDataInBuffersParams param;
  OMX_STRING ext = "OMX.google.android.index.storeMetaDataInBuffers";

  GST_DEBUG_OBJECT (comp->parent, "enable metadata in buffers");

  err = OMX_GetExtensionIndex (comp->omx, ext, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext);

    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->def.nPortIndex;
  param.bStoreMetaData = OMX_TRUE;

  err = gst_droid_codec_set_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while enabling metadata",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

GstDroidComponent *
gst_droid_codec_get_component (GstDroidCodec * codec, const gchar * type,
    GstElement * parent)
{
  GstDroidComponent *component = NULL;
  GstDroidCodecHandle *handle;
  OMX_ERRORTYPE err;

  g_mutex_lock (&codec->lock);

  if (g_hash_table_contains (codec->cores, type)) {
    handle = (GstDroidCodecHandle *) g_hash_table_lookup (codec->cores, type);
    handle->count++;
  } else {
    handle = gst_droid_codec_create_and_insert_handle_locked (codec, type);
    if (!handle) {
      GST_ERROR_OBJECT (parent, "error getting codec %s", type);
      goto error;
    }
  }

  /* allocate */
  component = g_slice_new0 (GstDroidComponent);
  component->in_port = g_slice_new0 (GstDroidComponentPort);
  component->out_port = g_slice_new0 (GstDroidComponentPort);
  component->handle = handle;
  component->parent = parent;

  err =
      handle->get_handle (&component->omx, handle->name, component, &callbacks);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting component handle",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  /* Now create our ports. */
  component->in_port->usage = -1;
  GST_OMX_INIT_STRUCT (&component->in_port->def);
  component->in_port->def.nPortIndex = component->handle->in_port;
  component->in_port->comp = component;

  component->out_port->usage = -1;
  GST_OMX_INIT_STRUCT (&component->out_port->def);
  component->out_port->def.nPortIndex = component->handle->out_port;
  component->out_port->comp = component;

  if (component->handle->is_decoder) {
    /* enable usage of android native buffers on output port */
    if (!gst_droid_codec_enable_android_native_buffers (component,
            component->out_port)) {
      goto error;
    }
  } else {
    /* encoders get meta data usage enabled */
    if (!gst_droid_codec_enable_metadata_in_buffers (component,
            component->in_port)) {
      goto error;
    }
  }

  err =
      gst_droid_codec_get_param (component, OMX_IndexParamPortDefinition,
      &component->in_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting input port definition",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  err =
      gst_droid_codec_get_param (component, OMX_IndexParamPortDefinition,
      &component->out_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting output port definition",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  goto unlock_and_out;

error:
  gst_droid_codec_destroy_component (component);
  component = NULL;

unlock_and_out:
  g_mutex_unlock (&codec->lock);

  return component;
}

void
gst_droid_codec_put_component (GstDroidCodec * codec,
    GstDroidComponent * component)
{
  GST_DEBUG_OBJECT (component->parent, "put component");

  g_mutex_lock (&codec->lock);

  if (component->handle->count > 1) {
    component->handle->count--;
  } else {
    g_hash_table_remove (codec->cores, component->handle->type);
  }

  g_mutex_unlock (&codec->lock);

  gst_droid_codec_destroy_component (component);
}

static void
gst_droid_codec_free ()
{
  GST_DEBUG ("codec free");

  G_LOCK (codec);

  g_mutex_clear (&codec->lock);
  g_hash_table_unref (codec->cores);
  g_slice_free (GstDroidCodec, codec);
  codec = NULL;

  G_UNLOCK (codec);
}

GstDroidCodec *
gst_droid_codec_get (void)
{
  GST_DEBUG ("codec get");

  G_LOCK (codec);

  if (!codec) {
    codec = g_slice_new (GstDroidCodec);
    codec->cores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gst_droid_codec_destroy_handle);
    g_mutex_init (&codec->lock);
    gst_mini_object_init (GST_MINI_OBJECT_CAST (codec), 0, GST_TYPE_DROID_CODEC,
        NULL, NULL, (GstMiniObjectFreeFunction) gst_droid_codec_free);
  }

  G_UNLOCK (codec);

  return codec;
}

OMX_ERRORTYPE
gst_droid_codec_get_param (GstDroidComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  GST_DEBUG_OBJECT (comp->parent, "getting parameter at index 0x%08x", index);

  return OMX_GetParameter (comp->omx, index, param);
}

OMX_ERRORTYPE
gst_droid_codec_set_param (GstDroidComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  GST_DEBUG_OBJECT (comp->parent, "setting parameter at index 0x%08x", index);

  return OMX_SetParameter (comp->omx, index, param);
}

gboolean
gst_droid_codec_configure_component (GstDroidComponent * comp,
    const GstVideoInfo * info)
{
  OMX_ERRORTYPE err;
  OMX_PARAM_PORTDEFINITIONTYPE def = comp->in_port->def;

  GST_DEBUG_OBJECT (comp->parent, "configure component");

  def.format.video.nFrameWidth = info->width;
  def.format.video.nFrameHeight = info->height;

  if (info->fps_n == 0) {
    def.format.video.xFramerate = 0;
  } else {
    def.format.video.xFramerate = (info->fps_n << 16) / (info->fps_d);
  }

  err = gst_droid_codec_set_param (comp, OMX_IndexParamPortDefinition, &def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) setting input port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamPortDefinition,
      &comp->in_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting input port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamPortDefinition,
      &comp->out_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting output port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_allocate_port_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port)
{
  int x = 0;
  for (x = 0; x < port->def.nBufferCountActual; x++) {
    //    OMX_AllocateBuffer ();

  }

  return FALSE;
}

gboolean
gst_droid_codec_start_component (GstDroidComponent * comp)
{
  OMX_STATETYPE state;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "start");

  /* TODO: locking */

  err = OMX_GetState (comp->omx, &state);
  if (err != OMX_ErrorNone) {
    return FALSE;
  }

  if (state == OMX_StateExecuting) {
    return TRUE;
  }

  if (state <= OMX_StateInvalid || state > OMX_StateExecuting) {
    return FALSE;
  }

  if (state != OMX_StateLoaded) {
    return FALSE;
  }

  err = OMX_SendCommand (comp->omx, OMX_CommandStateSet, OMX_StateIdle, NULL);

  if (err != OMX_ErrorNone) {
    return FALSE;
  }

  /* Let's allocate our buffers. */
  if (!gst_droid_codec_allocate_port_buffers (comp, comp->in_port)) {
    return FALSE;
  }

  if (!gst_droid_codec_allocate_port_buffers (comp, comp->out_port)) {
    return FALSE;
  }

  /* TODO: */


  return TRUE;
}
