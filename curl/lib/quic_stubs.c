/*
 * Stub implementations for ngtcp2, ngtcp2_crypto_quictls, and nghttp3.
 * These are required because libcurl was built with HTTP/3 (QUIC) support,
 * but we only use FTP/SFTP so the QUIC code paths are never reached.
 * All stubs return 0 / NULL — safe because they are never called at runtime.
 */

#include <stddef.h>
#include <stdint.h>

/* ── ngtcp2 stubs ─────────────────────────────────────────────────────────── */

const void *ngtcp2_version(int x) { (void)x; return NULL; }
void  ngtcp2_path_storage_zero(void *p) { (void)p; }
int   ngtcp2_conn_get_handshake_completed(void *c) { (void)c; return 0; }
int   ngtcp2_conn_in_draining_period(void *c) { (void)c; return 0; }
void  ngtcp2_conn_get_ccerr(void *c, void *e) { (void)c; (void)e; }
void  ngtcp2_settings_default_versioned(int v, void *s) { (void)v; (void)s; }
void  ngtcp2_transport_params_default_versioned(int v, void *p) { (void)v; (void)p; }
void  ngtcp2_addr_init(void *a, const void *sa, size_t salen) { (void)a; (void)sa; (void)salen; }
int   ngtcp2_conn_client_new_versioned(void **c, const void *dcid, const void *scid,
          const void *path, int version, int cbver, const void *cbs,
          const void *settings, const void *params, const void *mem, void *user_data)
      { (void)c; (void)dcid; (void)scid; (void)path; (void)version; (void)cbver;
        (void)cbs; (void)settings; (void)params; (void)mem; (void)user_data; return -1; }
void  ngtcp2_conn_set_tls_native_handle(void *c, void *tls) { (void)c; (void)tls; }
void  ngtcp2_ccerr_default(void *e) { (void)e; }
void  ngtcp2_conn_del(void *c) { (void)c; }
int   ngtcp2_conn_write_connection_close_versioned(void *c, void *path, int piv,
          void *pi, uint8_t *dest, size_t destlen, const void *ccerr,
          uint64_t ts)
      { (void)c; (void)path; (void)piv; (void)pi; (void)dest; (void)destlen;
        (void)ccerr; (void)ts; return 0; }
uint64_t ngtcp2_conn_get_cwnd_left(void *c) { (void)c; return 0; }
uint64_t ngtcp2_conn_get_max_data_left(void *c) { (void)c; return 0; }
int   ngtcp2_conn_open_bidi_stream(void *c, int64_t *sid, void *ud) { (void)c; (void)sid; (void)ud; return -1; }
void *ngtcp2_conn_get_stream_user_data(void *c, int64_t sid) { (void)c; (void)sid; return NULL; }
const void *ngtcp2_conn_get_remote_transport_params(void *c) { (void)c; return NULL; }
size_t ngtcp2_conn_get_max_tx_udp_payload_size(void *c) { (void)c; return 0; }
size_t ngtcp2_conn_get_path_max_tx_udp_payload_size(void *c) { (void)c; return 0; }
uint64_t ngtcp2_conn_get_send_quantum(void *c) { (void)c; return 0; }
int   ngtcp2_conn_writev_stream_versioned(void *c, void *path, int piv, void *pi,
          uint8_t *dest, size_t destlen, int64_t *pdatalen, uint32_t flags,
          int64_t stream_id, const void *datav, size_t datavcnt, uint64_t ts)
      { (void)c; (void)path; (void)piv; (void)pi; (void)dest; (void)destlen;
        (void)pdatalen; (void)flags; (void)stream_id; (void)datav; (void)datavcnt;
        (void)ts; return 0; }
void  ngtcp2_ccerr_set_application_error(void *e, uint64_t ec, const uint8_t *r, size_t rlen)
      { (void)e; (void)ec; (void)r; (void)rlen; }
const char *ngtcp2_strerror(int err) { (void)err; return ""; }
uint8_t ngtcp2_conn_get_tls_alert(void *c) { (void)c; return 0; }
void  ngtcp2_ccerr_set_tls_alert(void *e, uint8_t alert, const uint8_t *r, size_t rlen)
      { (void)e; (void)alert; (void)r; (void)rlen; }
void  ngtcp2_conn_update_pkt_tx_time(void *c, uint64_t ts) { (void)c; (void)ts; }
void  ngtcp2_ccerr_set_liberr(void *e, int liberr, const uint8_t *r, size_t rlen)
      { (void)e; (void)liberr; (void)r; (void)rlen; }
uint64_t ngtcp2_conn_get_expiry(void *c) { (void)c; return UINT64_MAX; }
int   ngtcp2_conn_handle_expiry(void *c, uint64_t ts) { (void)c; (void)ts; return 0; }
int   ngtcp2_conn_encode_0rtt_transport_params(void *c, uint8_t *dest, size_t *pdestlen)
      { (void)c; (void)dest; (void)pdestlen; return -1; }
int   ngtcp2_conn_decode_and_set_0rtt_transport_params(void *c, const uint8_t *data, size_t datalen)
      { (void)c; (void)data; (void)datalen; return -1; }
void  ngtcp2_conn_extend_max_stream_offset(void *c, int64_t sid, uint64_t ndatalen)
      { (void)c; (void)sid; (void)ndatalen; }
void  ngtcp2_conn_extend_max_offset(void *c, uint64_t ndatalen)
      { (void)c; (void)ndatalen; }
int   ngtcp2_conn_open_uni_stream(void *c, int64_t *sid, void *ud)
      { (void)c; (void)sid; (void)ud; return -1; }
uint64_t ngtcp2_conn_get_streams_uni_left(void *c) { (void)c; return 0; }
int   ngtcp2_conn_set_stream_user_data(void *c, int64_t sid, void *ud)
      { (void)c; (void)sid; (void)ud; return 0; }
int   ngtcp2_conn_shutdown_stream(void *c, int flags, int64_t sid, uint64_t app_err_code)
      { (void)c; (void)flags; (void)sid; (void)app_err_code; return 0; }
int   ngtcp2_conn_shutdown_stream_read(void *c, int flags, int64_t sid, uint64_t app_err_code)
      { (void)c; (void)flags; (void)sid; (void)app_err_code; return 0; }
int   ngtcp2_conn_shutdown_stream_write(void *c, int flags, int64_t sid, uint64_t app_err_code)
      { (void)c; (void)flags; (void)sid; (void)app_err_code; return 0; }
int   ngtcp2_conn_read_pkt_versioned(void *c, const void *path, int piv, const void *pi,
          const uint8_t *pkt, size_t pktlen, uint64_t ts)
      { (void)c; (void)path; (void)piv; (void)pi; (void)pkt; (void)pktlen; (void)ts; return 0; }
void  ngtcp2_conn_set_keep_alive_timeout(void *c, uint64_t timeout) { (void)c; (void)timeout; }

/* ── ngtcp2_crypto_quictls stubs ─────────────────────────────────────────── */

int   ngtcp2_crypto_quictls_configure_client_context(void *ssl_ctx)
      { (void)ssl_ctx; return -1; }

/* Callback function-pointer stubs — these must be function symbols */
int ngtcp2_crypto_client_initial_cb(void *c, void *ud)
    { (void)c; (void)ud; return -1; }
int ngtcp2_crypto_recv_crypto_data_cb(void *c, int level, uint64_t offset,
        const uint8_t *data, size_t datalen, void *ud)
    { (void)c; (void)level; (void)offset; (void)data; (void)datalen; (void)ud; return -1; }
int ngtcp2_crypto_encrypt_cb(uint8_t *dest, const void *aead, const void *aead_ctx,
        const uint8_t *plaintext, size_t plaintextlen, const uint8_t *nonce,
        size_t noncelen, const uint8_t *aad, size_t aadlen)
    { (void)dest; (void)aead; (void)aead_ctx; (void)plaintext; (void)plaintextlen;
      (void)nonce; (void)noncelen; (void)aad; (void)aadlen; return -1; }
int ngtcp2_crypto_decrypt_cb(uint8_t *dest, const void *aead, const void *aead_ctx,
        const uint8_t *ciphertext, size_t ciphertextlen, const uint8_t *nonce,
        size_t noncelen, const uint8_t *aad, size_t aadlen)
    { (void)dest; (void)aead; (void)aead_ctx; (void)ciphertext; (void)ciphertextlen;
      (void)nonce; (void)noncelen; (void)aad; (void)aadlen; return -1; }
int ngtcp2_crypto_hp_mask_cb(uint8_t *dest, const void *hp, const void *hp_ctx,
        const uint8_t *sample)
    { (void)dest; (void)hp; (void)hp_ctx; (void)sample; return -1; }
int ngtcp2_crypto_recv_retry_cb(void *c, const void *hd, void *ud)
    { (void)c; (void)hd; (void)ud; return -1; }
int ngtcp2_crypto_update_key_cb(void *c, uint8_t *rx_secret, uint8_t *tx_secret,
        void *rx_aead_ctx, uint8_t *rx_iv, void *tx_aead_ctx, uint8_t *tx_iv,
        const uint8_t *current_rx_secret, const uint8_t *current_tx_secret,
        size_t secretlen, void *ud)
    { (void)c; (void)rx_secret; (void)tx_secret; (void)rx_aead_ctx; (void)rx_iv;
      (void)tx_aead_ctx; (void)tx_iv; (void)current_rx_secret; (void)current_tx_secret;
      (void)secretlen; (void)ud; return -1; }
void ngtcp2_crypto_delete_crypto_aead_ctx_cb(void *c, void *aead_ctx, void *ud)
    { (void)c; (void)aead_ctx; (void)ud; }
void ngtcp2_crypto_delete_crypto_cipher_ctx_cb(void *c, void *cipher_ctx, void *ud)
    { (void)c; (void)cipher_ctx; (void)ud; }
int  ngtcp2_crypto_get_path_challenge_data_cb(void *c, uint8_t *data, void *ud)
    { (void)c; (void)data; (void)ud; return -1; }
int  ngtcp2_crypto_get_path_challenge_data2_cb(void *c, uint8_t *data, void *ud)
    { (void)c; (void)data; (void)ud; return -1; }

/* ── nghttp3 stubs ────────────────────────────────────────────────────────── */

const void *nghttp3_version(int x) { (void)x; return NULL; }
void  nghttp3_conn_del(void *c) { (void)c; }
int   nghttp3_conn_resume_stream(void *c, int64_t sid) { (void)c; (void)sid; return 0; }
const char *nghttp3_strerror(int err) { (void)err; return ""; }
uint64_t nghttp3_err_infer_quic_app_error_code(int err) { (void)err; return 0; }
int   nghttp3_conn_read_stream(void *c, int64_t sid, const uint8_t *data, size_t datalen,
          int fin)
      { (void)c; (void)sid; (void)data; (void)datalen; (void)fin; return -1; }
int   nghttp3_conn_add_ack_offset(void *c, int64_t sid, uint64_t n)
      { (void)c; (void)sid; (void)n; return 0; }
int   nghttp3_conn_close_stream(void *c, int64_t sid, uint64_t app_err)
      { (void)c; (void)sid; (void)app_err; return 0; }
int   nghttp3_conn_shutdown_stream_read(void *c, int64_t sid)
      { (void)c; (void)sid; return 0; }
int   nghttp3_conn_shutdown_stream_write(void *c, int64_t sid)
      { (void)c; (void)sid; return 0; }
int   nghttp3_conn_unblock_stream(void *c, int64_t sid) { (void)c; (void)sid; return 0; }
void  nghttp3_settings_default_versioned(int v, void *s) { (void)v; (void)s; }
int   nghttp3_conn_client_new_versioned(void **c, int csv, const void *cbs,
          int cbsv, const void *settings, const void *mem)
      { (void)c; (void)csv; (void)cbs; (void)cbsv; (void)settings; (void)mem; return -1; }
int   nghttp3_conn_bind_control_stream(void *c, int64_t sid)
      { (void)c; (void)sid; return 0; }
int   nghttp3_conn_bind_qpack_streams(void *c, int64_t qenc_sid, int64_t qdec_sid)
      { (void)c; (void)qenc_sid; (void)qdec_sid; return 0; }
int   nghttp3_conn_submit_request(void *c, int64_t sid, const void *nva, size_t nvlen,
          const void *dr, void *ud)
      { (void)c; (void)sid; (void)nva; (void)nvlen; (void)dr; (void)ud; return -1; }
int   nghttp3_conn_writev_stream(void *c, int64_t *psid, int *pfin, void *vec, size_t veccnt)
      { (void)c; (void)psid; (void)pfin; (void)vec; (void)veccnt; return 0; }
int   nghttp3_conn_block_stream(void *c, int64_t sid) { (void)c; (void)sid; return 0; }
int   nghttp3_conn_add_write_offset(void *c, int64_t sid, size_t n)
      { (void)c; (void)sid; (void)n; return 0; }
int   nghttp3_conn_set_stream_user_data(void *c, int64_t sid, void *ud)
      { (void)c; (void)sid; (void)ud; return 0; }
void  nghttp3_rcbuf_get_buf(const void *rcbuf, void *out) { (void)rcbuf; (void)out; }
