#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/base64.h>

#include "caldav/caldav.h"
#include "net/tls.h"

// ============================================================================
// Event list helpers
// ============================================================================

void cal_event_list_init(CalEventList *l) {
    l->events = NULL;
    l->count  = 0;
    l->cap    = 0;
}

void cal_event_list_free(CalEventList *l) {
    free(l->events);
    l->events = NULL;
    l->count  = 0;
    l->cap    = 0;
}

static int event_append(CalEventList *l, const CalEvent *e) {
    if (l->count >= l->cap) {
        size_t nc  = l->cap ? l->cap * 2 : 16;
        CalEvent *t = realloc(l->events, nc * sizeof(CalEvent));
        if (!t) return -1;
        l->events = t;
        l->cap    = nc;
    }
    l->events[l->count++] = *e;
    return 0;
}

static int event_cmp(const void *a, const void *b) {
    const CalEvent *ea = (const CalEvent *)a;
    const CalEvent *eb = (const CalEvent *)b;
    int dc = strcmp(ea->date, eb->date);
    if (dc != 0) return dc;
    if (ea->hour != eb->hour) {
        if (ea->hour < 0) return -1;
        if (eb->hour < 0) return  1;
        return ea->hour - eb->hour;
    }
    return ea->minute - eb->minute;
}

// ============================================================================
// CalDavConn initialisation — parse URL
// ============================================================================

void caldav_init(CalDavConn *conn, const char *url,
                 const char *user, const char *pass) {
    memset(conn, 0, sizeof(*conn));
    if (!url || !url[0]) return;

    const char *p = url;
    int is_https   = 1;
    if      (strncmp(p, "https://", 8) == 0) { p += 8; is_https = 1; }
    else if (strncmp(p, "http://",  7) == 0) { p += 7; is_https = 0; }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        size_t sn = (size_t)(colon - p);
        if (sn >= sizeof(conn->server)) sn = sizeof(conn->server) - 1;
        memcpy(conn->server, p, sn);

        const char *pe = slash ? slash : colon + strlen(colon);
        size_t pn = (size_t)(pe - colon - 1);
        if (pn >= sizeof(conn->port)) pn = sizeof(conn->port) - 1;
        memcpy(conn->port, colon + 1, pn);
    } else if (slash) {
        size_t sn = (size_t)(slash - p);
        if (sn >= sizeof(conn->server)) sn = sizeof(conn->server) - 1;
        memcpy(conn->server, p, sn);
        strncpy(conn->port, is_https ? "443" : "80", sizeof(conn->port) - 1);
    } else {
        strncpy(conn->server, p,    sizeof(conn->server) - 1);
        strncpy(conn->port, is_https ? "443" : "80", sizeof(conn->port) - 1);
    }

    if (slash) strncpy(conn->path, slash, sizeof(conn->path) - 1);
    else       strncpy(conn->path, "/",   sizeof(conn->path) - 1);

    if (user) strncpy(conn->username, user, sizeof(conn->username) - 1);
    if (pass) strncpy(conn->password, pass, sizeof(conn->password) - 1);
}

// ============================================================================
// HTTP/1.1 over TLS
// ============================================================================

static void make_basic_auth(const char *user, const char *pass,
                              char *out, size_t out_len) {
    char creds[512];
    snprintf(creds, sizeof(creds), "%s:%s", user, pass);
    unsigned char b64[768];
    size_t olen = 0;
    mbedtls_base64_encode(b64, sizeof(b64), &olen,
                           (const unsigned char *)creds, strlen(creds));
    b64[olen] = '\0';
    snprintf(out, out_len, "Basic %s", (char *)b64);
}

static int ssl_write_all(mbedtls_ssl_context *ssl, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r;
        do { r = mbedtls_ssl_write(ssl,
                 (const unsigned char *)buf + sent, len - sent);
        } while (r == MBEDTLS_ERR_SSL_WANT_WRITE);
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static int ssl_read_line(mbedtls_ssl_context *ssl, char *buf, size_t max) {
    size_t n = 0;
    while (n < max - 1) {
        unsigned char c;
        int r;
        do { r = mbedtls_ssl_read(ssl, &c, 1);
        } while (r == MBEDTLS_ERR_SSL_WANT_READ ||
                 r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
        if (r <= 0) break;
        if (c == '\n') break;
        if (c != '\r') buf[n++] = (char)c;
    }
    buf[n] = '\0';
    return (int)n;
}

static int ssl_read_exact(mbedtls_ssl_context *ssl, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int r;
        do { r = mbedtls_ssl_read(ssl,
                 (unsigned char *)buf + got, len - got);
        } while (r == MBEDTLS_ERR_SSL_WANT_READ ||
                 r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return (int)got;
}

#define HTTP_MAX_BODY (1 * 1024 * 1024)

// Parse a URL into server/port/path components (https assumed).
static void parse_url(const char *url,
                       char *srv, size_t srv_len,
                       char *prt, size_t prt_len,
                       char *pth, size_t pth_len) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://",  7) == 0) p += 7;

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    if (colon && (!slash || colon < slash)) {
        size_t sn = (size_t)(colon - p);
        if (sn >= srv_len) sn = srv_len - 1;
        memcpy(srv, p, sn); srv[sn] = '\0';
        const char *pe = slash ? slash : colon + strlen(colon);
        size_t pn = (size_t)(pe - colon - 1);
        if (pn >= prt_len) pn = prt_len - 1;
        memcpy(prt, colon + 1, pn); prt[pn] = '\0';
    } else if (slash) {
        size_t sn = (size_t)(slash - p);
        if (sn >= srv_len) sn = srv_len - 1;
        memcpy(srv, p, sn); srv[sn] = '\0';
        strncpy(prt, "443", prt_len - 1);
    } else {
        strncpy(srv, p, srv_len - 1);
        strncpy(prt, "443", prt_len - 1);
    }
    if (slash) strncpy(pth, slash, pth_len - 1);
    else       strncpy(pth, "/",   pth_len - 1);
}

// Send an HTTP request and return an allocated response body (caller frees).
// depth: "0", "1", or NULL to omit the Depth header.
// status_out and len_out are optional.
// Follows up to 3 HTTP redirects automatically.
static char *http_do(const char *server, const char *port,
                      const char *method, const char *path,
                      const char *auth,   const char *depth,
                      const char *body,   size_t body_len,
                      int *status_out, size_t *len_out) {
    char cur_server[256], cur_port[6], cur_path[512];
    strncpy(cur_server, server, sizeof(cur_server) - 1);
    strncpy(cur_port,   port,   sizeof(cur_port)   - 1);
    strncpy(cur_path,   path,   sizeof(cur_path)   - 1);

    for (int redir = 0; redir <= 3; redir++) {
        mbedtls_net_context net;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config  conf;

        if (tls_connect(&net, &ssl, &conf, cur_server, cur_port) != 0)
            return NULL;

        char depth_hdr[32] = "";
        if (depth) snprintf(depth_hdr, sizeof(depth_hdr), "Depth: %s\r\n", depth);

        char req[4096];
        int rl = snprintf(req, sizeof(req),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: %s\r\n"
            "Content-Type: application/xml; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "%s"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n"
            "\r\n",
            method, cur_path, cur_server, auth, body_len, depth_hdr);

        int write_ok = (ssl_write_all(&ssl, req, (size_t)rl) == 0 &&
                        ssl_write_all(&ssl, body, body_len)  == 0);
        if (!write_ok) {
            mbedtls_ssl_close_notify(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ssl_free(&ssl);
            mbedtls_net_free(&net);
            return NULL;
        }

        // Status line
        char line[4096];
        ssl_read_line(&ssl, line, sizeof(line));
        int status = 0;
        sscanf(line, "HTTP/%*s %d", &status);
        if (status_out) *status_out = status;

        // Response headers
        long content_length = -1;
        int  chunked        = 0;
        char location[512]  = "";
        while (1) {
            ssl_read_line(&ssl, line, sizeof(line));
            if (!line[0]) break;

            char *c = strchr(line, ':');
            if (!c) continue;
            *c = '\0';
            char *key = line, *val = c + 1;
            while (*val == ' ') val++;
            for (char *q = key; *q; q++)
                if (*q >= 'A' && *q <= 'Z') *q += 32;

            if      (strcmp(key, "content-length")    == 0) content_length = atol(val);
            else if (strcmp(key, "transfer-encoding") == 0) chunked = (strstr(val, "chunked") != NULL);
            else if (strcmp(key, "location")          == 0) strncpy(location, val, sizeof(location) - 1);
        }

        // Follow redirects
        if (status >= 301 && status <= 308 && location[0] && redir < 3) {
            mbedtls_ssl_close_notify(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ssl_free(&ssl);
            mbedtls_net_free(&net);
            if (strncmp(location, "http", 4) == 0) {
                // Absolute URL — re-parse server/port/path
                parse_url(location, cur_server, sizeof(cur_server),
                                    cur_port,   sizeof(cur_port),
                                    cur_path,   sizeof(cur_path));
            } else {
                // Relative path
                strncpy(cur_path, location, sizeof(cur_path) - 1);
            }
            continue;
        }

        if (status < 200 || status >= 300) {
            mbedtls_ssl_close_notify(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ssl_free(&ssl);
            mbedtls_net_free(&net);
            return NULL;
        }

        char  *resp     = NULL;
        size_t resp_len = 0;

        if (chunked) {
            resp = malloc(HTTP_MAX_BODY);
            if (!resp) goto cleanup_null;
            char szl[64];
            while (resp_len < HTTP_MAX_BODY - 1) {
                ssl_read_line(&ssl, szl, sizeof(szl));
                long csz = strtol(szl, NULL, 16);
                if (csz <= 0) break;
                if (resp_len + (size_t)csz > HTTP_MAX_BODY - 1)
                    csz = (long)(HTTP_MAX_BODY - 1 - resp_len);
                ssl_read_exact(&ssl, resp + resp_len, (size_t)csz);
                resp_len += (size_t)csz;
                ssl_read_line(&ssl, szl, sizeof(szl));
            }
            resp[resp_len] = '\0';
        } else if (content_length > 0) {
            size_t cl = (size_t)content_length;
            if (cl > HTTP_MAX_BODY) cl = HTTP_MAX_BODY;
            resp = malloc(cl + 1);
            if (!resp) goto cleanup_null;
            int got = ssl_read_exact(&ssl, resp, cl);
            resp_len = got > 0 ? (size_t)got : 0;
            resp[resp_len] = '\0';
        } else {
            resp = malloc(HTTP_MAX_BODY);
            if (!resp) goto cleanup_null;
            while (resp_len < HTTP_MAX_BODY - 1) {
                int r;
                do { r = mbedtls_ssl_read(&ssl,
                         (unsigned char *)resp + resp_len,
                         HTTP_MAX_BODY - resp_len - 1);
                } while (r == MBEDTLS_ERR_SSL_WANT_READ ||
                         r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET);
                if (r <= 0) break;
                resp_len += (size_t)r;
            }
            resp[resp_len] = '\0';
        }

        if (len_out) *len_out = resp_len;
        mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ssl_free(&ssl);
        mbedtls_net_free(&net);
        return resp;

    cleanup_null:
        mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ssl_free(&ssl);
        mbedtls_net_free(&net);
        return NULL;
    }
    return NULL;
}

// ============================================================================
// Minimal XML text extraction (no full parser — CalDAV responses are regular)
// ============================================================================

// Returns text content of first element with local name 'tag' (namespace-agnostic).
// Searches for both "tag>" and ":tag>" to match any namespace prefix.
static size_t xml_get_text(const char *xml, const char *tag,
                             char *out, size_t out_len) {
    char pat1[128], pat2[128];
    snprintf(pat1, sizeof(pat1), "%s>",  tag);
    snprintf(pat2, sizeof(pat2), ":%s>", tag);

    const char *p1 = strstr(xml, pat1);
    const char *p2 = strstr(xml, pat2);

    const char *start;
    if      (p1 && p2) start = (p1 < p2) ? p1 + strlen(pat1) : p2 + strlen(pat2);
    else if (p1)       start = p1 + strlen(pat1);
    else if (p2)       start = p2 + strlen(pat2);
    else return 0;

    const char *end = strchr(start, '<');
    if (!end) return 0;

    size_t len = (size_t)(end - start);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len;
}

// Returns text content of 'inner' element that appears after the first 'outer' element.
static size_t xml_get_nested(const char *xml,
                               const char *outer, const char *inner,
                               char *out, size_t out_len) {
    char pat1[128], pat2[128];
    snprintf(pat1, sizeof(pat1), "%s>",  outer);
    snprintf(pat2, sizeof(pat2), ":%s>", outer);

    const char *p1 = strstr(xml, pat1);
    const char *p2 = strstr(xml, pat2);

    const char *after;
    if      (p1 && p2) after = (p1 < p2) ? p1 + strlen(pat1) : p2 + strlen(pat2);
    else if (p1)       after = p1 + strlen(pat1);
    else if (p2)       after = p2 + strlen(pat2);
    else return 0;

    return xml_get_text(after, inner, out, out_len);
}

// ============================================================================
// iCal (RFC 5545) parser
// ============================================================================

// Unfold iCal lines: a line beginning with SPACE or TAB is a continuation.
static void ical_unfold(char *buf, size_t len) {
    size_t r = 0, w = 0;
    while (r < len) {
        if (buf[r] == '\r' || buf[r] == '\n') {
            if (buf[r] == '\r' && r + 1 < len && buf[r + 1] == '\n') r++;
            r++;
            if (r < len && (buf[r] == ' ' || buf[r] == '\t')) {
                r++; // eat folding whitespace — continuation
                continue;
            }
            buf[w++] = '\n';
        } else {
            buf[w++] = buf[r++];
        }
    }
    buf[w] = '\0';
}

// Parse DTSTART / DTEND property line into date, hour, minute, all_day.
static void parse_ical_dt(const char *line,
                            char *date_out, int *hour, int *min, int *all_day) {
    date_out[0] = '\0';
    *hour = -1; *min = 0; *all_day = 0;

    const char *colon = strchr(line, ':');
    if (!colon) return;
    const char *val = colon + 1;

    *all_day = (strstr(line, "VALUE=DATE") != NULL);

    size_t vlen = strlen(val);
    while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n' ||
                         val[vlen-1] == ' '  || val[vlen-1] == '\t'))
        vlen--;

    if (vlen < 8) return;

    snprintf(date_out, 11, "%.4s-%.2s-%.2s", val, val+4, val+6);

    if (!*all_day && vlen >= 15 && val[8] == 'T') {
        *hour = (val[9]  - '0') * 10 + (val[10] - '0');
        *min  = (val[11] - '0') * 10 + (val[12] - '0');
        if (*hour < 0 || *hour > 23) *hour = 0;
        if (*min  < 0 || *min  > 59) *min  = 0;
    } else if (vlen == 8) {
        *all_day = 1;
    }
}

// Strip HTML from a calendar description:
//   <br>, <br/>, <br /> → \n (iCal newline escape, two chars: backslash + n)
//   All other tags stripped, text content kept.
//   HTML entities decoded (&nbsp; &amp; &lt; &gt; &quot;).
//   Google separator lines (-::~:~ and ---) truncate the output.
static void clean_description(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (*r == '<') {
            // <br> variants → \n escape
            if ((r[1]=='b'||r[1]=='B') && (r[2]=='r'||r[2]=='R')) {
                *w++ = '\\'; *w++ = 'n';
            }
            // skip to end of tag
            while (*r && *r != '>') r++;
            if (*r) r++;
        } else if (*r == '&') {
            if      (strncmp(r, "&nbsp;",  6) == 0) { *w++ = ' ';  r += 6; }
            else if (strncmp(r, "&amp;",   5) == 0) { *w++ = '&';  r += 5; }
            else if (strncmp(r, "&lt;",    4) == 0) { *w++ = '<';  r += 4; }
            else if (strncmp(r, "&gt;",    4) == 0) { *w++ = '>';  r += 4; }
            else if (strncmp(r, "&quot;",  6) == 0) { *w++ = '"';  r += 6; }
            else if (strncmp(r, "&apos;",  6) == 0) { *w++ = '\''; r += 6; }
            else                                     { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';

    // Truncate at Google's internal separators
    char *sep;
    if ((sep = strstr(s, "-::~:~::")) != NULL) *sep = '\0';
    // Truncate at "\\n---\\n" (iCal newline + three dashes + iCal newline)
    if ((sep = strstr(s, "\\n---\\n")) != NULL) *sep = '\0';
    if ((sep = strstr(s, "\\n___"))    != NULL) *sep = '\0';

    // Unescape iCal backslash sequences: \, \; \\ \n
    char *r2 = s, *w2 = s;
    while (*r2) {
        if (r2[0] == '\\' && r2[1] != '\0') {
            switch (r2[1]) {
                case ',':  *w2++ = ',';  r2 += 2; break;
                case ';':  *w2++ = ';';  r2 += 2; break;
                case '\\': *w2++ = '\\'; r2 += 2; break;
                default:   *w2++ = *r2++; break;
            }
        } else {
            *w2++ = *r2++;
        }
    }
    *w2 = '\0';

    // Trim trailing \n escapes and spaces
    size_t n = strlen(s);
    while (n >= 2 && s[n-2] == '\\' && s[n-1] == 'n') n -= 2;
    while (n > 0  && (s[n-1] == ' ' || s[n-1] == '\t')) n--;
    s[n] = '\0';
}

static void xml_unescape(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '&') {
            if      (strncmp(r, "&amp;",  5) == 0) { *w++ = '&';  r += 5; }
            else if (strncmp(r, "&lt;",   4) == 0) { *w++ = '<';  r += 4; }
            else if (strncmp(r, "&gt;",   4) == 0) { *w++ = '>';  r += 4; }
            else if (strncmp(r, "&quot;", 6) == 0) { *w++ = '"';  r += 6; }
            else if (strncmp(r, "&apos;", 6) == 0) { *w++ = '\''; r += 6; }
            else { *w++ = *r++; }
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static void ical_prop_value(const char *line, char *out, size_t out_len) {
    const char *colon = strchr(line, ':');
    if (!colon) { out[0] = '\0'; return; }
    const char *val = colon + 1;
    size_t n = strlen(val);
    while (n > 0 && (val[n-1] == '\r' || val[n-1] == '\n' ||
                      val[n-1] == ' '  || val[n-1] == '\t'))
        n--;
    if (n >= out_len) n = out_len - 1;
    memcpy(out, val, n);
    out[n] = '\0';
    xml_unescape(out);
}

static void parse_vevents(const char *data, size_t data_len, CalEventList *out) {
    char *buf = malloc(data_len + 1);
    if (!buf) return;
    memcpy(buf, data, data_len);
    buf[data_len] = '\0';
    ical_unfold(buf, data_len);

    const char *p = buf;
    while ((p = strstr(p, "BEGIN:VEVENT")) != NULL) {
        p += 12;
        const char *end = strstr(p, "END:VEVENT");
        if (!end) break;

        size_t blen  = (size_t)(end - p);
        char  *block = malloc(blen + 1);
        if (!block) break;
        memcpy(block, p, blen);
        block[blen] = '\0';

        CalEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.hour = -1;

        char *ln = block, *nl;
        int   in_valarm = 0;
        while ((nl = strchr(ln, '\n')) != NULL) {
            *nl = '\0';
            if      (strncmp(ln, "BEGIN:VALARM", 12) == 0) { in_valarm = 1; }
            else if (strncmp(ln, "END:VALARM",   10) == 0) { in_valarm = 0; }
            else {
                if      (strncmp(ln, "UID:",         4) == 0) ical_prop_value(ln, ev.uid,         sizeof(ev.uid));
                else if (strncmp(ln, "SUMMARY:",     8) == 0) ical_prop_value(ln, ev.summary,     sizeof(ev.summary));
                else if (strncmp(ln, "LOCATION:",    9) == 0) ical_prop_value(ln, ev.location,    sizeof(ev.location));
                else if (!in_valarm && strncmp(ln, "DESCRIPTION:", 12) == 0) {
                    ical_prop_value(ln, ev.description, sizeof(ev.description));
                    clean_description(ev.description);
                }
                else if (strncmp(ln, "DTSTART",      7) == 0)
                    parse_ical_dt(ln, ev.date, &ev.hour, &ev.minute, &ev.all_day);
                else if (strncmp(ln, "DTEND",        5) == 0) ical_prop_value(ln, ev.dtend, sizeof(ev.dtend));
            }
            ln = nl + 1;
        }
        free(block);

        if (ev.date[0] != '\0')
            event_append(out, &ev);

        p = end + 10;
    }
    free(buf);

    if (out->count > 1)
        qsort(out->events, out->count, sizeof(CalEvent), event_cmp);
}

// ============================================================================
// CalDAV autodiscovery (RFC 6764 / RFC 4791)
// ============================================================================

static int caldav_discover(CalDavConn *conn) {
    char auth[512];
    make_basic_auth(conn->username, conn->password, auth, sizeof(auth));

    // Step 1: find current-user-principal
    static const char propfind1[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\">"
        "<D:prop><D:current-user-principal/></D:prop>"
        "</D:propfind>";

    int status = 0; size_t rlen = 0;
    char *resp = http_do(conn->server, conn->port,
                          "PROPFIND", conn->path, auth, "0",
                          propfind1, sizeof(propfind1) - 1, &status, &rlen);
    if (!resp) return -1;

    char principal[512] = "";
    xml_get_nested(resp, "current-user-principal", "href",
                    principal, sizeof(principal));
    free(resp);
    if (!principal[0]) return -1;

    // Step 2: find calendar-home-set from principal
    static const char propfind2[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<D:propfind xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
        "<D:prop><C:calendar-home-set/></D:prop>"
        "</D:propfind>";

    rlen = 0;
    resp = http_do(conn->server, conn->port,
                    "PROPFIND", principal, auth, "0",
                    propfind2, sizeof(propfind2) - 1, &status, &rlen);
    if (!resp) return -1;

    char home[512] = "";
    xml_get_nested(resp, "calendar-home-set", "href", home, sizeof(home));
    free(resp);
    if (!home[0]) return -1;

    strncpy(conn->path, home, sizeof(conn->path) - 1);
    return 0;
}

// ============================================================================
// Public API
// ============================================================================

int caldav_fetch_month(CalDavConn *conn, int year, int month, CalEventList *out) {
    if (!conn || !conn->server[0]) return -1;

    cal_event_list_init(out);

    char dtstart[9], dtend[9];
    snprintf(dtstart, sizeof(dtstart), "%04d%02d01", year, month);
    int ny = (month == 12) ? year + 1 : year;
    int nm = (month == 12) ? 1 : month + 1;
    snprintf(dtend, sizeof(dtend), "%04d%02d01", ny, nm);

    char xml[1024];
    snprintf(xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>"
        "<C:calendar-query xmlns:D=\"DAV:\" "
        "xmlns:C=\"urn:ietf:params:xml:ns:caldav\">"
        "<D:prop><D:getetag/><C:calendar-data/></D:prop>"
        "<C:filter>"
        "<C:comp-filter name=\"VCALENDAR\">"
        "<C:comp-filter name=\"VEVENT\">"
        "<C:time-range start=\"%sT000000Z\" end=\"%sT000000Z\"/>"
        "</C:comp-filter></C:comp-filter>"
        "</C:filter>"
        "</C:calendar-query>",
        dtstart, dtend);

    char auth[512];
    make_basic_auth(conn->username, conn->password, auth, sizeof(auth));

    int status = 0; size_t rlen = 0;
    char *resp = http_do(conn->server, conn->port,
                          "REPORT", conn->path, auth, "1",
                          xml, strlen(xml), &status, &rlen);

    if (!resp || (status != 207 && status != 200)) {
        int first_status = status;
        free(resp);
        // REPORT failed — attempt autodiscovery and retry
        if (caldav_discover(conn) != 0)
            return first_status ? first_status : -1;
        rlen = 0;
        resp = http_do(conn->server, conn->port,
                        "REPORT", conn->path, auth, "1",
                        xml, strlen(xml), &status, &rlen);
        if (!resp || (status != 207 && status != 200)) {
            free(resp);
            return status ? status : -1;
        }
    }

    parse_vevents(resp, rlen, out);
    free(resp);
    return 0;
}
