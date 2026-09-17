#ifndef LIBSSH2_CHANNEL_H
#define LIBSSH2_CHANNEL_H



int _libssh2_channel_receive_window_adjust(LIBSSH2_CHANNEL * channel,
                                           uint32_t adjustment,
                                           unsigned char force,
                                           unsigned int *store);


int _libssh2_channel_flush(LIBSSH2_CHANNEL *channel, int streamid);


int _libssh2_channel_free(LIBSSH2_CHANNEL *channel);

int
_libssh2_channel_extended_data(LIBSSH2_CHANNEL *channel, int ignore_mode);


ssize_t
_libssh2_channel_write(LIBSSH2_CHANNEL *channel, int stream_id,
                       const unsigned char *buf, size_t buflen);


LIBSSH2_CHANNEL *
_libssh2_channel_open(LIBSSH2_SESSION * session, const char *channel_type,
                      uint32_t channel_type_len,
                      uint32_t window_size,
                      uint32_t packet_size,
                      const unsigned char *message, size_t message_len);



int
_libssh2_channel_process_startup(LIBSSH2_CHANNEL *channel,
                                 const char *request, size_t request_len,
                                 const char *message, size_t message_len);


ssize_t _libssh2_channel_read(LIBSSH2_CHANNEL *channel, int stream_id,
                              char *buf, size_t buflen);

uint32_t _libssh2_channel_nextid(LIBSSH2_SESSION * session);

LIBSSH2_CHANNEL *_libssh2_channel_locate(LIBSSH2_SESSION * session,
                                         uint32_t channel_id);

size_t _libssh2_channel_packet_data_len(LIBSSH2_CHANNEL * channel,
                                        int stream_id);

int _libssh2_channel_close(LIBSSH2_CHANNEL * channel);


int _libssh2_channel_forward_cancel(LIBSSH2_LISTENER *listener);

#endif 
