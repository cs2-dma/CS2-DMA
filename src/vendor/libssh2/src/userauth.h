#ifndef LIBSSH2_USERAUTH_H
#define LIBSSH2_USERAUTH_H


int
_libssh2_userauth_publickey(LIBSSH2_SESSION *session,
                            const char *username,
                            size_t username_len,
                            const unsigned char *pubkeydata,
                            size_t pubkeydata_len,
                            LIBSSH2_USERAUTH_PUBLICKEY_SIGN_FUNC
                                ((*sign_callback)),
                            void *abstract);

#endif 
