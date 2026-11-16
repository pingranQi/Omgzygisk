#ifndef ONYX_PROC_MAPS_H
#define ONYX_PROC_MAPS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ONYX_MAP_READ = 1u << 0,
    ONYX_MAP_WRITE = 1u << 1,
    ONYX_MAP_EXEC = 1u << 2,
    ONYX_MAP_PRIVATE = 1u << 3
};

typedef struct {
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;
    uint64_t inode;
    unsigned int dev_major;
    unsigned int dev_minor;
    unsigned int flags;
    const char *path;
    size_t path_length;
} onyx_proc_map_entry;

typedef int (*onyx_proc_map_callback)(const onyx_proc_map_entry *entry, void *context);

/* Parse Linux procfs maps text. Invalid lines are skipped. */
void onyx_proc_maps_parse_line(const char *line, size_t length,
                               onyx_proc_map_callback callback, void *context);

/* Open and parse /proc/<pid>/maps; returns 0 on success, -1 on I/O failure. */
int onyx_proc_maps_scan(const char *pid, onyx_proc_map_callback callback, void *context);

#ifdef __cplusplus
}
#endif

#endif
