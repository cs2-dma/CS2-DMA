


#include "libssh2_priv.h"

#ifndef HAVE_BCRYPT_PBKDF

#include <stdlib.h>

#define LIBSSH2_BCRYPT_PBKDF_C
#include "blowfish.c"



#define BCRYPT_BLOCKS 8
#define BCRYPT_HASHSIZE (BCRYPT_BLOCKS * 4)

static void
bcrypt_hash(uint8_t *sha2pass, uint8_t *sha2salt, uint8_t *out)
{
    blf_ctx state;
    uint8_t ciphertext[BCRYPT_HASHSIZE] = {
        'O', 'x', 'y', 'c', 'h', 'r', 'o', 'm', 'a', 't', 'i', 'c',
        'B', 'l', 'o', 'w', 'f', 'i', 's', 'h',
        'S', 'w', 'a', 't',
        'D', 'y', 'n', 'a', 'm', 'i', 't', 'e' };
    uint32_t cdata[BCRYPT_BLOCKS];
    int i;
    uint16_t j;
    uint16_t shalen = SHA512_DIGEST_LENGTH;

    
    Blowfish_initstate(&state);
    Blowfish_expandstate(&state, sha2salt, shalen, sha2pass, shalen);
    for(i = 0; i < 64; i++) {
        Blowfish_expand0state(&state, sha2salt, shalen);
        Blowfish_expand0state(&state, sha2pass, shalen);
    }

    
    j = 0;
    for(i = 0; i < BCRYPT_BLOCKS; i++)
        cdata[i] = Blowfish_stream2word(ciphertext, sizeof(ciphertext),
                                        &j);
    for(i = 0; i < 64; i++)
        blf_enc(&state, cdata, BCRYPT_BLOCKS / 2);

    
    for(i = 0; i < BCRYPT_BLOCKS; i++) {
        out[4 * i + 3] = (uint8_t)((cdata[i] >> 24) & 0xff);
        out[4 * i + 2] = (cdata[i] >> 16) & 0xff;
        out[4 * i + 1] = (cdata[i] >> 8) & 0xff;
        out[4 * i + 0] = cdata[i] & 0xff;
    }

    
    _libssh2_explicit_zero(ciphertext, sizeof(ciphertext));
    _libssh2_explicit_zero(cdata, sizeof(cdata));
    _libssh2_explicit_zero(&state, sizeof(state));
}

static int
bcrypt_pbkdf(const char *pass, size_t passlen, const uint8_t *salt,
             size_t saltlen,
             uint8_t *key, size_t keylen, unsigned int rounds)
{
    uint8_t sha2pass[SHA512_DIGEST_LENGTH];
    uint8_t sha2salt[SHA512_DIGEST_LENGTH];
    uint8_t out[BCRYPT_HASHSIZE];
    uint8_t tmpout[BCRYPT_HASHSIZE];
    uint8_t *countsalt;
    size_t i, j, amt, stride;
    uint32_t count;
    size_t origkeylen = keylen;
    libssh2_sha512_ctx ctx;

    
    if(rounds < 1)
        return -1;
    if(passlen == 0 || saltlen == 0 || keylen == 0 ||
       keylen > sizeof(out) * sizeof(out) || saltlen > 1 << 20)
        return -1;
    countsalt = calloc(1, saltlen + 4);
    if(!countsalt)
        return -1;
    stride = (keylen + sizeof(out) - 1) / sizeof(out);
    amt = (keylen + stride - 1) / stride;

    memcpy(countsalt, salt, saltlen);

    
    if(!libssh2_sha512_init(&ctx) ||
       !libssh2_sha512_update(ctx, pass, passlen) ||
       !libssh2_sha512_final(ctx, sha2pass)) {
        free(countsalt);
        return -1;
    }

    
    for(count = 1; keylen > 0; count++) {
        countsalt[saltlen + 0] = (uint8_t)((count >> 24) & 0xff);
        countsalt[saltlen + 1] = (count >> 16) & 0xff;
        countsalt[saltlen + 2] = (count >> 8) & 0xff;
        countsalt[saltlen + 3] = count & 0xff;

        
        if(!libssh2_sha512_init(&ctx) ||
           !libssh2_sha512_update(ctx, countsalt, saltlen + 4) ||
           !libssh2_sha512_final(ctx, sha2salt)) {
            _libssh2_explicit_zero(out, sizeof(out));
            free(countsalt);
            return -1;
        }

        bcrypt_hash(sha2pass, sha2salt, tmpout);
        memcpy(out, tmpout, sizeof(out));

        for(i = 1; i < rounds; i++) {
            
            if(!libssh2_sha512_init(&ctx) ||
               !libssh2_sha512_update(ctx, tmpout, sizeof(tmpout)) ||
               !libssh2_sha512_final(ctx, sha2salt)) {
                _libssh2_explicit_zero(out, sizeof(out));
                free(countsalt);
                return -1;
            }

            bcrypt_hash(sha2pass, sha2salt, tmpout);
            for(j = 0; j < sizeof(out); j++)
                out[j] ^= tmpout[j];
        }

        
        amt = LIBSSH2_MIN(amt, keylen);
        for(i = 0; i < amt; i++) {
            size_t dest = i * stride + (count - 1);
            if(dest >= origkeylen) {
                break;
            }
            key[dest] = out[i];
        }
        keylen -= i;
    }

    
    _libssh2_explicit_zero(out, sizeof(out));
    free(countsalt);

    return 0;
}
#endif 



int _libssh2_bcrypt_pbkdf(const char *pass,
                          size_t passlen,
                          const uint8_t *salt,
                          size_t saltlen,
                          uint8_t *key,
                          size_t keylen,
                          unsigned int rounds)
{
    return bcrypt_pbkdf(pass,
                        passlen,
                        salt,
                        saltlen,
                        key,
                        keylen,
                        rounds);
}
