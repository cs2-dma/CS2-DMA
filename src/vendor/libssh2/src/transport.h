#ifndef LIBSSH2_TRANSPORT_H
#define LIBSSH2_TRANSPORT_H


#include "libssh2_priv.h"
#include "packet.h"


int _libssh2_transport_send(LIBSSH2_SESSION *session,
                            const unsigned char *data, size_t data_len,
                            const unsigned char *data2, size_t data2_len);




int _libssh2_transport_read(LIBSSH2_SESSION * session);

#endif 
