#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "imap.h"

// ---------------------------------------------------------------------------
// Low-level I/O
// ---------------------------------------------------------------------------

int imap_send(ImapConnection *conn, const char *buf) {
    int len = (int)strlen(buf);
    int written = 0;
    while (written < len) {
        int ret = mbedtls_ssl_write(&conn->ssl_ctx,
                                    (const unsigned char *)buf + written,
                                    len - written);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) return ret;
        written += ret;
    }
    return written;
}

int imap_recv(ImapConnection *conn, char *buf, size_t buf_size) {
    int ret;
    do {
        ret = mbedtls_ssl_read(&conn->ssl_ctx, (unsigned char *)buf, buf_size - 1);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
    if (ret < 0) return ret;
    buf[ret] = '\0';
    return ret;
}

// Read until we see "a<tag> OK/NO/BAD" on its own line.
// Accumulates into a malloc'd buffer; caller must free *out.
int imap_recv_response(ImapConnection *conn, int tag, char **out, size_t *out_len) {
    size_t cap = 65536;
    size_t used = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;

    char tag_ok[32], tag_no[32], tag_bad[32];
    snprintf(tag_ok,  sizeof(tag_ok),  "a%03d OK",  tag);
    snprintf(tag_no,  sizeof(tag_no),  "a%03d NO",  tag);
    snprintf(tag_bad, sizeof(tag_bad), "a%03d BAD", tag);

    char chunk[4096];
    while (1) {
        int n = imap_recv(conn, chunk, sizeof(chunk));
        if (n <= 0) { free(buf); return n == 0 ? -1 : n; }

        // Grow if needed
        if (used + n + 1 > cap) {
            cap = (used + n + 1) * 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); return -1; }
            buf = tmp;
        }
        memcpy(buf + used, chunk, n);
        used += n;
        buf[used] = '\0';

        // Check if the last line is a tagged completion
        if (strstr(buf, tag_ok) || strstr(buf, tag_no) || strstr(buf, tag_bad))
            break;
    }

    *out = buf;
    if (out_len) *out_len = used;

    // Return 0 for OK, -1 for NO/BAD
    if (strstr(buf, tag_ok))  return 0;
    return -1;
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

int imap_login(ImapConnection *conn, const char *username, const char *password) {
    int tag = conn->tag_counter++;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "a%03d LOGIN %s \"%s\"\r\n", tag, username, password);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    return ret;
}

int imap_logout(ImapConnection *conn) {
    int tag = conn->tag_counter++;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "a%03d LOGOUT\r\n", tag);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    return ret;
}

// ---------------------------------------------------------------------------
// Mailbox
// ---------------------------------------------------------------------------

int imap_select(ImapConnection *conn, const char *mailbox, int *out_exists) {
    int tag = conn->tag_counter++;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "a%03d SELECT \"%s\"\r\n", tag, mailbox);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    if (ret == 0 && out_exists) {
        // Parse "* N EXISTS"
        char *p = strstr(resp, " EXISTS");
        if (p) {
            char *start = p;
            while (start > resp && isdigit((unsigned char)*(start - 1))) start--;
            *out_exists = atoi(start);
        }
    }
    free(resp);
    return ret;
}

// List mailboxes matching pattern; caller must free each name and the array.
int imap_list(ImapConnection *conn, char ***out_names, int *out_count) {
    int tag = conn->tag_counter++;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "a%03d LIST \"\" \"*\"\r\n", tag);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    if (ret != 0) { free(resp); return ret; }

    // Count LIST lines
    int count = 0;
    char *p = resp;
    while ((p = strstr(p, "* LIST")) != NULL) { count++; p++; }

    char **names = malloc(sizeof(char *) * count);
    if (!names) { free(resp); return -1; }

    int i = 0;
    p = resp;
    while ((p = strstr(p, "* LIST")) != NULL && i < count) {
        // Format: * LIST (\Flags) "/" "mailbox" or * LIST (\Flags) "/" mailbox
        char *delim = strstr(p, "\" \"");
        if (!delim) { delim = strstr(p, "\" "); if (!delim) { p++; continue; } delim++; }
        else delim += 3;

        char *eol = strpbrk(delim, "\r\n");
        size_t len = eol ? (size_t)(eol - delim) : strlen(delim);
        // Strip surrounding quotes if present
        if (delim[0] == '"') { delim++; len -= 2; }
        names[i] = malloc(len + 1);
        if (names[i]) { memcpy(names[i], delim, len); names[i][len] = '\0'; }
        i++;
        p++;
    }

    free(resp);
    *out_names = names;
    *out_count = i;
    return 0;
}

// ---------------------------------------------------------------------------
// Header parsing helpers
// ---------------------------------------------------------------------------

static void parse_header_field(const char *headers, const char *field,
                                char *out, size_t out_size) {
    char search[64];
    snprintf(search, sizeof(search), "\r\n%s:", field);
    const char *p = strcasestr(headers, search);
    if (!p) {
        // Try at start of string
        char search2[64];
        snprintf(search2, sizeof(search2), "%s:", field);
        if (strncasecmp(headers, search2, strlen(search2)) == 0)
            p = headers - 2; // will be offset by +2 below
        else { out[0] = '\0'; return; }
    }
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    const char *end = strpbrk(p, "\r\n");
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

// Parse "Name <addr>" or bare "addr" into name/address fields
static void parse_from(const char *value, char *name, size_t name_size,
                        char *addr, size_t addr_size) {
    const char *lt = strchr(value, '<');
    const char *gt = strchr(value, '>');
    if (lt && gt && gt > lt) {
        size_t alen = (size_t)(gt - lt - 1);
        if (alen >= addr_size) alen = addr_size - 1;
        memcpy(addr, lt + 1, alen);
        addr[alen] = '\0';

        // Name is everything before '<', trimmed
        size_t nlen = (size_t)(lt - value);
        while (nlen > 0 && (value[nlen-1] == ' ' || value[nlen-1] == '"')) nlen--;
        size_t nstart = 0;
        while (nstart < nlen && (value[nstart] == ' ' || value[nstart] == '"')) nstart++;
        nlen -= nstart;
        if (nlen >= name_size) nlen = name_size - 1;
        memcpy(name, value + nstart, nlen);
        name[nlen] = '\0';
    } else {
        strncpy(addr, value, addr_size - 1);
        addr[addr_size - 1] = '\0';
        name[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Fetch headers
// ---------------------------------------------------------------------------

int imap_fetch_headers(ImapConnection *conn, int count, MessageList *list) {
    if (count <= 0) return 0;

    int tag = conn->tag_counter++;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
        "a%03d FETCH 1:%d (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])\r\n",
        tag, count);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    size_t resp_len = 0;
    ret = imap_recv_response(conn, tag, &resp, &resp_len);
    if (ret != 0) { free(resp); return ret; }

    // Count FETCH responses to size the allocation
    int n = 0;
    char *p = resp;
    while ((p = strstr(p, "* ")) != NULL) {
        if (isdigit((unsigned char)p[2])) n++;
        p++;
    }

    MessageHeader *headers = calloc(n, sizeof(MessageHeader));
    if (!headers) { free(resp); return -1; }

    int idx = 0;
    p = resp;
    while ((p = strstr(p, "* ")) != NULL && idx < n) {
        if (!isdigit((unsigned char)p[2])) { p++; continue; }

        MessageHeader *h = &headers[idx];

        // Parse UID
        char *uid_p = strstr(p, "UID ");
        if (uid_p) h->uid = (uint32_t)atol(uid_p + 4);

        // Parse FLAGS
        char *flags_p = strstr(p, "FLAGS (");
        if (flags_p) {
            char *flags_end = strchr(flags_p, ')');
            if (flags_end) {
                char flags_str[256] = {0};
                size_t flen = (size_t)(flags_end - flags_p - 7);
                if (flen < sizeof(flags_str)) {
                    memcpy(flags_str, flags_p + 7, flen);
                    if (strstr(flags_str, "\\Seen"))     h->flags |= FLAG_SEEN;
                    if (strstr(flags_str, "\\Answered")) h->flags |= FLAG_ANSWERED;
                    if (strstr(flags_str, "\\Flagged"))  h->flags |= FLAG_FLAGGED;
                    if (strstr(flags_str, "\\Deleted"))  h->flags |= FLAG_DELETED;
                    if (strstr(flags_str, "\\Draft"))    h->flags |= FLAG_DRAFT;
                }
            }
        }

        // Find the header block (after the literal size marker "{N}")
        char *hdr_start = strchr(p, '{');
        if (hdr_start) {
            hdr_start = strchr(hdr_start, '\n');
            if (hdr_start) {
                hdr_start++;
                char from_val[256] = {0};
                parse_header_field(hdr_start, "From",    from_val,    sizeof(from_val));
                parse_header_field(hdr_start, "Subject", h->subject,  sizeof(h->subject));
                parse_header_field(hdr_start, "Date",    h->date,     sizeof(h->date));
                parse_from(from_val, h->from_name, sizeof(h->from_name),
                                     h->from_address, sizeof(h->from_address));
            }
        }

        idx++;
        p++;
    }

    list->headers = headers;
    list->count   = idx;
    free(resp);
    return 0;
}

// ---------------------------------------------------------------------------
// Fetch full body
// ---------------------------------------------------------------------------

int imap_fetch_body(ImapConnection *conn, uint32_t uid, char **out, size_t *out_len) {
    int tag = conn->tag_counter++;
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "a%03d UID FETCH %u BODY[]\r\n", tag, uid);

    int ret = imap_send(conn, cmd);
    if (ret < 0) return ret;

    char *resp = NULL;
    size_t resp_len = 0;
    ret = imap_recv_response(conn, tag, &resp, &resp_len);
    if (ret != 0) { free(resp); return ret; }

    // Body starts after "{size}\r\n"
    char *body_start = strchr(resp, '{');
    if (!body_start) { *out = resp; if (out_len) *out_len = resp_len; return 0; }

    long body_size = atol(body_start + 1);
    body_start = strchr(body_start, '\n');
    if (!body_start) { free(resp); return -1; }
    body_start++;

    char *body = malloc(body_size + 1);
    if (!body) { free(resp); return -1; }
    memcpy(body, body_start, body_size);
    body[body_size] = '\0';

    free(resp);
    *out = body;
    if (out_len) *out_len = body_size;
    return 0;
}
