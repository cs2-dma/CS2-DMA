

#include "libssh2_priv.h"

LIBSSH2_API
const char *libssh2_version(int req_version_num)
{
    if(req_version_num <= LIBSSH2_VERSION_NUM)
        return LIBSSH2_VERSION;
    return NULL; 
}

LIBSSH2_API
libssh2_crypto_engine_t libssh2_crypto_engine(void)
{
    return LIBSSH2_CRYPTO_ENGINE;
}
