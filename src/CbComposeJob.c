/*  This file is part of Cawbird, a Gtk+ linux Twitter client forked from Corebird.
 *  Copyright (C) 2017 Timm Bäder (Corebird)
 *
 *  Cawbird is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Cawbird is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with cawbird.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "cawbird.h"
#include "CbComposeJob.h"
#include "CbUtils.h"

G_DEFINE_TYPE (CbComposeJob, cb_compose_job, G_TYPE_OBJECT);

static void do_send (CbComposeJob *self);


enum {
  IMAGE_UPLOAD_PROGRESS,
  IMAGE_UPLOAD_FINISHED,
  IMAGE_UPLOAD_ID_ASSIGNED,
  LAST_SIGNAL
};
static guint compose_job_signals[LAST_SIGNAL] = { 0 };

static void
image_upload_free (ImageUpload *u)
{
  g_clear_pointer (&u->filename, g_free);
  g_clear_pointer (&u->uuid, g_free);
  g_clear_object (&u->cancellable);
}

static char *
build_image_id_string (CbComposeJob *self)
{
  const ImageUpload *uploads[MAX_UPLOADS];
  guint n_uploads = 0;
  guint n_unfinished_uploads = 0;
  GString *str;
  guint i;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      const ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename == NULL)
        continue;

      uploads[n_uploads] = upload;
      n_uploads ++;

      if (upload->id == 0)
        n_unfinished_uploads ++;
    }

  g_assert (n_unfinished_uploads == 0);
  g_assert (n_uploads <= 4);

  if (n_uploads == 0)
    return NULL;

  str = g_string_new (NULL);

  /* n_uploads is at least 1 at this point */
  g_string_append_printf (str, "%" G_GINT64_FORMAT, uploads[0]->id);

  for (i = 1; i < n_uploads; i ++)
    {
      g_assert (uploads[i]->id != 0);

      g_string_append_printf (str, ",%" G_GINT64_FORMAT, uploads[i]->id);
    }

  return g_string_free (str, FALSE);
}

static void
cancelled_cb (GCancellable *cancellable,
              gpointer      user_data)
{
  CbComposeJob *self = user_data;
  guint i;

  /* Abort mission */
  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      const ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename != NULL &&
          upload->id == 0)
        {
          g_cancellable_cancel (upload->cancellable);
        }
    }
}

guint
cb_compose_job_get_n_filepaths (CbComposeJob *self)
{
  guint i;
  guint n = 0;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      const ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename != NULL)
        n ++;
    }

  return n;
}

//FIXME: This is inefficient, but C is awkward and won't deal with arrays of strings nicely
const char*
cb_compose_job_get_filepath (CbComposeJob *self, guint pos)
{
  guint i;
  guint n = 0;
  char* filename = NULL;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      const ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename != NULL)
        {
          if (n == pos)
            {
              filename = upload->filename;
              break;
            }

          n ++;
        }
    }

  return filename;
}

static guint
cb_compose_job_get_n_unfinished_uploads (CbComposeJob *self)
{
  guint i;
  guint n = 0;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      const ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename != NULL &&
          upload->id == 0)
        n ++;
    }

  return n;
}

static void
cb_compose_job_finalize (GObject *object)
{
  CbComposeJob *self = CB_COMPOSE_JOB (object);
  guint i;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      ImageUpload *upload = &self->image_uploads[i];

      if (upload->filename == NULL)
        continue;

      image_upload_free (upload);
    }

  g_clear_object (&self->user_stream);
  g_clear_object (&self->account_proxy);
  g_clear_object (&self->upload_proxy);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->send_call);

  g_free (self->text);

  G_OBJECT_CLASS (cb_compose_job_parent_class)->finalize (object);
}

static void
cb_compose_job_class_init (CbComposeJobClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->finalize = cb_compose_job_finalize;

  compose_job_signals[IMAGE_UPLOAD_PROGRESS] = g_signal_new ("image-upload-progress",
                                                             G_OBJECT_CLASS_TYPE (gobject_class),
                                                             G_SIGNAL_RUN_FIRST,
                                                             0,
                                                             NULL, NULL,
                                                             NULL, G_TYPE_NONE,
                                                             2, G_TYPE_STRING, G_TYPE_DOUBLE);

  compose_job_signals[IMAGE_UPLOAD_FINISHED] = g_signal_new ("image-upload-finished",
                                                             G_OBJECT_CLASS_TYPE (gobject_class),
                                                             G_SIGNAL_RUN_FIRST,
                                                             0,
                                                             NULL, NULL,
                                                             NULL, G_TYPE_NONE,
                                                             2, G_TYPE_STRING, G_TYPE_STRING);

  compose_job_signals[IMAGE_UPLOAD_ID_ASSIGNED] = g_signal_new ("image-upload-id-assigned",
                                                             G_OBJECT_CLASS_TYPE (gobject_class),
                                                             G_SIGNAL_RUN_FIRST,
                                                             0,
                                                             NULL, NULL,
                                                             NULL, G_TYPE_NONE,
                                                             2, G_TYPE_STRING, G_TYPE_INT64);
}

static void
cb_compose_job_init (CbComposeJob *self)
{
}

CbComposeJob *
cb_compose_job_new (CbUserStream *user_stream,
                    RestProxy    *account_proxy,
                    RestProxy    *upload_proxy,
                    GCancellable *cancellable)
{
  CbComposeJob *self = CB_COMPOSE_JOB (g_object_new (CB_TYPE_COMPOSE_JOB, NULL));

  g_set_object (&self->user_stream, user_stream);
  g_set_object (&self->account_proxy, account_proxy);
  g_set_object (&self->upload_proxy, upload_proxy);
  g_set_object (&self->cancellable, cancellable);

  g_signal_connect (cancellable, "cancelled", G_CALLBACK (cancelled_cb), self);

  return self;
}

static void
image_upload_cb (RestProxyCall *call,
                 gsize          total,
                 gsize          uploaded,
                 const GError  *error,
                 GObject       *weak_object,
                 gpointer       user_data)

{
  CbComposeJob *self = CB_COMPOSE_JOB (weak_object);
  ImageUpload *upload = user_data;
  double percent = (double)uploaded / (double)total;

  if (error != NULL)
    {
      /* We also get here when aborting, in which case @upload is already garbage */
      if (error->code != REST_PROXY_ERROR_CANCELLED &&
          upload->uuid != NULL)
        g_signal_emit (self, compose_job_signals[IMAGE_UPLOAD_FINISHED],
                       0,
                       upload->uuid, error->message);
      return;
    }

  if (g_cancellable_is_cancelled (upload->cancellable))
    {
      /* Note that this case can happen more than once. */
      rest_proxy_call_cancel (call);
      g_object_unref (upload->cancellable);
      return;
    }

  g_signal_emit (self, compose_job_signals[IMAGE_UPLOAD_PROGRESS], 0, upload->uuid, percent);

  if (uploaded == total)
    {
      JsonParser *parser = json_parser_new ();
      JsonObject *root_object;
      GError *json_error = NULL;
      const char *error_message = NULL;

      json_parser_load_from_data (parser, rest_proxy_call_get_payload (call), -1, &json_error);

      if (json_error)
        {
          g_warning ("Could not upload %s: %s", upload->filename, json_error->message);
          error_message = json_error->message;
        }
      else
        {
          root_object = json_node_get_object (json_parser_get_root (parser));
          upload->id = json_object_get_int_member (root_object, "media_id");
        }

      g_debug ("%s ID: %" G_GINT64_FORMAT, upload->filename, upload->id);

      g_signal_emit (self, compose_job_signals[IMAGE_UPLOAD_FINISHED], 0, upload->uuid, error_message);
      g_signal_emit (self, compose_job_signals[IMAGE_UPLOAD_ID_ASSIGNED], 0, upload->uuid, upload->id);

      g_object_unref (parser);

      /* Now, check whether send() has already been called and if so, finish that one */
      if (self->send_task != NULL)
        {
          g_assert (self->send_call != NULL);

          if (cb_compose_job_get_n_unfinished_uploads (self) == 0)
            {
              /* Obviously, this was the last image to be uploaded before we could start
               * sending the actual tweet, so do that now... */
              do_send (self);
            }
        }
    }
}

void
cb_compose_job_upload_image_async (CbComposeJob *self,
                                   const char   *image_path,
                                   const char   *uuid)
{
  ImageUpload *upload = NULL;
  RestProxyCall *call;
  RestParam *param;
  GFile *file;
  char *contents;
  gsize contents_length;
  guint i;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      ImageUpload *u = &self->image_uploads[i];

      if (u->filename == NULL)
        {
          upload = u;
          break;
        }
    }

  g_assert (upload != NULL);

  upload->filename = g_strdup (image_path);
  upload->uuid = g_strdup(uuid);
  upload->cancellable = g_cancellable_new ();

  file = g_file_new_for_path (image_path);
  /* TODO: Error checking? */
  g_file_load_contents (file, NULL, &contents, &contents_length, NULL, NULL);
  g_object_unref (file);

  call = rest_proxy_new_call (self->upload_proxy);
  rest_proxy_call_set_function (call, "1.1/media/upload.json");
  rest_proxy_call_set_method (call, "POST");
  param = rest_param_new_full ("media",
                               REST_MEMORY_TAKE,
                               contents, contents_length,
                               "multipart/form-data",
                               image_path);

  rest_proxy_call_add_param_full (call, param);

#ifdef DEBUG
  {
    char *s = cb_utils_rest_proxy_call_to_string (call);

    g_debug ("%s: %s", G_STRLOC, s);
    g_free (s);
  }
#endif
  rest_proxy_call_upload (call,
                          image_upload_cb,
                          G_OBJECT (self),
                          upload,
                          NULL);
  g_object_unref (call);
}

void
cb_compose_job_abort_image_upload (CbComposeJob *self,
                                   const char   *uuid)
{
  guint i;

  for (i = 0; i < MAX_UPLOADS; i ++)
    {
      ImageUpload *upload = &self->image_uploads[i];

      if (upload->uuid != NULL &&
          strcmp (upload->uuid, uuid) == 0)
        {
          g_cancellable_cancel (upload->cancellable);
          image_upload_free (upload);
          break;
        }
    }
}

void
cb_compose_job_set_reply_id (CbComposeJob *self,
                             gint64        reply_id)
{
  self->reply_id = reply_id;
}

void
cb_compose_job_set_quoted_tweet (CbComposeJob *self,
                                 CbTweet      *quoted_tweet)
{
  g_set_object (&self->quoted_tweet, quoted_tweet);
}

void
cb_compose_job_set_text (CbComposeJob *self,
                         const char   *text)
{
  self->text = g_strdup (text);
}

static void
send_tweet_call_completed_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  RestProxyCall *call = REST_PROXY_CALL (source_object);
  CbComposeJob *self = CB_COMPOSE_JOB (user_data);
  GTask *send_task = self->send_task;
  GError *error = NULL;

  rest_proxy_call_invoke_finish (call, result, &error);
  self->response_payload = rest_proxy_call_get_payload (call);

  if (error)
    {
      g_warning ("Could not send tweet: %s", error->message);
      g_task_return_error (send_task, tweet_utils_failed_request_to_error (call, error));
    }
  else
    {
      //FIXME: Can we inject missing image descriptions here? They don't get added in v1 API
      // Hopefully they're added in the new v2 API.
      cb_user_stream_inject_tweet (self->user_stream, CB_STREAM_MESSAGE_TWEET, self->response_payload);
      g_task_return_boolean (send_task, TRUE);
    }

  self->send_task = NULL;
  g_object_unref (send_task);
}

static void
do_send (CbComposeJob *self)
{
  char *media_ids = build_image_id_string (self);

  g_assert (cb_compose_job_get_n_unfinished_uploads (self) == 0);
  g_assert (self->send_call != NULL);
  g_assert (self->send_task != NULL);

  if (media_ids) {
    rest_proxy_call_add_param (self->send_call, "media_ids", media_ids);
    g_free(media_ids);
  }

#ifdef DEBUG
  {
    char *s = cb_utils_rest_proxy_call_to_string (self->send_call);

    g_debug ("%s: %s", G_STRLOC, s);
    g_free (s);
  }
#endif

  rest_proxy_call_invoke_async (self->send_call,
                                self->cancellable,
                                send_tweet_call_completed_cb,
                                self);
}

void
cb_compose_job_send_async (CbComposeJob        *self,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;
  RestProxyCall *call;

  g_assert (self->send_task == NULL);

  task = g_task_new (self, cancellable, callback, user_data);

  call = rest_proxy_new_call (self->account_proxy);
  rest_proxy_call_set_function (call, "1.1/statuses/update.json");
  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_param (call, "auto_populate_reply_metadata", "true");
  rest_proxy_call_add_param (call, "tweet_mode", "extended");
  rest_proxy_call_add_param (call, "include_ext_alt_text", "true");

  if (self->reply_id != 0)
    {
      char *id_str = g_strdup_printf ("%" G_GINT64_FORMAT, self->reply_id);

      g_assert (self->quoted_tweet == NULL);

      rest_proxy_call_add_param (call, "in_reply_to_status_id", id_str);
      g_free(id_str);
    }
  else if (self->quoted_tweet != NULL)
    {
      const CbMiniTweet *mt = self->quoted_tweet->retweeted_tweet != NULL ?
                              self->quoted_tweet->retweeted_tweet :
                              &self->quoted_tweet->source_tweet;
      char *quoted_url = g_strdup_printf ("https://twitter.com/%s/status/%" G_GINT64_FORMAT,
                                          mt->author.screen_name, mt->id);

      g_assert (self->reply_id == 0);

      guint i;
      guint upload_count = 0;

      for (i = 0; i < MAX_UPLOADS; i++)
        {
          const ImageUpload *upload = &self->image_uploads[i];

          if (upload->filename == NULL)
            {
              break;
            }

          upload_count ++;
        }

      if (upload_count == 0)
        {
          rest_proxy_call_add_param (call, "attachment_url", quoted_url);
        }
      else
        {
          char *new_text = g_strconcat (self->text, " ", quoted_url, (char *)0);
          g_free(self->text);
          self->text = new_text;
        }
        g_free(quoted_url);
    }

  rest_proxy_call_add_param (call, "status", self->text);


  self->send_call = call;
  self->send_task = task;
  if (cb_compose_job_get_n_unfinished_uploads (self) > 0)
    {
      /* In this case, we need to wait until ALL uploads are complete and successful. */
    }
  else
    {
      do_send (self);
    }
}

gboolean
cb_compose_job_send_finish (CbComposeJob  *self,
                            GAsyncResult  *result,
                            GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}
