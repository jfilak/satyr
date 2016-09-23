/*
    js_stacktrace.c

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

#ifndef SR_INTERNAL_JS_PLATFORM_H
#define SR_INTERNAL_JS_PLATFORM_H

struct sr_location;
struct sr_js_frame;
struct sr_js_stacktrace;

struct sr_js_frame *
js_platform_parse_frame_v8(const char **input, struct sr_location *location);

struct sr_js_stacktrace *
js_platform_parse_stacktrace_v8(const char **input, struct sr_location *location);

#endif /* SR_INTERNAL_JS_PLATFORM_H */
