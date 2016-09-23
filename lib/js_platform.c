/*
    js_platfrom.c

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

#include "js/platform.h"
#include "js/stacktrace.h"
#include "js/frame.h"
#include "location.h"

#include "utils.h"
#include "internal_utils.h"
#include "json.h"

#include <string.h>


#define RETURN_ON_INVALID_ENGINE(engine, retval) \
    do { if (!SR_JS_ENGINE_VALIDITY_CHECK(engine)) { \
            warn("Invalid JavaScript engine code %0x", engine); \
            return NULL; \
    } } while (0)

#define RETURN_ON_INVALID_RUNTIME(runtime, retval) \
    do { if (!SR_JS_RUNTIME_VALIDITY_CHECK(runtime)) { \
            warn("Invalid JavaScript runtime code %0x", runtime); \
            return NULL; \
    } } while (0)


typedef struct sr_js_frame *(js_runtime_frame_parser_t)(enum sr_js_engine,
                                                        const char **input,
                                                        struct sr_location *location);

typedef struct sr_js_frame *(js_engine_frame_parser_t)(const char **input,
                                                       struct sr_location *location);

typedef struct sr_js_stacktrace *(js_runtime_stacktrace_parser_t)(enum sr_js_engine,
                                                                  const char **input,
                                                                  struct sr_location *location);

typedef struct sr_js_stacktrace *(js_engine_stacktrace_parser_t)(const char **input,
                                                                 struct sr_location *location);


struct js_engine_meta
{
    const char *name;
    js_engine_frame_parser_t *parse_frame;
    js_engine_stacktrace_parser_t *parse_stacktrace;
}
js_engines[_SR_JS_ENGINE_UPPER_BOUND] =
{
    [SR_JS_ENGINE_V8] =
    {
        .name = "V8",
        .parse_frame = &js_platform_parse_frame_v8,
        .parse_stacktrace = &js_platform_parse_stacktrace_v8,
    },
};

struct js_runtime_meta
{
    const char *name;
    js_runtime_frame_parser_t *parse_frame;
    js_runtime_stacktrace_parser_t *parse_stacktrace;
}
js_runtimes[_SR_JS_RUNTIME_UPPER_BOUND] =
{
    [SR_JS_RUNTIME_NODEJS] =
    {
        .name = "Node.js",
        .parse_frame = NULL,
        .parse_stacktrace = NULL,
    },
};


sr_js_platform_t
sr_js_platform_from_string(const char *runtime_name,
                           const char *runtime_version,
                           char **error_message)
{
    enum sr_js_runtime runtime = sr_js_runtime_from_string(runtime_name);
    if (!runtime)
    {
        *error_message = sr_asprintf("No known JavaScript platform with runtime "
                                    "'%s'", runtime_name);

        return SR_JS_PLATFORM_NULL;
    }

    enum sr_js_engine engine = 0;

    switch (runtime)
    {
        case SR_JS_RUNTIME_NODEJS:
            engine = SR_JS_ENGINE_V8;
            break;

        default:
            /* pass - calm down the compiler */
            break;
    }

    if (!engine)
    {
        *error_message = sr_asprintf("No known JavaScript engine for runtime"
                                    "by '%s%s%s'",
                                    runtime_name,
                                    runtime_version ? " " : "",
                                    runtime_version ? runtime_version : "");

        return SR_JS_PLATFORM_NULL;
    }

    return _sr_js_platform_assemble(runtime, engine);
}

const char *
sr_js_engine_to_string(enum sr_js_engine engine)
{
    RETURN_ON_INVALID_ENGINE(engine, NULL);

    return js_engines[engine].name;
}

enum sr_js_engine
sr_js_engine_from_string(const char *engine_str)
{
    unsigned engine = 1;
    for (; engine < _SR_JS_ENGINE_UPPER_BOUND; ++engine)
        if (strcmp(engine_str, js_engines[engine].name) == 0)
            return engine;

    return 0;
}

const char *
sr_js_runtime_to_string(enum sr_js_runtime runtime)
{
    RETURN_ON_INVALID_RUNTIME(runtime, NULL);

    return js_runtimes[runtime].name;
}

enum sr_js_runtime
sr_js_runtime_from_string(const char *runtime_str)
{
    unsigned runtime = 1;
    for (; runtime < _SR_JS_RUNTIME_UPPER_BOUND; ++runtime)
        if (strcmp(runtime_str, js_runtimes[runtime].name) == 0)
            return runtime;

    return 0;
}

char *
sr_js_platform_to_json(sr_js_platform_t platform)
{
    const char *runtime_str = sr_js_runtime_to_string(sr_js_platform_runtime(platform));
    const char *engine_str = sr_js_engine_to_string(sr_js_platform_engine(platform));

    if (!runtime_str)
        runtime_str = "<unknown>";

    if (!engine_str)
        engine_str = "<unknown>";

   return sr_asprintf("{      \"engine\": \"%s\"\n"
                      ",      \"runtime\": \"%s\"\n"
                      "}",
                      engine_str,
                      runtime_str);
}

sr_js_platform_t
sr_js_platform_from_json(struct sr_json_value *root, char **error_message)
{
    sr_js_platform_t platform = SR_JS_PLATFORM_NULL;

    char *engine_str = NULL;
    if (!JSON_READ_STRING(root, "engine", &engine_str))
        goto fail;

    if (engine_str == NULL)
    {
        *error_message = sr_strdup("No 'engine' member");
        goto fail;
    }

    enum sr_js_engine engine= sr_js_engine_from_string(engine_str);
    if (!engine)
    {
        *error_message =sr_asprintf("Unknown JavaScript engine '%s'", engine_str);
        goto fail;
    }

    char *runtime_str = NULL;
    if (!JSON_READ_STRING(root, "runtime", &runtime_str))
        goto fail;

    if (runtime_str == NULL)
    {
        *error_message = sr_strdup("No 'runtime' member");
        goto fail;
    }

    enum sr_js_runtime runtime = sr_js_runtime_from_string(runtime_str);
    if (!runtime)
    {
        *error_message =sr_asprintf("Unknown JavaScript runtime '%s'", runtime_str);
        goto fail;
    }

    platform = sr_js_platform_new();
    sr_js_platform_init(platform, engine, runtime);

fail:
    free(engine_str);
    free(runtime_str);
    return platform;
}

struct sr_js_frame *
js_platform_parse_frame_v8(const char **input,
                           struct sr_location *location)
{
    int lines, columns;

    /*      at Object.<anonymous> ([stdin]-wrapper:6:22)
     * ^^^^^
     *
     * OR
     *
     *      at bootstrap_node.js:357:29
     * ^^^^^
     */
    const char *local_input = sr_strstr_location(*input, "at ", &lines, &columns);
    if (!local_input)
    {
        location->message = "Expected frame beginning.";
        return NULL;
    }

    /* at Object.<anonymous> ([stdin]-wrapper:6:22)
     * ^^^
     *
     * OR
     *
     * at bootstrap_node.js:357:29
     * ^^^
     */
    local_input += 3;
    sr_location_add(location, lines, columns + 3);

    /* Object.<anonymous> ([stdin]-wrapper:6:22)
     * -----------------------------------------^
     *
     * OR
     *
     * bootstrap_node.js:357:29
     * ------------------------^
     */
    const char *cursor = local_input;
    sr_skip_char_cspan(&cursor, "\n");
    struct sr_js_frame *frame = sr_js_frame_new();

    /* Let's hope file names containing new lines are not very common. For
     * example Node.js fails to execute such a file.
     */
    if (cursor[-1] == ')')
    {
        /* Object.<anonymous> ([stdin]-wrapper:6:22)
         * -------------------^
         */
        const char *name_end = strchr(local_input, '(');
        if (name_end == NULL)
        {
            location->message = "Opening brace with file information not found.";
            goto fail;
        }

        sr_location_add(location, 0, (name_end - local_input) + 1);

        /* Object.<anonymous> ([stdin]-wrapper:6:22)
         *                  ^--
         */
        while (local_input > name_end && *name_end == ' ')
            --name_end;

        /* Object.<anonymous>
         * ^^^^^^^^^^^^^^^^^^
         */
        frame->function_name = sr_strndup(local_input, name_end - local_input);

        /* ([stdin]-wrapper:6:22)
         * -^
         */
        ++name_end;

        sr_location_add(location, 0, name_end - local_input);
        local_input = name_end;

        /* ([stdin]-wrapper:6:22)
         *                     ^--
         */
         cursor -= 2;
    }
    else /* if (*cursor == '\n' || *cursor == '\0') */
    {
        /* bootstrap_node.js:357:29
         *                        ^-
         */
        --cursor;
    }

    /* Beware of file name containing colon, bracket or white space.
     * That's the reason why parse these information backwards.
     *
     *
     * bootstrap_node.js:357:29
     *                      ^--
     */
    const char *token = cursor;
    while (token > local_input && *token != ':')
        --token;

    if (token == local_input)
    {
        location->message = "Unable to locate line column.";
        goto fail;
    }

    /* bootstrap_node.js:357:29
     *                       ^^
     */
    cursor = token + 1;
    if (!sr_parse_uint32(&cursor, &(frame->line_column)))
    {
        sr_location_add(location, 0, cursor - local_input);
        location->message = "Failed to parse line column.";
        goto fail;
    }

    /* bootstrap_node.js:357:29
     *                  ^----
     */
    token -= 1;
    while (token > local_input && *token != ':')
        --token;

    if (token == local_input)
    {
        location->message = "Unable to locate file line.";
        goto fail;
    }

    /* bootstrap_node.js:357:29
     *                   ^^^
     */
    cursor = token + 1;
    if (!sr_parse_uint32(&cursor, &(frame->line_column)))
    {
        sr_location_add(location, 0, cursor - local_input);
        location->message = "Failed to parse file line.";
        goto fail;
    }

    /* bootstrap_node.js:357:29
     * ^^^^^^^^^^^^^^^^^
     */
    frame->function_name = sr_strndup(local_input, token - local_input);

    location->column += sr_skip_char_cspan(&local_input, "\n");

    *input = local_input;
    return frame;

fail:
    sr_js_frame_free(frame);
    return NULL;
}

struct sr_js_frame *
sr_js_platform_parse_frame(sr_js_platform_t platform, const char **input,
                           struct sr_location *location)
{
    enum sr_js_runtime runtime = sr_js_platform_runtime(platform);
    RETURN_ON_INVALID_RUNTIME(runtime, NULL);

    enum sr_js_engine engine = sr_js_platform_engine(platform);
    RETURN_ON_INVALID_ENGINE(engine, NULL);

    struct sr_js_frame *frame = NULL;

    if (js_runtimes[runtime].parse_frame)
        frame = js_runtimes[runtime].parse_frame(engine, input, location);
    else
        frame = js_engines[engine].parse_frame(input, location);

    return frame;
}

struct sr_js_stacktrace *
js_platform_parse_stacktrace_v8(const char **input,
                                struct sr_location *location)
{
    const char *local_input = *input;
    struct sr_js_stacktrace *stacktrace = sr_js_stacktrace_new();

    /*
     * ReferenceError: nonexistentFunc is not defined
     * ^^^^^^^^^^^^^^
     */
    if (!sr_parse_char_cspan(&local_input, ":", &stacktrace->exception_name))
    {
        location->message = "Unable to find the colon right behind exception type.";
        goto fail;
    }

    {
        const size_t name_len = strlen(stacktrace->exception_name);
        if (name_len == 0)
        {
            location->message = "Zero length exception type.";
            goto fail;
        }

        location->column += name_len;
    }

    /*
     * ReferenceError: nonexistentFunc is not defined
     *               ^^
     */
    if (!sr_skip_string(&local_input, ": "))
    {
        location->message = "Unable to find the colon after first exception type.";
        goto fail;
    }

    /*
     * ReferenceError: nonexistentFunc is not defined
     *                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
     *
     * Ignore error message because it may contain security sensitive data.
     */
    if (!sr_skip_to_next_line_location(&local_input, &location->line, &location->column))
    {
        location->message = "Stack trace does not include any frames.";
        goto fail;
    }

    struct sr_js_frame *last_frame = NULL;
    while (*local_input != '\0')
    {
        struct sr_js_frame *current_frame = js_platform_parse_frame_v8(&local_input,
                                                                       location);
        if (current_frame == NULL)
            goto fail;

        if (stacktrace->frames == NULL)
            stacktrace->frames = current_frame;
        else
            last_frame->next = current_frame;

        /* Eat newline (except at the end of file). */
        if (!sr_skip_char(&local_input, '\n') && *local_input != '\0')
        {
            location->message = "Expected newline after stacktrace frame.";
            goto fail;
        }

        location->column = 0;
        location->line++;

        last_frame = current_frame;
    }

    *input = local_input;
    return stacktrace;

fail:
    sr_js_stacktrace_free(stacktrace);
    return NULL;
}

struct sr_js_stacktrace *
sr_js_platform_parse_stacktrace(sr_js_platform_t platform, const char **input,
                                struct sr_location *location)
{
    enum sr_js_runtime runtime = sr_js_platform_runtime(platform);
    RETURN_ON_INVALID_RUNTIME(runtime, NULL);

    enum sr_js_engine engine = sr_js_platform_engine(platform);
    RETURN_ON_INVALID_ENGINE(engine, NULL);

    struct sr_js_stacktrace *stacktrace = NULL;

    if (js_runtimes[runtime].parse_stacktrace != NULL)
        stacktrace = js_runtimes[runtime].parse_stacktrace(engine, input, location);
    else
        stacktrace = js_engines[engine].parse_stacktrace(input, location);

    if (stacktrace != NULL)
    {
        stacktrace->platform = sr_js_platform_new();
        stacktrace->platform = sr_js_platform_dup(platform);
    }

    return stacktrace;
}
