/*
    js_frame.c

    Copyright (C) 2016  Red Hat, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include "internal_js_platform.h"
#include "js/frame.h"
#include "utils.h"
#include "location.h"
#include "strbuf.h"
#include "json.h"
#include "generic_frame.h"
#include "thread.h"
#include "stacktrace.h"
#include "internal_utils.h"
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

/* Method table */

static void
js_append_bthash_text(struct sr_js_frame *frame, enum sr_bthash_flags flags,
                      struct sr_strbuf *strbuf);
static void
js_append_duphash_text(struct sr_js_frame *frame, enum sr_duphash_flags flags,
                       struct sr_strbuf *strbuf);

DEFINE_NEXT_FUNC(js_next, struct sr_frame, struct sr_js_frame)
DEFINE_SET_NEXT_FUNC(js_set_next, struct sr_frame, struct sr_js_frame)

struct frame_methods js_frame_methods =
{
    .append_to_str = (append_to_str_fn_t) sr_js_frame_append_to_str,
    .next = (next_frame_fn_t) js_next,
    .set_next = (set_next_frame_fn_t) js_set_next,
    .cmp = (frame_cmp_fn_t) sr_js_frame_cmp,
    .cmp_distance = (frame_cmp_fn_t) sr_js_frame_cmp_distance,
    .frame_append_bthash_text =
        (frame_append_bthash_text_fn_t) js_append_bthash_text,
    .frame_append_duphash_text =
        (frame_append_duphash_text_fn_t) js_append_duphash_text,
    .frame_free = (frame_free_fn_t) sr_js_frame_free,
};

/* Public functions */

struct sr_js_frame *
sr_js_frame_new()
{
    struct sr_js_frame *frame =
        sr_malloc(sizeof(struct sr_js_frame));

    sr_js_frame_init(frame);
    return frame;
}

void
sr_js_frame_init(struct sr_js_frame *frame)
{
    memset(frame, 0, sizeof(struct sr_js_frame));
}

void
sr_js_frame_free(struct sr_js_frame *frame)
{
    if (!frame)
        return;

    free(frame->file_name);
    free(frame->function_name);
    free(frame);
}

struct sr_js_frame *
sr_js_frame_dup(struct sr_js_frame *frame, bool siblings)
{
    struct sr_js_frame *result = sr_js_frame_new();
    memcpy(result, frame, sizeof(struct sr_js_frame));

    /* Handle siblings. */
    if (siblings)
    {
        if (result->next)
            result->next = sr_js_frame_dup(result->next, true);
    }
    else
        result->next = NULL; /* Do not copy that. */

    /* Duplicate all strings. */
    if (result->file_name)
        result->file_name = sr_strdup(result->file_name);

    if (result->function_name)
        result->function_name = sr_strdup(result->function_name);

    return result;
}

int
sr_js_frame_cmp(struct sr_js_frame *frame1,
                struct sr_js_frame *frame2)
{
    /* function_name */
    int function_name = sr_strcmp0(frame1->function_name,
                                   frame2->function_name);
    if (function_name != 0)
        return function_name;

    /* file_name */
    int file_name = sr_strcmp0(frame1->file_name,
                               frame2->file_name);
    if (file_name != 0)
        return file_name;

    /* file_line */
    int file_line = frame1->file_line - frame2->file_line;
    if (file_line != 0)
        return file_line;

    /* line_column */
    int line_column = frame1->line_column - frame2->line_column;
    if (line_column != 0)
        return line_column;

    return 0;
}

int
sr_js_frame_cmp_distance(struct sr_js_frame *frame1,
                         struct sr_js_frame *frame2)
{
    /* file_line */
    int file_line = frame1->file_line - frame2->file_line;
    if (file_line != 0)
        return file_line;

    /* function_name */
    int function_name = sr_strcmp0(frame1->function_name,
                                   frame2->function_name);
    if (function_name != 0)
        return function_name;

    /* file_name */
    int file_name = sr_strcmp0(frame1->file_name,
                               frame2->file_name);
    if (file_name != 0)
        return file_name;

    return 0;
}

struct sr_js_frame *
sr_js_frame_append(struct sr_js_frame *dest,
                   struct sr_js_frame *item)
{
    if (!dest)
        return item;

    struct sr_js_frame *dest_loop = dest;
    while (dest_loop->next)
        dest_loop = dest_loop->next;

    dest_loop->next = item;
    return dest;
}

struct sr_js_frame *
sr_js_frame_parse(const char **input,
                  struct sr_location *location)
{
    struct sr_js_frame *frame = NULL;

    frame = js_platform_parse_frame_v8(input, location);
    if (frame == NULL)
        location->message = "The frame does not match any JavaScript dialect";

    return frame;
}

struct sr_js_frame *
sr_js_frame_from_json(struct sr_json_value *root, char **error_message)
{
    if (!JSON_CHECK_TYPE(root, SR_JSON_OBJECT, "frame"))
        return NULL;

    struct sr_js_frame *result = sr_js_frame_new();
    struct sr_json_value *val;

    /* Source file name */
    if ((val = json_element(root, "file_name")))
    {
        if (!JSON_CHECK_TYPE(val, SR_JSON_STRING, "file_name"))
            goto fail;

        result->file_name = sr_strdup(val->u.string.ptr);
    }

    /* Function name. */
    if ((val = json_element(root, "function_name")))
    {
        if (!JSON_CHECK_TYPE(val, SR_JSON_STRING, "function_name"))
            goto fail;

        result->function_name = sr_strdup(val->u.string.ptr);
    }

    bool success =
        JSON_READ_UINT32(root, "file_line", &result->file_line) &&
        JSON_READ_UINT32(root, "line_column", &result->line_column);

    if (!success)
        goto fail;

    return result;

fail:
    sr_js_frame_free(result);
    return NULL;
}

char *
sr_js_frame_to_json(struct sr_js_frame *frame)
{
    struct sr_strbuf *strbuf = sr_strbuf_new();

    /* Source file name. */
    if (frame->file_name)
    {
        sr_strbuf_append_str(strbuf, ",   \"file_name\": ");
        sr_json_append_escaped(strbuf, frame->file_name);
        sr_strbuf_append_str(strbuf, "\n");
    }

    /* Source file line. */
    if (frame->file_line)
    {
        sr_strbuf_append_strf(strbuf,
                              ",   \"file_line\": %"PRIu32"\n",
                              frame->file_line);
    }

    /* Line column. */
    if (frame->line_column)
    {
        sr_strbuf_append_strf(strbuf,
                              ",   \"line_column\": %"PRIu32"\n",
                              frame->line_column);
    }

    strbuf->buf[0] = '{';
    sr_strbuf_append_char(strbuf, '}');
    return sr_strbuf_free_nobuf(strbuf);
}

void
sr_js_frame_append_to_str(struct sr_js_frame *frame,
                          struct sr_strbuf *dest)
{
    sr_strbuf_append_str(dest, "at ");
    if (frame->function_name)
        sr_strbuf_append_strf(dest, "%s (", frame->function_name);

    sr_strbuf_append_strf(dest, "%s:%"PRIu32":%"PRIu32,
                          frame->file_name,
                          frame->file_line,
                          frame->line_column);

    if (frame->function_name)
        sr_strbuf_append_str(dest, ")");
}

static void
js_append_bthash_text(struct sr_js_frame *frame, enum sr_bthash_flags flags,
                      struct sr_strbuf *strbuf)
{
    sr_strbuf_append_strf(strbuf,
                          "%s, %"PRIu32", %s\n",
                          OR_UNKNOWN(frame->file_name),
                          frame->file_line,
                          OR_UNKNOWN(frame->function_name));
}

static void
js_append_duphash_text(struct sr_js_frame *frame, enum sr_duphash_flags flags,
                       struct sr_strbuf *strbuf)
{
    /* filename:line */
    sr_strbuf_append_strf(strbuf, "%s:%"PRIu32"\n",
                          OR_UNKNOWN(frame->file_name),
                          frame->file_line);
}
