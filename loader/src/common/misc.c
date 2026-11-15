#include "parse_int.h"

#include <limits.h>

/*
 * Keep integer parsing independent of the C++ standard library so this
 * utility can be compiled as C18. The negative accumulator allows INT_MIN
 * to be represented without signed overflow.
 */
int onyx_parse_int(const char *data, size_t length) {
    size_t index = 0;
    int negative = 0;
    int value = 0;
    int limit;
    int multiply_limit;

    if (data == NULL || length == 0) {
        return -1;
    }

    if (data[index] == '-') {
        negative = 1;
        ++index;
    }

    if (index == length) {
        return -1;
    }

    limit = negative ? INT_MIN : -INT_MAX;
    multiply_limit = limit / 10;

    for (; index < length; ++index) {
        const unsigned char ch = (unsigned char)data[index];
        int digit;

        if (ch < (unsigned char)'0' || ch > (unsigned char)'9') {
            return -1;
        }

        digit = (int)(ch - (unsigned char)'0');

        if (value < multiply_limit) {
            return -1;
        }

        value *= 10;
        if (value < limit + digit) {
            return -1;
        }

        value -= digit;
    }

    return negative ? value : -value;
}
