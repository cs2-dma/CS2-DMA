#ifndef LIBSSH2_PACKET_H
#define LIBSSH2_PACKET_H


int _libssh2_packet_read(LIBSSH2_SESSION * session);

int _libssh2_packet_ask(LIBSSH2_SESSION * session, unsigned char packet_type,
                        unsigned char **data, size_t *data_len,
                        int match_ofs,
                        const unsigned char *match_buf,
                        size_t match_len);

int _libssh2_packet_askv(LIBSSH2_SESSION * session,
                         const unsigned char *packet_types,
                         unsigned char **data, size_t *data_len,
                         int match_ofs,
                         const unsigned char *match_buf,
                         size_t match_len);
int _libssh2_packet_require(LIBSSH2_SESSION * session,
                            unsigned char packet_type, unsigned char **data,
                            size_t *data_len, int match_ofs,
                            const unsigned char *match_buf,
                            size_t match_len,
                            packet_require_state_t * state);
int _libssh2_packet_requirev(LIBSSH2_SESSION *session,
                             const unsigned char *packet_types,
                             unsigned char **data, size_t *data_len,
                             int match_ofs,
                             const unsigned char *match_buf,
                             size_t match_len,
                             packet_requirev_state_t * state);
int _libssh2_packet_burn(LIBSSH2_SESSION * session,
                         libssh2_nonblocking_states * state);
int _libssh2_packet_write(LIBSSH2_SESSION * session, unsigned char *data,
                          unsigned long data_len);
int _libssh2_packet_add(LIBSSH2_SESSION * session, unsigned char *data,
                        size_t datalen, int macstate, uint32_t seq);

#endif 
