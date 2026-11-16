#include "files_c.h"
#include "parse_int.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

struct lines {
    char values[4][32];
    size_t lengths[4];
    size_t count;
    size_t stop_after;
};

static int collect_line(const char *line, size_t length, const void *opaque) {
    struct lines *state = (struct lines *)opaque;
    size_t slot = state->count;

    if (slot >= 4 || length >= sizeof(state->values[slot])) {
        return 0;
    }

    memcpy(state->values[slot], line, length);
    state->values[slot][length] = '\0';
    state->lengths[slot] = length;
    state->count++;
    return state->stop_after == 0 || state->count < state->stop_after;
}

static int test_parse_int(void) {
    char max_value[32];
    char min_value[32];

    (void)snprintf(max_value, sizeof(max_value), "%d", INT_MAX);
    (void)snprintf(min_value, sizeof(min_value), "%d", INT_MIN);

    if (onyx_parse_int(max_value, strlen(max_value)) != INT_MAX) return 1;
    if (onyx_parse_int(min_value, strlen(min_value)) != INT_MIN) return 2;
    if (onyx_parse_int("0", 1) != 0) return 3;
    if (onyx_parse_int("-1", 2) != -1) return 4;
    if (onyx_parse_int(NULL, 1) != -1) return 5;
    if (onyx_parse_int("", 0) != -1) return 6;
    if (onyx_parse_int("-", 1) != -1) return 7;
    if (onyx_parse_int("12x", 3) != -1) return 8;
    if (onyx_parse_int("2147483648", 10) != -1) return 9;
    if (onyx_parse_int("-2147483649", 11) != -1) return 10;
    if (onyx_parse_int("1", 2) != -1) return 11;
    return 0;
}

static int test_readline_trim_and_stop(void) {
    FILE *stream = tmpfile();
    struct lines state = {0};

    if (stream == NULL) return 20;
    if (fputs("  alpha  \r\n   \nlast\n", stream) < 0) {
        fclose(stream);
        return 21;
    }
    rewind(stream);

    omg_file_readline(stream, 1, collect_line, &state);
    fclose(stream);

    if (state.count != 3) return 22;
    if (state.lengths[0] != 5 || strcmp(state.values[0], "alpha") != 0) return 23;
    if (state.lengths[1] != 0 || strcmp(state.values[1], "") != 0) return 24;
    if (state.lengths[2] != 4 || strcmp(state.values[2], "last") != 0) return 25;

    stream = tmpfile();
    state = (struct lines){0};
    state.stop_after = 1;
    if (stream == NULL) return 26;
    if (fputs("first\nsecond\n", stream) < 0) {
        fclose(stream);
        return 27;
    }
    rewind(stream);
    omg_file_readline(stream, 0, collect_line, &state);
    fclose(stream);

    if (state.count != 1) return 28;
    if (state.lengths[0] != 6 || strcmp(state.values[0], "first\n") != 0) return 29;

    /* Invalid inputs are documented no-ops. */
    omg_file_readline(NULL, 0, collect_line, &state);
    omg_file_readline(stdin, 0, NULL, &state);
    return 0;
}

int main(void) {
    int result = test_parse_int();
    if (result != 0) {
        fprintf(stderr, "parse_int test failed: %d\n", result);
        return result;
    }

    result = test_readline_trim_and_stop();
    if (result != 0) {
        fprintf(stderr, "readline test failed: %d\n", result);
        return result;
    }

    puts("common C utility tests passed");
    return 0;
}
