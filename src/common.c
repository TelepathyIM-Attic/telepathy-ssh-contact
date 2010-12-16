/*
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"

#include "common.h"

typedef struct
{
  GIOStream *stream1;
  GIOStream *stream2;
  _GIOStreamSpliceFlags flags;
  gint io_priority;
  GCancellable *cancellable;
  gulong cancelled_id;
  GCancellable *op1_cancellable;
  GCancellable *op2_cancellable;
  guint completed;
  GError *error;
} SpliceContext;

static void
splice_context_free (SpliceContext *ctx)
{
  g_object_unref (ctx->stream1);
  g_object_unref (ctx->stream2);
  if (ctx->cancellable != NULL)
    g_object_unref (ctx->cancellable);
  g_object_unref (ctx->op1_cancellable);
  g_object_unref (ctx->op2_cancellable);
  g_clear_error (&ctx->error);
  g_slice_free (SpliceContext, ctx);
}

static void
splice_complete (GSimpleAsyncResult *simple,
                 SpliceContext      *ctx)
{
  if (ctx->cancelled_id != 0)
    g_cancellable_disconnect (ctx->cancellable, ctx->cancelled_id);
  ctx->cancelled_id = 0;

  if (ctx->error != NULL)
    g_simple_async_result_set_from_error (simple, ctx->error);
  g_simple_async_result_complete (simple);
}

static void
splice_close_cb (GObject      *iostream,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  GSimpleAsyncResult *simple = user_data;
  SpliceContext *ctx;
  GError *error = NULL;

  g_io_stream_close_finish (G_IO_STREAM (iostream), res, &error);

  ctx = g_simple_async_result_get_op_res_gpointer (simple);
  ctx->completed++;

  /* Keep the first error that occured */
  if (error != NULL && ctx->error == NULL)
    ctx->error = error;
  else
    g_clear_error (&error);

  /* If all operations are done, complete now */
  if (ctx->completed == 4)
    splice_complete (simple, ctx);

  g_object_unref (simple);
}

static void
splice_cb (GObject      *ostream,
           GAsyncResult *res,
           gpointer      user_data)
{
  GSimpleAsyncResult *simple = user_data;
  SpliceContext *ctx;
  GError *error = NULL;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (ostream), res, &error);

  ctx = g_simple_async_result_get_op_res_gpointer (simple);
  ctx->completed++;

  /* ignore cancellation error if it was not requested by the user */
  if (error != NULL &&
      g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
      (ctx->cancellable == NULL ||
       !g_cancellable_is_cancelled (ctx->cancellable)))
    g_clear_error (&error);

  /* Keep the first error that occured */
  if (error != NULL && ctx->error == NULL)
    ctx->error = error;
  else
    g_clear_error (&error);

   if (ctx->completed == 1 &&
       (ctx->flags & _G_IO_STREAM_SPLICE_WAIT_FOR_BOTH) == 0)
    {
      /* We don't want to wait for the 2nd operation to finish, cancel it */
      g_cancellable_cancel (ctx->op1_cancellable);
      g_cancellable_cancel (ctx->op2_cancellable);
    }
  else if (ctx->completed == 2)
    {
      if (ctx->cancellable == NULL ||
          !g_cancellable_is_cancelled (ctx->cancellable))
        {
          g_cancellable_reset (ctx->op1_cancellable);
          g_cancellable_reset (ctx->op2_cancellable);
        }

      /* Close the IO streams if needed */
      if ((ctx->flags & _G_IO_STREAM_SPLICE_CLOSE_STREAM1) != 0)
        g_io_stream_close_async (ctx->stream1, ctx->io_priority,
            ctx->op1_cancellable, splice_close_cb, g_object_ref (simple));
      else
        ctx->completed++;

      if ((ctx->flags & _G_IO_STREAM_SPLICE_CLOSE_STREAM2) != 0)
        g_io_stream_close_async (ctx->stream2, ctx->io_priority,
            ctx->op2_cancellable, splice_close_cb, g_object_ref (simple));
      else
        ctx->completed++;

      /* If all operations are done, complete now */
      if (ctx->completed == 4)
        splice_complete (simple, ctx);
    }

  g_object_unref (simple);
}

static void
splice_cancelled_cb (GCancellable       *cancellable,
                     GSimpleAsyncResult *simple)
{
  SpliceContext *ctx;

  ctx = g_simple_async_result_get_op_res_gpointer (simple);
  g_cancellable_cancel (ctx->op1_cancellable);
  g_cancellable_cancel (ctx->op2_cancellable);
}

void
_g_io_stream_splice_async (GIOStream            *stream1,
                          GIOStream            *stream2,
                          _GIOStreamSpliceFlags  flags,
                          gint                  io_priority,
                          GCancellable         *cancellable,
                          GAsyncReadyCallback   callback,
                          gpointer              user_data)
{
  GSimpleAsyncResult *simple;
  SpliceContext *ctx;
  GInputStream *istream;
  GOutputStream *ostream;

  if (cancellable != NULL && g_cancellable_is_cancelled (cancellable))
    {
      g_simple_async_report_error_in_idle (NULL, callback,
          user_data, G_IO_ERROR, G_IO_ERROR_CANCELLED,
          "Operation has been cancelled");
      return;
    }

  ctx = g_slice_new0 (SpliceContext);
  ctx->stream1 = g_object_ref (stream1);
  ctx->stream2 = g_object_ref (stream2);
  ctx->flags = flags;
  ctx->io_priority = io_priority;
  ctx->op1_cancellable = g_cancellable_new ();
  ctx->op2_cancellable = g_cancellable_new ();
  ctx->completed = 0;

  simple = g_simple_async_result_new (NULL, callback, user_data,
      _g_io_stream_splice_finish);
  g_simple_async_result_set_op_res_gpointer (simple, ctx,
      (GDestroyNotify) splice_context_free);

  if (cancellable != NULL)
    {
      ctx->cancellable = g_object_ref (cancellable);
      ctx->cancelled_id = g_cancellable_connect (cancellable,
          G_CALLBACK (splice_cancelled_cb), g_object_ref (simple),
          g_object_unref);
    }

  istream = g_io_stream_get_input_stream (stream1);
  ostream = g_io_stream_get_output_stream (stream2);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      io_priority, ctx->op1_cancellable, splice_cb,
      g_object_ref (simple));

  istream = g_io_stream_get_input_stream (stream2);
  ostream = g_io_stream_get_output_stream (stream1);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      io_priority, ctx->op2_cancellable, splice_cb,
      g_object_ref (simple));

  g_object_unref (simple);
}

gboolean
_g_io_stream_splice_finish (GAsyncResult  *result,
                           GError       **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
      _g_io_stream_splice_finish), FALSE);

  return TRUE;
}
