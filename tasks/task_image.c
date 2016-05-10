/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2016 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <file/nbio.h>
#include <formats/image.h>
#include <compat/strl.h>
#include <retro_assert.h>
#include <retro_miscellaneous.h>
#include <lists/string_list.h>
#include <rhash.h>
#ifdef HAVE_RPNG
#include <formats/rpng.h>
#endif

#ifdef HAVE_MENU
#include "../menu/menu_driver.h"
#endif

#include "tasks_internal.h"
#include "../verbosity.h"

enum nbio_image_status_enum
{
   NBIO_IMAGE_STATUS_POLL = 0,
   NBIO_IMAGE_STATUS_TRANSFER,
   NBIO_IMAGE_STATUS_TRANSFER_PARSE,
   NBIO_IMAGE_STATUS_PROCESS_TRANSFER,
   NBIO_IMAGE_STATUS_PROCESS_TRANSFER_PARSE,
   NBIO_IMAGE_STATUS_TRANSFER_PARSE_FREE
};

static int cb_image_menu_upload_generic(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data;
   unsigned r_shift, g_shift, b_shift, a_shift;

   if (!nbio)
      return -1;

   if (nbio->image.processing_final_state == IMAGE_PROCESS_ERROR ||
         nbio->image.processing_final_state == IMAGE_PROCESS_ERROR_END)
      return -1;

   video_texture_image_set_color_shifts(&r_shift, &g_shift, &b_shift,
         &a_shift);

   video_texture_image_color_convert(r_shift, g_shift, b_shift,
         a_shift, &nbio->image.ti);

   nbio->image.is_blocking_on_processing         = false;
   nbio->image.is_blocking                       = true;
   nbio->image.is_finished                       = true;
   nbio->is_finished                             = true;

   return 0;
}

static int rarch_main_data_image_iterate_process_transfer_parse(
      nbio_handle_t *nbio)
{
   if (nbio->image.handle && nbio->image.cb)
   {
      size_t len = 0;
      nbio->image.cb(nbio, len);
   }

   return 0;
}

static int rarch_main_data_image_iterate_transfer_parse(nbio_handle_t *nbio)
{
   if (nbio->image.handle && nbio->image.cb)
   {
      size_t len = 0;
      nbio->image.cb(nbio, len);
   }

   return 0;
}

static int cb_nbio_default(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data;

   if (!data)
      return -1;

   (void)len;

   nbio->is_finished   = true;

   return 0;
}

#ifdef HAVE_RPNG
static int cb_image_menu_generic_rpng(nbio_handle_t *nbio)
{
   unsigned width = 0, height = 0;
   int retval;
   if (!nbio)
      return -1;

   if (!rpng_is_valid(nbio->image.handle))
      return -1;

   retval = rpng_nbio_load_image_argb_process(nbio->image.handle,
         &nbio->image.ti.pixels, &width, &height);

   nbio->image.ti.width  = width;
   nbio->image.ti.height = height;

   if (retval == IMAGE_PROCESS_ERROR)
      return -1;
   if (retval == IMAGE_PROCESS_ERROR_END)
      return -1;

   nbio->image.is_blocking_on_processing         = true;
   nbio->image.is_finished                       = false;

   return 0;
}

static int cb_image_menu_wallpaper_rpng(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data; 

   if (cb_image_menu_generic_rpng(nbio) != 0)
      return -1;

   nbio->image.cb = &cb_image_menu_upload_generic;

   return 0;
}

static int cb_image_menu_thumbnail(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data; 

   if (cb_image_menu_generic_rpng(nbio) != 0)
      return -1;

   nbio->image.cb = &cb_image_menu_upload_generic;

   return 0;
}

static int rarch_main_data_image_iterate_transfer(nbio_handle_t *nbio)
{
   unsigned i;

   if (!nbio)
      return -1;

   if (nbio->image.is_finished)
      return 0;

   for (i = 0; i < nbio->image.pos_increment; i++)
   {
      /* TODO/FIXME - add JPEG equivalents as well */
      if (!rpng_nbio_load_image_argb_iterate(nbio->image.handle))
         goto error;
   }

   nbio->image.frame_count++;
   return 0;

error:
   return -1;
}

static int rarch_main_data_image_iterate_process_transfer(nbio_handle_t *nbio)
{
   unsigned i, width = 0, height = 0;
   int retval = 0;

   if (!nbio)
      return -1;

   for (i = 0; i < nbio->image.processing_pos_increment; i++)
   {
      /* TODO/FIXME -add JPEG equivalents as well */
      retval = rpng_nbio_load_image_argb_process(nbio->image.handle,
            &nbio->image.ti.pixels, &width, &height);

      nbio->image.ti.width  = width;
      nbio->image.ti.height = height;

      if (retval != IMAGE_PROCESS_NEXT)
         break;
   }

   if (retval == IMAGE_PROCESS_NEXT)
      return 0;

   nbio->image.processing_final_state = retval;
   return -1;
}

static int cb_nbio_generic_rpng(nbio_handle_t *nbio, size_t *len)
{
   void *ptr           = NULL;

   if (!nbio->image.handle)
   {
      nbio->image.cb = NULL;
      return -1;
   }

   ptr = nbio_get_ptr(nbio->handle, len);

   if (!ptr)
   {
      rpng_nbio_load_image_free(nbio->image.handle);
      nbio->image.handle = NULL;
      nbio->image.cb     = NULL;

      return -1;
   }

   rpng_set_buf_ptr(nbio->image.handle, (uint8_t*)ptr);
   nbio->image.pos_increment            = (*len / 2) ? (*len / 2) : 1;
   nbio->image.processing_pos_increment = (*len / 4) ?  (*len / 4) : 1;

   if (!rpng_nbio_load_image_argb_start(nbio->image.handle))
   {
      rpng_nbio_load_image_free(nbio->image.handle);
      return -1;
   }

   nbio->image.is_blocking   = false;
   nbio->image.is_finished   = false;
   nbio->is_finished         = true;

   return 0;
}

static int cb_nbio_image_menu_wallpaper_rpng(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data; 

   if (!nbio || !data)
      return -1;
   
   nbio->image.handle = rpng_alloc();
   nbio->image.cb     = &cb_image_menu_wallpaper_rpng;

   return cb_nbio_generic_rpng(nbio, &len);
}

static int cb_nbio_image_menu_thumbnail_rpng(void *data, size_t len)
{
   nbio_handle_t *nbio = (nbio_handle_t*)data; 

   if (!nbio || !data)
      return -1;
   
   nbio->image.handle = rpng_alloc();
   nbio->image.cb     = &cb_image_menu_thumbnail;

   return cb_nbio_generic_rpng(nbio, &len);
}
#endif

bool rarch_task_image_load_handler(retro_task_t *task)
{
   nbio_handle_t       *nbio  = (nbio_handle_t*)task->state;
   nbio_image_handle_t *image = nbio ? &nbio->image : NULL;

   switch (image->status)
   {
      case NBIO_IMAGE_STATUS_PROCESS_TRANSFER:
         if (rarch_main_data_image_iterate_process_transfer(nbio) == -1)
            image->status = NBIO_IMAGE_STATUS_PROCESS_TRANSFER_PARSE;
         break;
      case NBIO_IMAGE_STATUS_TRANSFER_PARSE:
         rarch_main_data_image_iterate_transfer_parse(nbio);
         if (image->is_blocking_on_processing)
            image->status = NBIO_IMAGE_STATUS_PROCESS_TRANSFER;
         break;
      case NBIO_IMAGE_STATUS_TRANSFER:
         if (!image->is_blocking)
            if (rarch_main_data_image_iterate_transfer(nbio) == -1)
               image->status = NBIO_IMAGE_STATUS_TRANSFER_PARSE;
         break;
      case NBIO_IMAGE_STATUS_PROCESS_TRANSFER_PARSE:
         rarch_main_data_image_iterate_process_transfer_parse(nbio);
         if (!image->is_finished)
            break;
      case NBIO_IMAGE_STATUS_TRANSFER_PARSE_FREE:
      case NBIO_IMAGE_STATUS_POLL:
      default:
         break;
   }

   if (     nbio->is_finished 
         && nbio->image.is_finished 
         && !task->cancelled)
   {
      task->task_data = malloc(sizeof(nbio->image.ti));

      if (task->task_data)
         memcpy(task->task_data, &nbio->image.ti, sizeof(nbio->image.ti));

      return false;
   }

   return true;
}

bool rarch_task_push_image_load(const char *fullpath,
      const char *type, retro_task_callback_t cb, void *user_data)
{
#if defined(HAVE_RPNG) && defined(HAVE_MENU)
   nbio_handle_t *nbio          = NULL;
   retro_task_t *t              = NULL;
   uint32_t cb_type_hash        = 0;
   struct nbio_t* handle        = NULL;

   cb_type_hash = djb2_calculate(type);

   handle = nbio_open(fullpath, NBIO_READ);

   if (!handle)
   {
      RARCH_ERR("[image load] Failed to open '%s': %s.\n",
            fullpath, strerror(errno));
      return false;
   }

   nbio = (nbio_handle_t*)calloc(1, sizeof(*nbio));

   if (!nbio)
      return false;

   nbio->handle       = handle;
   nbio->is_finished  = false;
   nbio->cb           = &cb_nbio_default;
   nbio->status       = NBIO_STATUS_TRANSFER;
   nbio->image.status = NBIO_IMAGE_STATUS_TRANSFER;

   if (strstr(fullpath, ".png"))
   {
#ifdef HAVE_RPNG
      if (cb_type_hash == CB_MENU_WALLPAPER)
         nbio->cb = &cb_nbio_image_menu_wallpaper_rpng;
      else if (cb_type_hash == CB_MENU_THUMBNAIL)
         nbio->cb = &cb_nbio_image_menu_thumbnail_rpng;
#endif
   }
   else if (strstr(fullpath, ".jpeg") || strstr(fullpath, ".jpg"))
   {
      /* TODO/FIXME */
   }

   nbio_begin_read(handle);

   t = (retro_task_t*)calloc(1, sizeof(*t));

   if (!t)
   {
      free(nbio);
      return false;
   }

   t->state     = nbio;
   t->handler   = rarch_task_file_load_handler;
   t->callback  = cb;
   t->user_data = user_data;

   task_queue_ctl(TASK_QUEUE_CTL_PUSH, t);
#endif
   return true;
}

void rarch_task_image_load_free(retro_task_t *task)
{
   nbio_handle_t       *nbio  = (nbio_handle_t*)task->state;
   nbio_image_handle_t *image = nbio ? &nbio->image : NULL;

#ifdef HAVE_RPNG
   /* TODO/FIXME - add JPEG equivalents as well */
   rpng_nbio_load_image_free(image->handle);
#endif

   image->handle                 = NULL;
   image->frame_count            = 0;
}
