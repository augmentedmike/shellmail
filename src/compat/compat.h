#ifndef compat_h
#define compat_h

// Portable case-insensitive string search.
// strcasestr is a GNU/BSD extension not in C99 — use this everywhere.
const char *compat_strcasestr(const char *haystack, const char *needle);

#endif
