#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <psa/crypto.h>
#include "sync/sync.h"
#include "core/app_state.h"
#include "cache/cache.h"
#include "imap/imap.h"

struct SyncContext {
    struct AppState    *state;
    pthread_t           thread;
    pthread_mutex_t     mutex;
    pthread_cond_t      cond;
    _Atomic int         requested;
    _Atomic int         quit;
    _Atomic SyncStatus  status;
    _Atomic int         needs_reload;
};

// ---------------------------------------------------------------------------
// TLS + IMAP helpers (stack-local connection, same pattern as session.c)
// ---------------------------------------------------------------------------

static int sync_tls_connect(ImapConnection *conn,
                             const char *server, const char *port) {
    psa_crypto_init();

    mbedtls_net_init(&conn->net_ctx);
    mbedtls_ssl_init(&conn->ssl_ctx);
    mbedtls_ssl_config_init(&conn->ssl_conf);
    mbedtls_x509_crt_init(&conn->crt);
    conn->tag_counter = 0;

    int ret = mbedtls_net_connect(&conn->net_ctx, server, port,
                                  MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) return ret;

    ret = mbedtls_ssl_config_defaults(&conn->ssl_conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return ret;

    mbedtls_ssl_conf_authmode(&conn->ssl_conf, MBEDTLS_SSL_VERIFY_NONE);

    ret = mbedtls_ssl_setup(&conn->ssl_ctx, &conn->ssl_conf);
    if (ret != 0) return ret;

    mbedtls_ssl_set_hostname(&conn->ssl_ctx, server);
    mbedtls_ssl_set_bio(&conn->ssl_ctx, &conn->net_ctx,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    ret = mbedtls_ssl_handshake(&conn->ssl_ctx);
    if (ret != 0) return ret;

    // Read and discard server greeting
    char greeting[512];
    int n = imap_recv(conn, greeting, sizeof(greeting));
    if (n < 0) return n;

    return 0;
}

static void sync_tls_disconnect(ImapConnection *conn) {
    mbedtls_ssl_close_notify(&conn->ssl_ctx);
    mbedtls_net_free(&conn->net_ctx);
    mbedtls_ssl_free(&conn->ssl_ctx);
    mbedtls_ssl_config_free(&conn->ssl_conf);
    mbedtls_x509_crt_free(&conn->crt);
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

static void *sync_worker(void *arg) {
    SyncContext *ctx = (SyncContext *)arg;
    AppState    *state = ctx->state;

    while (!atomic_load(&ctx->quit)) {
        // Wait until a sync is requested
        pthread_mutex_lock(&ctx->mutex);
        while (!atomic_load(&ctx->requested) && !atomic_load(&ctx->quit)) {
            pthread_cond_wait(&ctx->cond, &ctx->mutex);
        }
        if (atomic_load(&ctx->quit)) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        atomic_store(&ctx->requested, 0);
        pthread_mutex_unlock(&ctx->mutex);

        // --- Connect ---
        atomic_store(&ctx->status, SYNC_CONNECTING);

        ImapConnection conn;
        memset(&conn, 0, sizeof(conn));

        if (sync_tls_connect(&conn,
                             state->config.imap_server,
                             state->config.imap_port) != 0) {
            atomic_store(&ctx->status, SYNC_ERROR);
            continue;
        }

        // --- Login ---
        if (imap_login(&conn,
                       state->config.username,
                       state->config.password) != 0) {
            sync_tls_disconnect(&conn);
            atomic_store(&ctx->status, SYNC_ERROR);
            continue;
        }

        // --- SELECT INBOX ---
        int exists = 0;
        if (imap_select(&conn, "INBOX", &exists) != 0 || exists == 0) {
            imap_logout(&conn);
            sync_tls_disconnect(&conn);
            atomic_store(&ctx->status, exists == 0 ? SYNC_DONE : SYNC_ERROR);
            continue;
        }

        // --- Fetch new headers since last_uid ---
        atomic_store(&ctx->status, SYNC_FETCHING);

        uint32_t last_uid = 0;
        cache_get_last_uid(state->cache, &last_uid);

        // Fetch the most recent 200 messages (seq-based, same as original main.c)
        int fetch_count = exists < 200 ? exists : 200;
        int start = exists - fetch_count + 1;

        MessageList new_msgs;
        message_list_init(&new_msgs);

        int ret = imap_fetch_headers(&conn, start, exists, &new_msgs);

        if (ret == 0 && new_msgs.count > 0) {
            cache_save_headers(state->cache, &new_msgs);
            atomic_store(&ctx->needs_reload, 1);
        }

        message_list_free(&new_msgs);

        // --- Disconnect ---
        imap_logout(&conn);
        sync_tls_disconnect(&conn);

        atomic_store(&ctx->status, SYNC_DONE);
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SyncContext *sync_create(struct AppState *state) {
    SyncContext *ctx = malloc(sizeof(SyncContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(*ctx));

    ctx->state = state;
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond, NULL);
    atomic_store(&ctx->requested,    0);
    atomic_store(&ctx->quit,         0);
    atomic_store(&ctx->status,       SYNC_IDLE);
    atomic_store(&ctx->needs_reload, 0);

    return ctx;
}

void sync_start(SyncContext *ctx) {
    pthread_create(&ctx->thread, NULL, sync_worker, ctx);
}

SyncStatus sync_status(SyncContext *ctx) {
    return (SyncStatus)atomic_load(&ctx->status);
}

void sync_request(SyncContext *ctx) {
    pthread_mutex_lock(&ctx->mutex);
    atomic_store(&ctx->requested, 1);
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
}

int sync_needs_reload(SyncContext *ctx) {
    return atomic_load(&ctx->needs_reload);
}

void sync_clear_reload(SyncContext *ctx) {
    atomic_store(&ctx->needs_reload, 0);
}

void sync_destroy(SyncContext *ctx) {
    if (!ctx) return;

    // Signal worker to quit
    pthread_mutex_lock(&ctx->mutex);
    atomic_store(&ctx->quit, 1);
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    pthread_join(ctx->thread, NULL);
    pthread_mutex_destroy(&ctx->mutex);
    pthread_cond_destroy(&ctx->cond);
    free(ctx);
}
