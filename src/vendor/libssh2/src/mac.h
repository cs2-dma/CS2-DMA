#ifndef LIBSSH2_MAC_H
#define LIBSSH2_MAC_H


#include "libssh2_priv.h"

struct _LIBSSH2_MAC_METHOD
{
    const char *name;

    
    int mac_len;

    
    int key_len;

    
    int (*init) (LIBSSH2_SESSION * session, unsigned char *key, int *free_key,
                 void **abstract);
    int (*hash) (LIBSSH2_SESSION * session, unsigned char *buf,
                 uint32_t seqno, const unsigned char *packet,
                 size_t packet_len, const unsigned char *addtl,
                 size_t addtl_len, void **abstract);
    int (*dtor) (LIBSSH2_SESSION * session, void **abstract);

    int etm; 
};

typedef struct _LIBSSH2_MAC_METHOD LIBSSH2_MAC_METHOD;

const LIBSSH2_MAC_METHOD **_libssh2_mac_methods(void);
const LIBSSH2_MAC_METHOD *_libssh2_mac_override(
        const LIBSSH2_CRYPT_METHOD *crypt);

#endif 
