#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include "cache/cache.h"

struct Cache {
    sqlite3 *db;
};

static int cache_exec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
    }
    return rc;
}

Cache *cache_open(const char *path) {
    Cache *c = malloc(sizeof(Cache));
    if (!c) return NULL;

    if (sqlite3_open(path, &c->db) != SQLITE_OK) {
        free(c);
        return NULL;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS messages ("
        "    uid        INTEGER PRIMARY KEY,"
        "    thread_id  INTEGER NOT NULL DEFAULT 0,"
        "    flags      INTEGER NOT NULL DEFAULT 0,"
        "    date       TEXT    NOT NULL DEFAULT '',"
        "    subject    TEXT    NOT NULL DEFAULT '',"
        "    from_name  TEXT    NOT NULL DEFAULT '',"
        "    from_addr  TEXT    NOT NULL DEFAULT '',"
        "    body       BLOB,"
        "    fetched_at INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_thread ON messages(thread_id);"
        "CREATE INDEX IF NOT EXISTS idx_date   ON messages(date DESC);"
        "CREATE TABLE IF NOT EXISTS meta ("
        "    key   TEXT PRIMARY KEY,"
        "    value TEXT NOT NULL"
        ");";

    if (cache_exec(c->db, schema) != SQLITE_OK) {
        sqlite3_close(c->db);
        free(c);
        return NULL;
    }

    return c;
}

void cache_close(Cache *c) {
    if (!c) return;
    sqlite3_close(c->db);
    free(c);
}

int cache_get_last_uid(Cache *c, uint32_t *out) {
    const char *sql = "SELECT value FROM meta WHERE key='last_uid'";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out = (uint32_t)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return 0;
    }
    sqlite3_finalize(stmt);
    *out = 0;
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int cache_save_headers(Cache *c, const MessageList *list) {
    if (!list->count) return 0;

    sqlite3_exec(c->db, "BEGIN", NULL, NULL, NULL);

    const char *sql =
        "INSERT OR REPLACE INTO messages"
        "(uid, thread_id, flags, date, subject, from_name, from_addr)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_exec(c->db, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    for (size_t i = 0; i < list->count; i++) {
        const MessageHeader *h = &list->headers[i];
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)h->uid);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)h->thread_id);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)h->flags);
        sqlite3_bind_text(stmt,  4, h->date,         -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  5, h->subject,      -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  6, h->from_name,    -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  7, h->from_address, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    // Update last_uid = MAX(uid)
    const char *meta_sql =
        "INSERT OR REPLACE INTO meta(key, value)"
        " VALUES('last_uid', (SELECT MAX(uid) FROM messages))";
    sqlite3_exec(c->db, meta_sql, NULL, NULL, NULL);

    sqlite3_exec(c->db, "COMMIT", NULL, NULL, NULL);
    return 0;
}

int cache_load_headers(Cache *c, MessageList *out) {
    const char *sql =
        "SELECT uid, thread_id, flags, date, subject, from_name, from_addr"
        " FROM messages ORDER BY date DESC";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    size_t cap = 256;
    size_t count = 0;
    MessageHeader *headers = malloc(cap * sizeof(MessageHeader));
    if (!headers) { sqlite3_finalize(stmt); return -1; }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= cap) {
            cap *= 2;
            MessageHeader *tmp = realloc(headers, cap * sizeof(MessageHeader));
            if (!tmp) { free(headers); sqlite3_finalize(stmt); return -1; }
            headers = tmp;
        }
        MessageHeader *h = &headers[count++];
        memset(h, 0, sizeof(*h));

        h->uid       = (uint32_t)sqlite3_column_int64(stmt, 0);
        h->thread_id = (uint64_t)sqlite3_column_int64(stmt, 1);
        h->flags     = (uint32_t)sqlite3_column_int64(stmt, 2);

        const char *date     = (const char *)sqlite3_column_text(stmt, 3);
        const char *subject  = (const char *)sqlite3_column_text(stmt, 4);
        const char *fname    = (const char *)sqlite3_column_text(stmt, 5);
        const char *faddr    = (const char *)sqlite3_column_text(stmt, 6);

        if (date)    strncpy(h->date,         date,    sizeof(h->date)         - 1);
        if (subject) strncpy(h->subject,      subject, sizeof(h->subject)      - 1);
        if (fname)   strncpy(h->from_name,    fname,   sizeof(h->from_name)    - 1);
        if (faddr)   strncpy(h->from_address, faddr,   sizeof(h->from_address) - 1);
    }
    sqlite3_finalize(stmt);

    out->headers = headers;
    out->count   = count;
    return 0;
}

int cache_has_body(Cache *c, uint32_t uid) {
    const char *sql =
        "SELECT COUNT(*) FROM messages WHERE uid=? AND body IS NOT NULL";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)uid);
    int result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0) > 0 ? 1 : 0;
    sqlite3_finalize(stmt);
    return result;
}

int cache_save_body(Cache *c, uint32_t uid, const char *body, size_t len) {
    const char *sql =
        "UPDATE messages SET body=?, fetched_at=? WHERE uid=?";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_blob(stmt,  1, body, (int)len, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)time(NULL));
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)uid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int cache_load_body(Cache *c, uint32_t uid, char **out, size_t *out_len) {
    const char *sql = "SELECT body FROM messages WHERE uid=?";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)uid);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) { sqlite3_finalize(stmt); return -1; }

    int bytes = sqlite3_column_bytes(stmt, 0);
    const void *blob = sqlite3_column_blob(stmt, 0);
    if (!blob || bytes <= 0) { sqlite3_finalize(stmt); return -1; }

    char *buf = malloc((size_t)bytes + 1);
    if (!buf) { sqlite3_finalize(stmt); return -1; }
    memcpy(buf, blob, (size_t)bytes);
    buf[bytes] = '\0';

    sqlite3_finalize(stmt);
    *out     = buf;
    *out_len = (size_t)bytes;
    return 0;
}

int cache_update_flags(Cache *c, uint32_t uid, uint32_t flags) {
    const char *sql = "UPDATE messages SET flags=? WHERE uid=?";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(c->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)flags);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)uid);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
