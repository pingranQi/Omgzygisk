#define _GNU_SOURCE
#include "proc_maps.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void onyx_proc_maps_parse_line(const char *line, size_t length,
                               onyx_proc_map_callback callback, void *context) {
    if (line == NULL || callback == NULL) return;

    char *buffer = malloc(length + 1);
    if (buffer == NULL) return;
    memcpy(buffer, line, length);
    buffer[length] = '\0';

    uintptr_t start = 0, end = 0, offset = 0;
    uint64_t inode = 0;
    unsigned int dev_major = 0, dev_minor = 0;
    char permissions[5] = {0};
    int path_offset = 0;

    const int fields = sscanf(buffer,
        "%" SCNxPTR "-%" SCNxPTR " %4s %" SCNxPTR " %x:%x %" SCNu64 " %n",
        &start, &end, permissions, &offset, &dev_major, &dev_minor, &inode, &path_offset);

    if (fields == 7 && end >= start && path_offset >= 0 && (size_t)path_offset <= length) {
        size_t path_length = length - (size_t)path_offset;
        while (path_length > 0 &&
               (buffer[path_offset + path_length - 1] == '\n' ||
                buffer[path_offset + path_length - 1] == '\r' ||
                buffer[path_offset + path_length - 1] == '\0')) {
            --path_length;
        }

        unsigned int flags = 0;
        if (permissions[0] == 'r') flags |= ONYX_MAP_READ;
        if (permissions[1] == 'w') flags |= ONYX_MAP_WRITE;
        if (permissions[2] == 'x') flags |= ONYX_MAP_EXEC;
        if (permissions[3] == 'p') flags |= ONYX_MAP_PRIVATE;

        const onyx_proc_map_entry entry = {
            .start = start,
            .end = end,
            .offset = offset,
            .inode = inode,
            .dev_major = dev_major,
            .dev_minor = dev_minor,
            .flags = flags,
            .path = buffer + path_offset,
            .path_length = path_length
        };
        (void)callback(&entry, context);
    }

    free(buffer);
}

int onyx_proc_maps_scan(const char *pid, onyx_proc_map_callback callback, void *context) {
    if (pid == NULL || callback == NULL || pid[0] == '\0') return -1;
    if (strcmp(pid, "self") != 0) {
        for (const unsigned char *p = (const unsigned char *)pid; *p != '\0'; ++p) {
            if (*p < '0' || *p > '9') return -1;
        }
    }

    char path[64];
    const int path_length = snprintf(path, sizeof(path), "/proc/%s/maps", pid);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) return -1;

    FILE *stream = fopen(path, "r");
    if (stream == NULL) return -1;

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, stream)) >= 0) {
        onyx_proc_maps_parse_line(line, (size_t)length, callback, context);
    }

    const int failed = ferror(stream);
    free(line);
    fclose(stream);
    return failed ? -1 : 0;
}
