#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mbedtls/base64.h>
#include "imap/imap.h"
#include "cache/cache.h"

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
// Flag bit parser — case-insensitive, shared by fetch and sync
// ---------------------------------------------------------------------------

static uint32_t parse_imap_flags(const char *content, size_t len) {
    char buf[256] = {0};
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    for (size_t i = 0; i < len; i++)
        buf[i] = (char)tolower((unsigned char)content[i]);
    uint32_t flags = 0;
    if (strstr(buf, "\\seen"))     flags |= FLAG_SEEN;
    if (strstr(buf, "\\answered")) flags |= FLAG_ANSWERED;
    if (strstr(buf, "\\flagged"))  flags |= FLAG_FLAGGED;
    if (strstr(buf, "\\deleted"))  flags |= FLAG_DELETED;
    if (strstr(buf, "\\draft"))    flags |= FLAG_DRAFT;
    return flags;
}

// ---------------------------------------------------------------------------
// RFC 2047 encoded-word decoder  =?charset?B/Q?...?=
// ---------------------------------------------------------------------------

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Decode a single encoded-word into out (null-terminated).
// Returns bytes written, or -1 if not an encoded-word.
static int decode_encoded_word(const char *p, char *out, size_t out_size) {
    if (strncmp(p, "=?", 2) != 0) return -1;
    const char *q = p + 2;

    // Skip charset
    const char *enc_start = strchr(q, '?');
    if (!enc_start) return -1;
    enc_start++;

    char encoding = (char)toupper((unsigned char)*enc_start);
    if (encoding != 'B' && encoding != 'Q') return -1;
    if (enc_start[1] != '?') return -1;

    const char *text_start = enc_start + 2;
    const char *text_end   = strstr(text_start, "?=");
    if (!text_end) return -1;

    size_t text_len = (size_t)(text_end - text_start);

    if (encoding == 'B') {
        // Base64
        size_t out_len = 0;
        unsigned char tmp[1024];
        if (text_len > sizeof(tmp)) text_len = sizeof(tmp);
        if (mbedtls_base64_decode(tmp, sizeof(tmp), &out_len,
                                  (const unsigned char *)text_start, text_len) != 0)
            return -1;
        if (out_len >= out_size) out_len = out_size - 1;
        memcpy(out, tmp, out_len);
        out[out_len] = '\0';
        return (int)(text_end - p + 2); // bytes consumed from input
    } else {
        // Quoted-printable
        size_t j = 0;
        for (size_t i = 0; i < text_len && j < out_size - 1; i++) {
            if (text_start[i] == '_') {
                out[j++] = ' ';
            } else if (text_start[i] == '=' && i + 2 < text_len) {
                out[j++] = (char)((hex_val(text_start[i+1]) << 4) |
                                   hex_val(text_start[i+2]));
                i += 2;
            } else {
                out[j++] = text_start[i];
            }
        }
        out[j] = '\0';
        return (int)(text_end - p + 2);
    }
}

// Decode all encoded-words in a header value into out.
static void decode_rfc2047(const char *in, char *out, size_t out_size) {
    size_t j = 0;
    const char *p = in;
    while (*p && j < out_size - 1) {
        if (strncmp(p, "=?", 2) == 0) {
            char word[512];
            int consumed = decode_encoded_word(p, word, sizeof(word));
            if (consumed > 0) {
                size_t wlen = strlen(word);
                if (j + wlen >= out_size) wlen = out_size - j - 1;
                memcpy(out + j, word, wlen);
                j += wlen;
                p += consumed;
                // Skip whitespace between adjacent encoded-words
                while (*p == ' ' || *p == '\t') {
                    if (strncmp(p + 1, "=?", 2) == 0) { p++; break; }
                    out[j++] = *p++;
                }
                continue;
            }
        }
        out[j++] = *p++;
    }
    out[j] = '\0';
}

// ---------------------------------------------------------------------------
// Date formatter: RFC 2822 → "Apr 20 10:41"
// ---------------------------------------------------------------------------

static const char *month_names[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

static void format_date(const char *rfc2822, char *out, size_t out_size) {
    // RFC 2822: "Mon, 20 Apr 2026 10:41:19 +0000"
    // Skip optional weekday
    const char *p = rfc2822;
    while (*p == ' ') p++;
    if (strchr(p, ',')) p = strchr(p, ',') + 1;
    while (*p == ' ') p++;

    int day = 0; char mon_str[8] = {0}; int year = 0;
    int hour = 0, min = 0;
    // Try "DD Mon YYYY HH:MM"
    if (sscanf(p, "%d %7s %d %d:%d", &day, mon_str, &year, &hour, &min) >= 4) {
        // Find month index
        const char *mon_abbr = "???";
        for (int i = 0; i < 12; i++) {
            if (strncasecmp(mon_str, month_names[i], 3) == 0) {
                mon_abbr = month_names[i];
                break;
            }
        }
        snprintf(out, out_size, "%s %2d %02d:%02d", mon_abbr, day, hour, min);
        return;
    }
    // Fallback: trim to last out_size chars
    size_t len = strlen(rfc2822);
    if (len >= out_size) rfc2822 += len - out_size + 1;
    strncpy(out, rfc2822, out_size - 1);
    out[out_size - 1] = '\0';
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

int imap_fetch_headers(ImapConnection *conn, int start, int end, MessageList *list) {
    if (start <= 0 || end < start) return 0;

    int tag = conn->tag_counter++;
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
        "a%03d FETCH %d:%d (UID FLAGS X-GM-THRID BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])\r\n",
        tag, start, end);

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

        // Bound metadata parsing to this message's parenthesized section,
        // ending at the literal marker '{' (before the header block data).
        // This prevents fields from one message bleeding into the next.
        char *meta_end = strchr(p, '{');
        size_t meta_len = meta_end ? (size_t)(meta_end - p) : strlen(p);
        char *meta = malloc(meta_len + 1);
        if (!meta) { p++; continue; }
        memcpy(meta, p, meta_len);
        meta[meta_len] = '\0';

        // Parse UID
        char *uid_p = strstr(meta, "UID ");
        if (uid_p) h->uid = (uint32_t)atol(uid_p + 4);

        // Parse X-GM-THRID
        char *thrid_p = strstr(meta, "X-GM-THRID ");
        if (thrid_p) h->thread_id = (uint64_t)strtoull(thrid_p + 11, NULL, 10);

        // Parse FLAGS
        char *flags_p = strstr(meta, "FLAGS (");
        if (!flags_p) flags_p = strcasestr(meta, "FLAGS (");
        if (flags_p) {
            char *flags_end = strchr(flags_p + 7, ')');
            if (flags_end)
                h->flags = parse_imap_flags(flags_p + 7,
                                            (size_t)(flags_end - (flags_p + 7)));
        }

        free(meta);

        // Parse email headers from the literal block (after "{N}\n")
        if (meta_end) {
            char *hdr_start = strchr(meta_end, '\n');
            if (hdr_start) {
                hdr_start++;
                char from_val[256]    = {0};
                char raw_subject[256] = {0};
                char raw_date[128]    = {0};
                parse_header_field(hdr_start, "From",    from_val,    sizeof(from_val));
                parse_header_field(hdr_start, "Subject", raw_subject, sizeof(raw_subject));
                parse_header_field(hdr_start, "Date",    raw_date,    sizeof(raw_date));
                decode_rfc2047(raw_subject, h->subject, sizeof(h->subject));
                format_date(raw_date, h->date, sizeof(h->date));
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

// ---------------------------------------------------------------------------
// Flag sync: fetch FLAGS for every message, bulk-update cache
// ---------------------------------------------------------------------------

int imap_sync_flags(ImapConnection *conn, Cache *c) {
    int tag = conn->tag_counter++;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "a%03d FETCH 1:* (UID FLAGS)\r\n", tag);
    if (imap_send(conn, cmd) < 0) return -1;

    char *resp = NULL;
    int ret = imap_recv_response(conn, tag, &resp, NULL);
    if (ret != 0) { free(resp); return ret; }

    size_t cap = 256, n = 0;
    uint32_t *uids      = malloc(cap * sizeof(uint32_t));
    uint32_t *flags_arr = malloc(cap * sizeof(uint32_t));
    if (!uids || !flags_arr) { free(uids); free(flags_arr); free(resp); return -1; }

    // Block-based: each "* N FETCH" starts a block; next "* N" or tagged OK ends it.
    // This handles multi-line FETCH responses correctly.
    char *p = resp;
    while (*p) {
        // Find next "* N FETCH" (N is a sequence number)
        char *star = strstr(p, "* ");
        if (!star) break;
        if (!isdigit((unsigned char)star[2])) { p = star + 1; continue; }

        // Block ends at the next "* " that starts a new message, or at end of resp
        char *block_end = star + 1;
        while (*block_end) {
            char *next_star = strstr(block_end, "* ");
            if (!next_star) { block_end = resp + strlen(resp); break; }
            // Only treat as end if preceded by newline (avoids matching "* " inside data)
            if (next_star > resp && (*(next_star - 1) == '\n' || *(next_star - 1) == '\r')) {
                block_end = next_star;
                break;
            }
            block_end = next_star + 1;
        }

        size_t block_len = (size_t)(block_end - star);
        char *block = malloc(block_len + 1);
        if (!block) { p = block_end; continue; }
        memcpy(block, star, block_len);
        block[block_len] = '\0';

        uint32_t uid        = 0;
        int      flags_found = 0;
        uint32_t flags       = 0;

        char *uid_p = strstr(block, "UID ");
        if (uid_p) uid = (uint32_t)atol(uid_p + 4);

        char *flags_p = strstr(block, "FLAGS (");
        if (!flags_p) flags_p = strcasestr(block, "FLAGS (");
        if (flags_p) {
            char *flags_end = strchr(flags_p + 7, ')');
            if (flags_end) {
                flags_found = 1;
                flags = parse_imap_flags(flags_p + 7,
                                         (size_t)(flags_end - (flags_p + 7)));
            }
        }

        free(block);

        // Only update if we found both UID and FLAGS — never overwrite with stale 0
        if (uid > 0 && flags_found) {
            if (n >= cap) {
                cap *= 2;
                uint32_t *tu = realloc(uids,      cap * sizeof(uint32_t));
                uint32_t *tf = realloc(flags_arr, cap * sizeof(uint32_t));
                if (!tu || !tf) {
                    free(tu ? tu : uids);
                    free(tf ? tf : flags_arr);
                    free(resp);
                    return -1;
                }
                uids      = tu;
                flags_arr = tf;
            }
            uids[n]      = uid;
            flags_arr[n] = flags;
            n++;
        }

        p = block_end;
    }

    free(resp);
    if (n > 0) cache_bulk_update_flags(c, uids, flags_arr, n);
    free(uids);
    free(flags_arr);
    return 0;
}

// ---------------------------------------------------------------------------
// Folder management
// ---------------------------------------------------------------------------

int imap_create_mailbox(ImapConnection *conn, const char *name) {
    int tag = conn->tag_counter++;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "a%03d CREATE \"%s\"\r\n", tag, name);
    if (imap_send(conn, cmd) < 0) return -1;
    char *resp = NULL;
    int ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    return ret; // -1 if already exists — caller ignores this
}

// Move a message: UID COPY to dest, mark \Deleted, EXPUNGE.
int imap_uid_move(ImapConnection *conn, uint32_t uid, const char *dest) {
    char cmd[512];
    char *resp = NULL;
    int tag, ret;

    // UID COPY
    tag = conn->tag_counter++;
    snprintf(cmd, sizeof(cmd), "a%03d UID COPY %u \"%s\"\r\n", tag, uid, dest);
    if (imap_send(conn, cmd) < 0) return -1;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    if (ret != 0) return -1;

    // UID STORE +FLAGS (\Deleted)
    tag = conn->tag_counter++;
    snprintf(cmd, sizeof(cmd), "a%03d UID STORE %u +FLAGS (\\Deleted)\r\n", tag, uid);
    if (imap_send(conn, cmd) < 0) return -1;
    resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    if (ret != 0) return -1;

    // EXPUNGE
    tag = conn->tag_counter++;
    snprintf(cmd, sizeof(cmd), "a%03d EXPUNGE\r\n", tag);
    if (imap_send(conn, cmd) < 0) return -1;
    resp = NULL;
    ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    return ret;
}

// Mark every message in the currently selected mailbox as \Seen
int imap_mark_all_seen(ImapConnection *conn) {
    int tag = conn->tag_counter++;
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "a%03d STORE 1:* +FLAGS.SILENT (\\Seen)\r\n", tag);
    if (imap_send(conn, cmd) < 0) return -1;
    char *resp = NULL;
    int ret = imap_recv_response(conn, tag, &resp, NULL);
    free(resp);
    return ret;
}
