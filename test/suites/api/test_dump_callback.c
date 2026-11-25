/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "util.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

struct my_sink {
    char *buf;
    size_t off;
    size_t cap;
};

static int my_writer(const char *buffer, size_t len, void *data) {
    struct my_sink *s = data;
    if (len > s->cap - s->off) {
        return -1;
    }
    memcpy(s->buf + s->off, buffer, len);
    s->off += len;
    return 0;
}

/* A writer that fails once its fixed capacity is exhausted. After the first
 * failure it keeps counting invocations but never accepts more data. This
 * verifies that a failed export aborts immediately instead of submitting
 * further chunks, which could leave a half-written, possibly sensitive
 * object at the destination. */
struct counting_sink {
    char *buf;
    size_t off;
    size_t cap;
    size_t calls_after_failure;
    int failed;
};

static int counting_writer(const char *buffer, size_t len, void *data) {
    struct counting_sink *s = data;

    if (s->failed) {
        s->calls_after_failure++;
        return 0;
    }

    if (len > s->cap - s->off) {
        s->failed = 1;
        return -1;
    }

    memcpy(s->buf + s->off, buffer, len);
    s->off += len;
    return 0;
}

static void failing_callback_aborts_export() {
    struct counting_sink s;
    json_t *json;
    size_t total;

    /* An object with several members so that a tiny capacity is exhausted
     * while the first (possibly sensitive) key is still being written. */
    json = json_pack("{s:s,s:s,s:s,s:s}", "secret", "password",
                     "token", "abcdefghijklmnop", "user", "admin",
                     "data", "sensitive-value");
    if (!json)
        fail("json_pack failed");

    /* Full length produced by a normal (always succeeding) export. */
    total = json_dumpb(json, NULL, 0, JSON_COMPACT);
    if (total == 0) {
        json_decref(json);
        fail("json_dumpb could not measure the encoding");
    }

    s.buf = malloc(total);
    if (!s.buf) {
        json_decref(json);
        fail("malloc failed");
    }

    /* A one-byte capacity is exhausted in the middle of the first key.
     * Exercise both object dumping branches (unsorted and sorted keys). */
    for (int sort_keys = 0; sort_keys <= 1; sort_keys++) {
        size_t flags = JSON_COMPACT | (sort_keys ? JSON_SORT_KEYS : 0);

        s.off = 0;
        s.cap = 1;
        s.failed = 0;
        s.calls_after_failure = 0;

        if (json_dump_callback(json, counting_writer, &s, flags) != -1) {
            free(s.buf);
            json_decref(json);
            fail("json_dump_callback succeeded after the callback failed");
        }

        /* No chunk may be submitted after the first failure, otherwise a
         * half-written object is left at the destination. */
        if (s.calls_after_failure != 0) {
            free(s.buf);
            json_decref(json);
            fail("failed export kept submitting chunks to the callback");
        }

        if (s.off > s.cap) {
            free(s.buf);
            json_decref(json);
            fail("failed export wrote beyond the sink capacity");
        }
    }

    /* With a full capacity, normal streaming export must keep working and
     * deliver the complete encoding. */
    s.off = 0;
    s.cap = total;
    s.failed = 0;
    s.calls_after_failure = 0;
    if (json_dump_callback(json, counting_writer, &s,
                           JSON_COMPACT | JSON_SORT_KEYS) == -1) {
        free(s.buf);
        json_decref(json);
        fail("json_dump_callback failed with a full-capacity sink");
    }
    if (s.off != total) {
        free(s.buf);
        json_decref(json);
        fail("successful export did not deliver the complete encoding");
    }

    free(s.buf);
    json_decref(json);
}

static void run_tests() {
    struct my_sink s;
    json_t *json;
    const char str[] = "[\"A\", {\"B\": \"C\", \"e\": false}, 1, null, \"foo\"]";
    char *dumped_to_string;

    json = json_loads(str, 0, NULL);
    if (!json) {
        fail("json_loads failed");
    }

    dumped_to_string = json_dumps(json, 0);
    if (!dumped_to_string) {
        json_decref(json);
        fail("json_dumps failed");
    }

    s.off = 0;
    s.cap = strlen(dumped_to_string);
    s.buf = malloc(s.cap);
    if (!s.buf) {
        json_decref(json);
        free(dumped_to_string);
        fail("malloc failed");
    }

    if (json_dump_callback(json, my_writer, &s, 0) == -1) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback failed on an exact-length sink buffer");
    }

    if (strncmp(dumped_to_string, s.buf, s.off) != 0) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback and json_dumps did not produce identical "
             "output");
    }

    s.off = 1;
    if (json_dump_callback(json, my_writer, &s, 0) != -1) {
        json_decref(json);
        free(dumped_to_string);
        free(s.buf);
        fail("json_dump_callback succeeded on a short buffer when it should "
             "have failed");
    }

    json_decref(json);
    free(dumped_to_string);
    free(s.buf);

    failing_callback_aborts_export();
}
