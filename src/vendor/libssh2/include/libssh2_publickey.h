



#ifndef LIBSSH2_PUBLICKEY_H
#define LIBSSH2_PUBLICKEY_H 1

#include "libssh2.h"

typedef struct _LIBSSH2_PUBLICKEY               LIBSSH2_PUBLICKEY;

typedef struct _libssh2_publickey_attribute {
    const char *name;
    unsigned long name_len;
    const char *value;
    unsigned long value_len;
    char mandatory;
} libssh2_publickey_attribute;

typedef struct _libssh2_publickey_list {
    unsigned char *packet; 

    const unsigned char *name;
    unsigned long name_len;
    const unsigned char *blob;
    unsigned long blob_len;
    unsigned long num_attrs;
    libssh2_publickey_attribute *attrs; 
} libssh2_publickey_list;


#define libssh2_publickey_attribute(name, value, mandatory) \
    { (name), strlen(name), (value), strlen(value), (mandatory) },
#define libssh2_publickey_attribute_fast(name, value, mandatory) \
    { (name), sizeof(name) - 1, (value), sizeof(value) - 1, (mandatory) },

#ifdef __cplusplus
extern "C" {
#endif


LIBSSH2_API LIBSSH2_PUBLICKEY *
libssh2_publickey_init(LIBSSH2_SESSION *session);

LIBSSH2_API int
libssh2_publickey_add_ex(LIBSSH2_PUBLICKEY *pkey,
                         const unsigned char *name,
                         unsigned long name_len,
                         const unsigned char *blob,
                         unsigned long blob_len, char overwrite,
                         unsigned long num_attrs,
                         const libssh2_publickey_attribute attrs[]);
#define libssh2_publickey_add(pkey, name, blob, blob_len, overwrite, \
                              num_attrs, attrs) \
    libssh2_publickey_add_ex((pkey), \
                             (name), strlen(name), \
                             (blob), (blob_len), \
                             (overwrite), (num_attrs), (attrs))

LIBSSH2_API int libssh2_publickey_remove_ex(LIBSSH2_PUBLICKEY *pkey,
                                            const unsigned char *name,
                                            unsigned long name_len,
                                            const unsigned char *blob,
                                            unsigned long blob_len);
#define libssh2_publickey_remove(pkey, name, blob, blob_len) \
    libssh2_publickey_remove_ex((pkey), \
                                (name), strlen(name), \
                                (blob), (blob_len))

LIBSSH2_API int
libssh2_publickey_list_fetch(LIBSSH2_PUBLICKEY *pkey,
                             unsigned long *num_keys,
                             libssh2_publickey_list **pkey_list);
LIBSSH2_API void
libssh2_publickey_list_free(LIBSSH2_PUBLICKEY *pkey,
                            libssh2_publickey_list *pkey_list);

LIBSSH2_API int libssh2_publickey_shutdown(LIBSSH2_PUBLICKEY *pkey);

#ifdef __cplusplus
} 
#endif

#endif 
