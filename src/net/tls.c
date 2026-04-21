#include "net/tls.h"

int tls_connect(mbedtls_net_context *net, mbedtls_ssl_context *ssl,
                mbedtls_ssl_config  *conf,
                const char *server, const char *port) {
    mbedtls_net_init(net);
    mbedtls_ssl_init(ssl);
    mbedtls_ssl_config_init(conf);

    int ret = mbedtls_net_connect(net, server, port, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) return ret;

    ret = mbedtls_ssl_config_defaults(conf,
                                      MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return ret;

    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);

    ret = mbedtls_ssl_setup(ssl, conf);
    if (ret != 0) return ret;

    mbedtls_ssl_set_hostname(ssl, server);
    mbedtls_ssl_set_bio(ssl, net, mbedtls_net_send, mbedtls_net_recv, NULL);

    return mbedtls_ssl_handshake(ssl);
}

void tls_disconnect(mbedtls_net_context *net, mbedtls_ssl_context *ssl,
                    mbedtls_ssl_config  *conf, mbedtls_x509_crt *crt) {
    mbedtls_ssl_close_notify(ssl);
    mbedtls_net_free(net);
    mbedtls_ssl_free(ssl);
    mbedtls_ssl_config_free(conf);
    mbedtls_x509_crt_free(crt);
}
