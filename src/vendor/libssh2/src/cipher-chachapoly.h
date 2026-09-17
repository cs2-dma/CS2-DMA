


#ifndef CHACHA_POLY_AEAD_H
#define CHACHA_POLY_AEAD_H

#include "chacha.h"
#include "poly1305.h"

#define CHACHA_KEYLEN   32 

struct chachapoly_ctx {
    struct chacha_ctx main_ctx, header_ctx;
};

int chachapoly_init(struct chachapoly_ctx *cpctx,
                    const u_char *key, u_int keylen);
int chachapoly_crypt(struct chachapoly_ctx *cpctx, u_int seqnr,
                     u_char *dest, const u_char *src, u_int len, u_int aadlen,
                     int do_encrypt);
int chachapoly_get_length(struct chachapoly_ctx *cpctx,
                          u_int *plenp, u_int seqnr, const u_char *cp,
                          u_int len);

#endif 
