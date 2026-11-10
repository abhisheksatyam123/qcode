#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// C boundary around an httplib client compiled against prefixed BoringSSL.
// The main engine links system OpenSSL for every other provider, so this
// transport deliberately exposes no OpenSSL/httplib types.

typedef struct QcodeZenClient QcodeZenClient;

typedef struct QcodeZenHeader {
  const char* name;
  const char* value;
} QcodeZenHeader;

// Return non-zero from a callback to continue receiving.
typedef int (*QcodeZenResponseCallback)(void* user, int status);
typedef int (*QcodeZenHeaderCallback)(void* user, const char* name,
                                      const char* value);
typedef int (*QcodeZenContentCallback)(void* user, const char* data,
                                       size_t length, uint64_t offset,
                                       uint64_t total);

// Values mirror httplib::Error so the adapter can pass them through.
typedef enum QcodeZenError {
  QCODE_ZEN_SUCCESS = 0,
  QCODE_ZEN_UNKNOWN = 1,
  QCODE_ZEN_CONNECTION = 2,
  QCODE_ZEN_BIND_IP_ADDRESS = 3,
  QCODE_ZEN_READ = 4,
  QCODE_ZEN_WRITE = 5,
  QCODE_ZEN_EXCEED_REDIRECT_COUNT = 6,
  QCODE_ZEN_CANCELED = 7,
  QCODE_ZEN_SSL_CONNECTION = 8,
  QCODE_ZEN_SSL_LOADING_CERTS = 9,
  QCODE_ZEN_SSL_SERVER_VERIFICATION = 10,
  QCODE_ZEN_SSL_SERVER_HOSTNAME_VERIFICATION = 11,
  QCODE_ZEN_UNSUPPORTED_MULTIPART_BOUNDARY_CHARS = 12,
  QCODE_ZEN_COMPRESSION = 13,
  QCODE_ZEN_CONNECTION_TIMEOUT = 14,
  QCODE_ZEN_PROXY_CONNECTION = 15,
} QcodeZenError;

QcodeZenClient* qcode_zen_client_new(const char* host, int port,
                                     int verify_ssl_cert,
                                     int connection_timeout_sec,
                                     int read_timeout_sec,
                                     int address_family);
void qcode_zen_client_free(QcodeZenClient* client);
void qcode_zen_client_stop(QcodeZenClient* client);
void qcode_zen_client_set_timeouts(QcodeZenClient* client,
                                   int connection_timeout_sec,
                                   int read_timeout_sec);
void qcode_zen_client_set_max_timeout(QcodeZenClient* client, int64_t msec);
void qcode_zen_client_set_keep_alive(QcodeZenClient* client, int on);

// Performs one HTTP request. `out_status` is set whenever a response was
// received, including non-2xx responses. Header and body callbacks run on the
// calling thread.
QcodeZenError qcode_zen_client_send(
    QcodeZenClient* client, const char* method, const char* path,
    const QcodeZenHeader* headers, size_t header_count, const char* body,
    size_t body_length, const char* content_type, int* out_status,
    QcodeZenResponseCallback response_cb, QcodeZenHeaderCallback header_cb,
    QcodeZenContentCallback content_cb, void* user, char* error_buf,
    size_t error_buf_size);

#ifdef __cplusplus
}
#endif
