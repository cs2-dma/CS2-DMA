



#include "libssh2_priv.h"
#include "misc.h"
#include "cipher-chachapoly.h"

int
chachapoly_timingsafe_bcmp(const void *b1, const void *b2, size_t n);

int
chachapoly_init(struct chachapoly_ctx *ctx, const u_char *key, u_int keylen)
{
    if(keylen != (32 + 32)) 
        return LIBSSH2_ERROR_INVAL;
    chacha_keysetup(&ctx->main_ctx, key, 256);
    chacha_keysetup(&ctx->header_ctx, key + 32, 256);
    return 0;
}


int
chachapoly_crypt(struct chachapoly_ctx *ctx, u_int seqnr, u_char *dest,
                 const u_char *src, u_int len, u_int aadlen, int do_encrypt)
{
    u_char seqbuf[8];
    const u_char one[8] = { 1, 0, 0, 0, 0, 0, 0, 0 }; 
    u_char expected_tag[POLY1305_TAGLEN], poly_key[POLY1305_KEYLEN];
    int r = LIBSSH2_ERROR_INVAL;
    unsigned char *ptr = NULL;

    
    memset(poly_key, 0, sizeof(poly_key));
    ptr = &seqbuf[0];
    _libssh2_store_u64(&ptr, seqnr);
    chacha_ivsetup(&ctx->main_ctx, seqbuf, NULL);
    chacha_encrypt_bytes(&ctx->main_ctx,
                         poly_key, poly_key, sizeof(poly_key));

    
    if(!do_encrypt) {
        const u_char *tag = src + aadlen + len;

        poly1305_auth(expected_tag, src, aadlen + len, poly_key);
        if(chachapoly_timingsafe_bcmp(expected_tag, tag, POLY1305_TAGLEN)
           != 0) {
            r = LIBSSH2_ERROR_DECRYPT;
            goto out;
        }
    }

    
    if(aadlen) {
        chacha_ivsetup(&ctx->header_ctx, seqbuf, NULL);
        chacha_encrypt_bytes(&ctx->header_ctx, src, dest, aadlen);
    }

    
    chacha_ivsetup(&ctx->main_ctx, seqbuf, one);
    chacha_encrypt_bytes(&ctx->main_ctx, src + aadlen,
                         dest + aadlen, len);

    
    if(do_encrypt) {
        poly1305_auth(dest + aadlen + len, dest, aadlen + len,
                      poly_key);
    }
    r = 0;
out:
    memset(expected_tag, 0, sizeof(expected_tag));
    memset(seqbuf, 0, sizeof(seqbuf));
    memset(poly_key, 0, sizeof(poly_key));
    return r;
}


int
chachapoly_get_length(struct chachapoly_ctx *ctx, unsigned int *plenp,
                      unsigned int seqnr, const unsigned char *cp,
                      unsigned int len)
{
    u_char buf[4], seqbuf[8];
    unsigned char *ptr = NULL;

    if(len < 4)
        return -1;
    ptr = &seqbuf[0];
    _libssh2_store_u64(&ptr, seqnr);
    chacha_ivsetup(&ctx->header_ctx, seqbuf, NULL);
    chacha_encrypt_bytes(&ctx->header_ctx, cp, buf, 4);
    *plenp = _libssh2_ntohu32(buf);
    return 0;
}

int
chachapoly_timingsafe_bcmp(const void *b1, const void *b2, size_t n)
{
    const unsigned char *p1 = b1, *p2 = b2;
    int ret = 0;

    for(; n > 0; n--)
        ret |= *p1++ ^ *p2++;
    return (ret != 0);
}
