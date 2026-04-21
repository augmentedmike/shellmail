#include <string.h>
#include <strings.h>  // strncasecmp
#include "compat/compat.h"

const char *compat_strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return haystack;
    }
    return NULL;
}
