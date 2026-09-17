

#include "libssh2_priv.h"

static int _libssh2_initialized = 0;
static int _libssh2_init_flags = 0;

LIBSSH2_API int
libssh2_init(int flags)
{
    if(_libssh2_initialized == 0 && !(flags & LIBSSH2_INIT_NO_CRYPTO)) {
        libssh2_crypto_init();
    }

    _libssh2_initialized++;
    _libssh2_init_flags |= flags;

    return 0;
}

LIBSSH2_API void
libssh2_exit(void)
{
    if(_libssh2_initialized == 0)
        return;

    _libssh2_initialized--;

    if(_libssh2_initialized == 0 &&
       !(_libssh2_init_flags & LIBSSH2_INIT_NO_CRYPTO)) {
        libssh2_crypto_exit();
    }

    return;
}

void
_libssh2_init_if_needed(void)
{
    if(_libssh2_initialized == 0)
        (void)libssh2_init(0);
}
