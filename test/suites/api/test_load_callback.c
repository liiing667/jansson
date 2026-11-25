/*
 * Copyright (c) 2009-2011 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "util.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

struct my_source {
    const char *buf;
    size_t off;
    size_t cap;
    size_t chunk_size;
};

static const char my_str[] = "[\"A\", {\"B\": \"C\", \"e\": false}, 1, null, \"foo\"]";

static size_t greedy_reader(void *buf, size_t buflen, void *arg) {
    struct my_source *s = arg;
    if (buflen > s->cap - s->off)
        buflen = s->cap - s->off;
    if (buflen > 0) {
        memcpy(buf, s->buf + s->off, buflen);
        s->off += buflen;
        return buflen;
    } else {
        return 0;
    }
}

static size_t chunk_reader(void *buf, size_t buflen, void *arg) {
    struct my_source *s = arg;
    size_t len = s->chunk_size;

    if (len > buflen)
        len = buflen;
    if (len > s->cap - s->off)
        len = s->cap - s->off;

    memcpy(buf, s->buf + s->off, len);
    s->off += len;
    return len;
}

static void embedded_nul_is_not_eof() {
    static const char input[] = {'[', '1', '\0', ']'};
    const size_t len = sizeof(input);
    json_error_t buffer_error;
    json_error_t callback_error;
    json_t *buffer_json;
    json_t *callback_json;
    struct my_source source;
    size_t chunk_size;

    buffer_json = json_loadb(input, len, 0, &buffer_error);
    if (buffer_json) {
        json_decref(buffer_json);
        fail("json_loadb accepted an embedded NUL byte");
    }

    for (chunk_size = 1; chunk_size <= len; chunk_size++) {
        source.buf = input;
        source.off = 0;
        source.cap = len;
        source.chunk_size = chunk_size;

        callback_json = json_load_callback(chunk_reader, &source, 0, &callback_error);
        if (callback_json) {
            json_decref(callback_json);
            fail("json_load_callback treated an embedded NUL byte as EOF");
        }

        if (callback_error.position != buffer_error.position ||
            strcmp(callback_error.text, buffer_error.text) != 0) {
            fail("json_load_callback and json_loadb disagreed on an embedded "
                 "NUL byte");
        }
    }
}

static void chunked_reading_matches_buffer() {
    struct my_source source;
    json_error_t callback_error;
    json_t *callback_json;
    json_t *buffer_json;
    size_t chunk_size;

    buffer_json = json_loadb(my_str, strlen(my_str), 0, &callback_error);
    if (!buffer_json)
        fail("json_loadb failed on valid callback input");

    for (chunk_size = 1; chunk_size <= strlen(my_str); chunk_size++) {
        source.buf = my_str;
        source.off = 0;
        source.cap = strlen(my_str);
        source.chunk_size = chunk_size;

        callback_json = json_load_callback(chunk_reader, &source, 0, &callback_error);
        if (!callback_json)
            fail("json_load_callback failed on valid chunked input");
        if (!json_equal(callback_json, buffer_json))
            fail("json_load_callback returned different data from json_loadb");
        json_decref(callback_json);
    }

    json_decref(buffer_json);
}

static void run_tests() {
    struct my_source s;
    json_t *json;
    json_error_t error;

    s.off = 0;
    s.cap = strlen(my_str);
    s.buf = my_str;

    json = json_load_callback(greedy_reader, &s, 0, &error);

    if (!json)
        fail("json_load_callback failed on a valid callback");
    json_decref(json);

    s.off = 0;
    s.cap = strlen(my_str) - 1;
    s.buf = my_str;

    json = json_load_callback(greedy_reader, &s, 0, &error);
    if (json) {
        json_decref(json);
        fail("json_load_callback should have failed on an incomplete stream, "
             "but it didn't");
    }
    if (strcmp(error.source, "<callback>") != 0) {
        fail("json_load_callback returned an invalid error source");
    }
    if (strcmp(error.text, "']' expected near end of file") != 0) {
        fail("json_load_callback returned an invalid error message for an "
             "unclosed top-level array");
    }

    json = json_load_callback(NULL, NULL, 0, &error);
    if (json) {
        json_decref(json);
        fail("json_load_callback should have failed on NULL load callback, but "
             "it didn't");
    }
    if (strcmp(error.text, "wrong arguments") != 0) {
        fail("json_load_callback returned an invalid error message for a NULL "
             "load callback");
    }

    chunked_reading_matches_buffer();
    embedded_nul_is_not_eof();
}
