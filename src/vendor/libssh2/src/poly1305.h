



#ifndef POLY1305_H
#define POLY1305_H

#define POLY1305_KEYLEN 32
#define POLY1305_TAGLEN 16

void poly1305_auth(u_char out[POLY1305_TAGLEN], const u_char *m, size_t inlen,
                   const u_char key[POLY1305_KEYLEN]);

#endif 
