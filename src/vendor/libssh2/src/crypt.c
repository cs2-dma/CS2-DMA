

#include "libssh2_priv.h"
#include "cipher-chachapoly.h"

#include <assert.h>

#if defined(LIBSSH2DEBUG) && defined(LIBSSH2_CRYPT_NONE_INSECURE)

static int
crypt_none_crypt(LIBSSH2_SESSION * session,
                 unsigned int seqno,
                 unsigned char *buf,
                 size_t buf_len,
                 void **abstract,
                 int firstlast)
{
    
    return 0;
}

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_none = {
    "none",
    "DEK-Info: NONE",
    8,                
    0,                
    0,                
    0,                
    NULL,
    crypt_none_crypt,
    NULL
};
#endif 

struct crypt_ctx
{
    int encrypt;
    _libssh2_cipher_type(algo);
    _libssh2_cipher_ctx h;
    struct chachapoly_ctx chachapoly_ctx;
};

static int
crypt_init(LIBSSH2_SESSION * session,
           const LIBSSH2_CRYPT_METHOD * method,
           unsigned char *iv, int *free_iv,
           unsigned char *secret, int *free_secret,
           int encrypt, void **abstract)
{
    struct crypt_ctx *ctx = LIBSSH2_ALLOC(session,
                                          sizeof(struct crypt_ctx));
    if(!ctx)
        return LIBSSH2_ERROR_ALLOC;

    ctx->encrypt = encrypt;
    ctx->algo = method->algo;
    if(_libssh2_cipher_init(&ctx->h, ctx->algo, iv, secret, encrypt)) {
        LIBSSH2_FREE(session, ctx);
        return -1;
    }
    *abstract = ctx;
    *free_iv = 1;
    *free_secret = 1;
    return 0;
}

static int
crypt_encrypt(LIBSSH2_SESSION * session,
              unsigned int seqno,
              unsigned char *buf,
              size_t buf_len,
              void **abstract,
              int firstlast)
{
    struct crypt_ctx *cctx = *(struct crypt_ctx **) abstract;
    (void) session;
    (void) seqno;
    return _libssh2_cipher_crypt(&cctx->h, cctx->algo, cctx->encrypt, buf,
                                 buf_len, firstlast);
}

static int
crypt_dtor(LIBSSH2_SESSION * session, void **abstract)
{
    struct crypt_ctx **cctx = (struct crypt_ctx **) abstract;
    if(cctx && *cctx) {
        _libssh2_cipher_dtor(&(*cctx)->h);
        LIBSSH2_FREE(session, *cctx);
        *abstract = NULL;
    }
    return 0;
}

#if LIBSSH2_AES_GCM
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes256_gcm = {
    "aes256-gcm@openssh.com",
    "",
    16,                         
    12,                         
    32,                         
    16,                         
    LIBSSH2_CRYPT_FLAG_INTEGRATED_MAC | LIBSSH2_CRYPT_FLAG_PKTLEN_AAD,
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes256gcm
};

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes128_gcm = {
    "aes128-gcm@openssh.com",
    "",
    16,                         
    12,                         
    16,                         
    16,                         
    LIBSSH2_CRYPT_FLAG_INTEGRATED_MAC | LIBSSH2_CRYPT_FLAG_PKTLEN_AAD,
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes128gcm
};
#endif

#if LIBSSH2_AES_CTR
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes128_ctr = {
    "aes128-ctr",
    "",
    16,                         
    16,                         
    16,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes128ctr
};

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes192_ctr = {
    "aes192-ctr",
    "",
    16,                         
    16,                         
    24,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes192ctr
};

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes256_ctr = {
    "aes256-ctr",
    "",
    16,                         
    16,                         
    32,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes256ctr
};
#endif

#if LIBSSH2_AES_CBC
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes128_cbc = {
    "aes128-cbc",
    "DEK-Info: AES-128-CBC",
    16,                         
    16,                         
    16,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes128
};

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes192_cbc = {
    "aes192-cbc",
    "DEK-Info: AES-192-CBC",
    16,                         
    16,                         
    24,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes192
};

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_aes256_cbc = {
    "aes256-cbc",
    "DEK-Info: AES-256-CBC",
    16,                         
    16,                         
    32,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes256
};


static const LIBSSH2_CRYPT_METHOD
    libssh2_crypt_method_rijndael_cbc_lysator_liu_se = {
    "rijndael-cbc@lysator.liu.se",
    "DEK-Info: AES-256-CBC",
    16,                         
    16,                         
    32,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_aes256
};
#endif 

#if LIBSSH2_BLOWFISH
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_blowfish_cbc = {
    "blowfish-cbc",
    "",
    8,                          
    8,                          
    16,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_blowfish
};
#endif 

#if LIBSSH2_RC4
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_arcfour = {
    "arcfour",
    "DEK-Info: RC4",
    8,                          
    8,                          
    16,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_arcfour
};

static int
crypt_init_arcfour128(LIBSSH2_SESSION * session,
                      const LIBSSH2_CRYPT_METHOD * method,
                      unsigned char *iv, int *free_iv,
                      unsigned char *secret, int *free_secret,
                      int encrypt, void **abstract)
{
    int rc;

    rc = crypt_init(session, method, iv, free_iv, secret, free_secret,
                    encrypt, abstract);
    if(rc == 0) {
        struct crypt_ctx *cctx = *(struct crypt_ctx **) abstract;
        unsigned char block[8];
        size_t discard = 1536;
        for(; discard; discard -= 8)
            _libssh2_cipher_crypt(&cctx->h, cctx->algo, cctx->encrypt, block,
                                  method->blocksize, MIDDLE_BLOCK);
                               
    }

    return rc;
}

static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_arcfour128 = {
    "arcfour128",
    "",
    8,                          
    8,                          
    16,                         
    0,                          
    0,                          
    &crypt_init_arcfour128,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_arcfour
};
#endif 

#if LIBSSH2_CAST
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_cast128_cbc = {
    "cast128-cbc",
    "",
    8,                          
    8,                          
    16,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_cast5
};
#endif 

#if LIBSSH2_3DES
static const LIBSSH2_CRYPT_METHOD libssh2_crypt_method_3des_cbc = {
    "3des-cbc",
    "DEK-Info: DES-EDE3-CBC",
    8,                          
    8,                          
    24,                         
    0,                          
    0,                          
    &crypt_init,
    NULL,
    &crypt_encrypt,
    &crypt_dtor,
    _libssh2_cipher_3des
};
#endif

static int
crypt_init_chacha20_poly(LIBSSH2_SESSION * session,
           const LIBSSH2_CRYPT_METHOD * method,
           unsigned char *iv, int *free_iv,
           unsigned char *secret, int *free_secret,
           int encrypt, void **abstract)
{
    struct crypt_ctx *ctx = LIBSSH2_ALLOC(session,
                                          sizeof(struct crypt_ctx));

    (void)iv;

    if(!ctx)
        return LIBSSH2_ERROR_ALLOC;

    ctx->encrypt = encrypt;
    ctx->algo = method->algo;

    if(chachapoly_init(&ctx->chachapoly_ctx, secret, method->secret_len)) {
        LIBSSH2_FREE(session, ctx);
        return -1;
    }

    *abstract = ctx;
    *free_iv = 1;
    *free_secret = 1;
    return 0;
}


static int
crypt_encrypt_chacha20_poly_buffer(LIBSSH2_SESSION * session,
                                   unsigned int seqno,
                                   unsigned char *buf,
                                   size_t buf_len,
                                   void **abstract,
                                   int firstlast)
{
    int ret = 1;
    struct crypt_ctx *ctx = *(struct crypt_ctx **) abstract;

    (void)session;
    (void)firstlast;

    if(ctx) {
        if(ctx->encrypt) {
            
            ret = chachapoly_crypt(&ctx->chachapoly_ctx, seqno, buf, buf,
                                   ((u_int)buf_len) - 4, 4, ctx->encrypt);
        }
        else {
            
            ret = chachapoly_crypt(&ctx->chachapoly_ctx, seqno, buf, buf,
                                   ((u_int)buf_len), 4, ctx->encrypt);

            
            if(ret == 0) {
                memmove(buf, buf + 4, buf_len - 4);
            }
        }
    }

    return (ret == 0 ? 0 : 1);
}

static int
crypt_get_length_chacha20_poly(LIBSSH2_SESSION * session, unsigned int seqno,
                               unsigned char *data, size_t data_size,
                               unsigned int *len, void **abstract)
{
    struct crypt_ctx *ctx = *(struct crypt_ctx **) abstract;

    (void)session;

    return chachapoly_get_length(&ctx->chachapoly_ctx, len, seqno, data,
                                 (u_int)data_size);
}

static int
crypt_dtor_chacha20_poly(LIBSSH2_SESSION * session, void **abstract)
{
    struct crypt_ctx **cctx = (struct crypt_ctx **) abstract;
    if(cctx && *cctx) {
        LIBSSH2_FREE(session, *cctx);
        *abstract = NULL;
    }
    return 0;
}

static const LIBSSH2_CRYPT_METHOD
    libssh2_crypt_method_chacha20_poly1305_openssh = {
    "chacha20-poly1305@openssh.com",
    "",
    8,                                          
    0,                                          
    64,                                         
    16,                                         
    LIBSSH2_CRYPT_FLAG_REQUIRES_FULL_PACKET,    
    &crypt_init_chacha20_poly,
    &crypt_get_length_chacha20_poly,
    &crypt_encrypt_chacha20_poly_buffer,
    &crypt_dtor_chacha20_poly,
    _libssh2_cipher_chacha20                    
};


static const LIBSSH2_CRYPT_METHOD *_libssh2_crypt_methods[] = {
    &libssh2_crypt_method_chacha20_poly1305_openssh,
#if LIBSSH2_AES_GCM
    &libssh2_crypt_method_aes256_gcm,
    &libssh2_crypt_method_aes128_gcm,
#endif 
#if LIBSSH2_AES_CTR
    &libssh2_crypt_method_aes256_ctr,
    &libssh2_crypt_method_aes192_ctr,
    &libssh2_crypt_method_aes128_ctr,
#endif 
#if LIBSSH2_AES_CBC
    &libssh2_crypt_method_aes256_cbc,
    &libssh2_crypt_method_rijndael_cbc_lysator_liu_se,  
    &libssh2_crypt_method_aes192_cbc,
    &libssh2_crypt_method_aes128_cbc,
#endif 
#if LIBSSH2_BLOWFISH
    &libssh2_crypt_method_blowfish_cbc,
#endif 
#if LIBSSH2_RC4
    &libssh2_crypt_method_arcfour128,
    &libssh2_crypt_method_arcfour,
#endif 
#if LIBSSH2_CAST
    &libssh2_crypt_method_cast128_cbc,
#endif 
#if LIBSSH2_3DES
    &libssh2_crypt_method_3des_cbc,
#endif 
#if defined(LIBSSH2DEBUG) && defined(LIBSSH2_CRYPT_NONE_INSECURE)
    &libssh2_crypt_method_none,
#endif
    NULL
};


const LIBSSH2_CRYPT_METHOD **
libssh2_crypt_methods(void)
{
    return _libssh2_crypt_methods;
}
