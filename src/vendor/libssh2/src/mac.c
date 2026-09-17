

#include "libssh2_priv.h"
#include "mac.h"

#if defined(LIBSSH2DEBUG) && defined(LIBSSH2_MAC_NONE_INSECURE)

static int
mac_none_MAC(LIBSSH2_SESSION * session, unsigned char *buf,
             uint32_t seqno, const unsigned char *packet,
             size_t packet_len, const unsigned char *addtl,
             size_t addtl_len, void **abstract)
{
    return 0;
}




static LIBSSH2_MAC_METHOD mac_method_none = {
    "none",
    0,
    0,
    NULL,
    mac_none_MAC,
    NULL,
    0
};
#endif 


static int
mac_method_common_init(LIBSSH2_SESSION * session, unsigned char *key,
                       int *free_key, void **abstract)
{
    *abstract = key;
    *free_key = 0;
    (void)session;

    return 0;
}




static int
mac_method_common_dtor(LIBSSH2_SESSION * session, void **abstract)
{
    if(*abstract) {
        LIBSSH2_FREE(session, *abstract);
    }
    *abstract = NULL;

    return 0;
}



#if LIBSSH2_HMAC_SHA512

static int
mac_method_hmac_sha2_512_hash(LIBSSH2_SESSION * session,
                              unsigned char *buf, uint32_t seqno,
                              const unsigned char *packet,
                              size_t packet_len,
                              const unsigned char *addtl,
                              size_t addtl_len, void **abstract)
{
    libssh2_hmac_ctx ctx;
    unsigned char seqno_buf[4];
    int res;
    (void)session;

    _libssh2_htonu32(seqno_buf, seqno);

    if(!_libssh2_hmac_ctx_init(&ctx))
        return 1;
    res = _libssh2_hmac_sha512_init(&ctx, *abstract, 64) &&
          _libssh2_hmac_update(&ctx, seqno_buf, 4) &&
          _libssh2_hmac_update(&ctx, packet, packet_len);
    if(res && addtl && addtl_len)
        res = _libssh2_hmac_update(&ctx, addtl, addtl_len);
    if(res)
        res = _libssh2_hmac_final(&ctx, buf);
    _libssh2_hmac_cleanup(&ctx);

    return !res;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_sha2_512 = {
    "hmac-sha2-512",
    64,
    64,
    mac_method_common_init,
    mac_method_hmac_sha2_512_hash,
    mac_method_common_dtor,
    0
};

static const LIBSSH2_MAC_METHOD mac_method_hmac_sha2_512_etm = {
    "hmac-sha2-512-etm@openssh.com",
    64,
    64,
    mac_method_common_init,
    mac_method_hmac_sha2_512_hash,
    mac_method_common_dtor,
    1
};

#endif



#if LIBSSH2_HMAC_SHA256

static int
mac_method_hmac_sha2_256_hash(LIBSSH2_SESSION * session,
                              unsigned char *buf, uint32_t seqno,
                              const unsigned char *packet,
                              size_t packet_len,
                              const unsigned char *addtl,
                              size_t addtl_len, void **abstract)
{
    libssh2_hmac_ctx ctx;
    unsigned char seqno_buf[4];
    int res;
    (void)session;

    _libssh2_htonu32(seqno_buf, seqno);

    if(!_libssh2_hmac_ctx_init(&ctx))
        return 1;
    res = _libssh2_hmac_sha256_init(&ctx, *abstract, 32) &&
          _libssh2_hmac_update(&ctx, seqno_buf, 4) &&
          _libssh2_hmac_update(&ctx, packet, packet_len);
    if(res && addtl && addtl_len)
        res = _libssh2_hmac_update(&ctx, addtl, addtl_len);
    if(res)
        res = _libssh2_hmac_final(&ctx, buf);
    _libssh2_hmac_cleanup(&ctx);

    return !res;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_sha2_256 = {
    "hmac-sha2-256",
    32,
    32,
    mac_method_common_init,
    mac_method_hmac_sha2_256_hash,
    mac_method_common_dtor,
    0
};

static const LIBSSH2_MAC_METHOD mac_method_hmac_sha2_256_etm = {
    "hmac-sha2-256-etm@openssh.com",
    32,
    32,
    mac_method_common_init,
    mac_method_hmac_sha2_256_hash,
    mac_method_common_dtor,
    1
};

#endif





static int
mac_method_hmac_sha1_hash(LIBSSH2_SESSION * session,
                          unsigned char *buf, uint32_t seqno,
                          const unsigned char *packet,
                          size_t packet_len,
                          const unsigned char *addtl,
                          size_t addtl_len, void **abstract)
{
    libssh2_hmac_ctx ctx;
    unsigned char seqno_buf[4];
    int res;
    (void)session;

    _libssh2_htonu32(seqno_buf, seqno);

    if(!_libssh2_hmac_ctx_init(&ctx))
        return 1;
    res = _libssh2_hmac_sha1_init(&ctx, *abstract, 20) &&
          _libssh2_hmac_update(&ctx, seqno_buf, 4) &&
          _libssh2_hmac_update(&ctx, packet, packet_len);
    if(res && addtl && addtl_len)
        res = _libssh2_hmac_update(&ctx, addtl, addtl_len);
    if(res)
        res = _libssh2_hmac_final(&ctx, buf);
    _libssh2_hmac_cleanup(&ctx);

    return !res;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_sha1 = {
    "hmac-sha1",
    20,
    20,
    mac_method_common_init,
    mac_method_hmac_sha1_hash,
    mac_method_common_dtor,
    0
};

static const LIBSSH2_MAC_METHOD mac_method_hmac_sha1_etm = {
    "hmac-sha1-etm@openssh.com",
    20,
    20,
    mac_method_common_init,
    mac_method_hmac_sha1_hash,
    mac_method_common_dtor,
    1
};


static int
mac_method_hmac_sha1_96_hash(LIBSSH2_SESSION * session,
                             unsigned char *buf, uint32_t seqno,
                             const unsigned char *packet,
                             size_t packet_len,
                             const unsigned char *addtl,
                             size_t addtl_len, void **abstract)
{
    unsigned char temp[SHA_DIGEST_LENGTH];

    if(mac_method_hmac_sha1_hash(session, temp, seqno, packet, packet_len,
                                 addtl, addtl_len, abstract))
        return 1;

    memcpy(buf, (char *) temp, 96 / 8);
    return 0;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_sha1_96 = {
    "hmac-sha1-96",
    12,
    20,
    mac_method_common_init,
    mac_method_hmac_sha1_96_hash,
    mac_method_common_dtor,
    0
};

#if LIBSSH2_MD5

static int
mac_method_hmac_md5_hash(LIBSSH2_SESSION * session, unsigned char *buf,
                         uint32_t seqno,
                         const unsigned char *packet,
                         size_t packet_len,
                         const unsigned char *addtl,
                         size_t addtl_len, void **abstract)
{
    libssh2_hmac_ctx ctx;
    unsigned char seqno_buf[4];
    int res;
    (void)session;

    _libssh2_htonu32(seqno_buf, seqno);

    if(!_libssh2_hmac_ctx_init(&ctx))
        return 1;
    res = _libssh2_hmac_md5_init(&ctx, *abstract, 16) &&
          _libssh2_hmac_update(&ctx, seqno_buf, 4) &&
          _libssh2_hmac_update(&ctx, packet, packet_len);
    if(res && addtl && addtl_len)
        res = _libssh2_hmac_update(&ctx, addtl, addtl_len);
    if(res)
        res = _libssh2_hmac_final(&ctx, buf);
    _libssh2_hmac_cleanup(&ctx);

    return !res;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_md5 = {
    "hmac-md5",
    16,
    16,
    mac_method_common_init,
    mac_method_hmac_md5_hash,
    mac_method_common_dtor,
    0
};


static int
mac_method_hmac_md5_96_hash(LIBSSH2_SESSION * session,
                            unsigned char *buf, uint32_t seqno,
                            const unsigned char *packet,
                            size_t packet_len,
                            const unsigned char *addtl,
                            size_t addtl_len, void **abstract)
{
    unsigned char temp[MD5_DIGEST_LENGTH];

    if(mac_method_hmac_md5_hash(session, temp, seqno, packet, packet_len,
                                addtl, addtl_len, abstract))
        return 1;

    memcpy(buf, (char *) temp, 96 / 8);
    return 0;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_md5_96 = {
    "hmac-md5-96",
    12,
    16,
    mac_method_common_init,
    mac_method_hmac_md5_96_hash,
    mac_method_common_dtor,
    0
};
#endif 

#if LIBSSH2_HMAC_RIPEMD

static int
mac_method_hmac_ripemd160_hash(LIBSSH2_SESSION * session,
                               unsigned char *buf, uint32_t seqno,
                               const unsigned char *packet,
                               size_t packet_len,
                               const unsigned char *addtl,
                               size_t addtl_len,
                               void **abstract)
{
    libssh2_hmac_ctx ctx;
    unsigned char seqno_buf[4];
    int res;
    (void)session;

    _libssh2_htonu32(seqno_buf, seqno);

    if(!_libssh2_hmac_ctx_init(&ctx))
        return 1;
    res = _libssh2_hmac_ripemd160_init(&ctx, *abstract, 20) &&
          _libssh2_hmac_update(&ctx, seqno_buf, 4) &&
          _libssh2_hmac_update(&ctx, packet, packet_len);
    if(res && addtl && addtl_len)
        res = _libssh2_hmac_update(&ctx, addtl, addtl_len);
    if(res)
        res = _libssh2_hmac_final(&ctx, buf);
    _libssh2_hmac_cleanup(&ctx);

    return !res;
}



static const LIBSSH2_MAC_METHOD mac_method_hmac_ripemd160 = {
    "hmac-ripemd160",
    20,
    20,
    mac_method_common_init,
    mac_method_hmac_ripemd160_hash,
    mac_method_common_dtor,
    0
};

static const LIBSSH2_MAC_METHOD mac_method_hmac_ripemd160_openssh_com = {
    "hmac-ripemd160@openssh.com",
    20,
    20,
    mac_method_common_init,
    mac_method_hmac_ripemd160_hash,
    mac_method_common_dtor,
    0
};
#endif 

static const LIBSSH2_MAC_METHOD *mac_methods[] = {
#if LIBSSH2_HMAC_SHA256
    &mac_method_hmac_sha2_256,
    &mac_method_hmac_sha2_256_etm,
#endif
#if LIBSSH2_HMAC_SHA512
    &mac_method_hmac_sha2_512,
    &mac_method_hmac_sha2_512_etm,
#endif
    &mac_method_hmac_sha1,
    &mac_method_hmac_sha1_etm,
    &mac_method_hmac_sha1_96,
#if LIBSSH2_MD5
    &mac_method_hmac_md5,
    &mac_method_hmac_md5_96,
#endif
#if LIBSSH2_HMAC_RIPEMD
    &mac_method_hmac_ripemd160,
    &mac_method_hmac_ripemd160_openssh_com,
#endif 
#if defined(LIBSSH2DEBUG) && defined(LIBSSH2_MAC_NONE_INSECURE)
    &mac_method_none,
#endif
    NULL
};

const LIBSSH2_MAC_METHOD **
_libssh2_mac_methods(void)
{
    return mac_methods;
}

#if LIBSSH2_AES_GCM
static int
mac_method_none_init(LIBSSH2_SESSION * session, unsigned char *key,
                     int *free_key, void **abstract)
{
    (void)session;
    (void)key;
    (void)free_key;
    (void)abstract;
    return 0;
}

static int
mac_method_hmac_none_hash(LIBSSH2_SESSION * session,
                          unsigned char *buf, uint32_t seqno,
                          const unsigned char *packet,
                          size_t packet_len,
                          const unsigned char *addtl,
                          size_t addtl_len, void **abstract)
{
    (void)session;
    (void)buf;
    (void)seqno;
    (void)packet;
    (void)packet_len;
    (void)addtl;
    (void)addtl_len;
    (void)abstract;
    return 0;
}

static int
mac_method_none_dtor(LIBSSH2_SESSION * session, void **abstract)
{
    (void)session;
    (void)abstract;
    return 0;
}


static const LIBSSH2_MAC_METHOD mac_method_hmac_aesgcm = {
    "INTEGRATED-AES-GCM",  
    16,
    16,
    mac_method_none_init,
    mac_method_hmac_none_hash,
    mac_method_none_dtor,
    0
};
#endif 


const LIBSSH2_MAC_METHOD *
_libssh2_mac_override(const LIBSSH2_CRYPT_METHOD *crypt)
{
#if LIBSSH2_AES_GCM
    if(!strcmp(crypt->name, "aes256-gcm@openssh.com") ||
       !strcmp(crypt->name, "aes128-gcm@openssh.com"))
        return &mac_method_hmac_aesgcm;
#else
    (void) crypt;
#endif 
    return NULL;
}
