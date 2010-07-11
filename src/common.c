/*
 * Copyright (C) 2010 Xavier Claessens <xclaesse@gmail.com>
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
  guint ref_count;
  GIOStream *stream1;
  GIOStream *stream2;

  _GIOStreamSpliceCallback callback;
  gpointer user_data;
} SpliceContext;

static SpliceContext *
splice_context_ref (SpliceContext *self)
{
  self->ref_count++;
  return self;
}

static void
splice_context_unref (SpliceContext *self)
{
  if (--self->ref_count == 0)
    {
      if (self->callback != NULL)
        self->callback (self->user_data);

      g_object_unref (self->stream1);
      g_object_unref (self->stream2);

      g_slice_free (SpliceContext, self);
    }
}

static void
splice_context_close (SpliceContext *self)
{
  g_io_stream_close (self->stream1, NULL, NULL);
  g_io_stream_close (self->stream2, NULL, NULL);
}

static void
splice_cb (GObject *ostream,
    GAsyncResult *res,
    gpointer user_data)
{
  SpliceContext *ctx = user_data;

  splice_context_close (ctx);
  splice_context_unref (ctx);
}

void
_g_io_stream_splice (GIOStream *stream1,
    GIOStream *stream2,
    _GIOStreamSpliceCallback callback,
    gpointer user_data)
{
  SpliceContext *ctx;
  GInputStream *istream;
  GOutputStream *ostream;

  ctx = g_slice_new0 (SpliceContext);
  ctx->ref_count = 1;
  ctx->stream1 = g_object_ref (stream1);
  ctx->stream2 = g_object_ref (stream2);
  ctx->callback = callback;
  ctx->user_data = user_data;

  istream = g_io_stream_get_input_stream (stream1);
  ostream = g_io_stream_get_output_stream (stream2);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, NULL, splice_cb, splice_context_ref (ctx));
  
  istream = g_io_stream_get_input_stream (stream2);
  ostream = g_io_stream_get_output_stream (stream1);
  g_output_stream_splice_async (ostream, istream, G_OUTPUT_STREAM_SPLICE_NONE,
      G_PRIORITY_DEFAULT, NULL, splice_cb, splice_context_ref (ctx));

  splice_context_unref (ctx);
}

