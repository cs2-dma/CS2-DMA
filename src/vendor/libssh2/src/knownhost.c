

#include "libssh2_priv.h"

struct known_host {
    struct list_node node;
    char *name;      
    size_t name_len; 
    int port;        
    int typemask;    
    char *salt;      
    size_t salt_len; 
    char *key;       
    char *key_type_name; 
    size_t key_type_len; 
    char *comment;       
    size_t comment_len;  

    
    struct libssh2_knownhost external;
};

struct _LIBSSH2_KNOWNHOSTS
{
    LIBSSH2_SESSION *session;  
    struct list_head head;
};

static void free_host(LIBSSH2_SESSION *session, struct known_host *entry)
{
    if(entry) {
        if(entry->comment)
            LIBSSH2_FREE(session, entry->comment);
        if(entry->key_type_name)
            LIBSSH2_FREE(session, entry->key_type_name);
        if(entry->key)
            LIBSSH2_FREE(session, entry->key);
        if(entry->salt)
            LIBSSH2_FREE(session, entry->salt);
        if(entry->name)
            LIBSSH2_FREE(session, entry->name);
        LIBSSH2_FREE(session, entry);
    }
}


LIBSSH2_API LIBSSH2_KNOWNHOSTS *
libssh2_knownhost_init(LIBSSH2_SESSION *session)
{
    LIBSSH2_KNOWNHOSTS *knh =
        LIBSSH2_ALLOC(session, sizeof(struct _LIBSSH2_KNOWNHOSTS));

    if(!knh) {
        _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                       "Unable to allocate memory for known-hosts "
                       "collection");
        return NULL;
    }

    knh->session = session;

    _libssh2_list_init(&knh->head);

    return knh;
}

#define KNOWNHOST_MAGIC 0xdeadcafe

static struct libssh2_knownhost *knownhost_to_external(struct known_host *node)
{
    struct libssh2_knownhost *ext = &node->external;

    ext->magic = KNOWNHOST_MAGIC;
    ext->node = node;
    ext->name = ((node->typemask & LIBSSH2_KNOWNHOST_TYPE_MASK) ==
                 LIBSSH2_KNOWNHOST_TYPE_PLAIN) ? node->name : NULL;
    ext->key = node->key;
    ext->typemask = node->typemask;

    return ext;
}

static int
knownhost_add(LIBSSH2_KNOWNHOSTS *hosts,
              const char *host, const char *salt,
              const char *key_type_name, size_t key_type_len,
              const char *key, size_t keylen,
              const char *comment, size_t commentlen,
              int typemask, struct libssh2_knownhost **store)
{
    struct known_host *entry;
    size_t hostlen = strlen(host);
    int rc;
    char *ptr;
    size_t ptrlen;

    
    if(!(typemask & LIBSSH2_KNOWNHOST_KEY_MASK))
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_INVAL,
                              "No key type set");

    entry = LIBSSH2_CALLOC(hosts->session, sizeof(struct known_host));
    if(!entry)
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate memory for known host "
                              "entry");

    entry->typemask = typemask;

    switch(entry->typemask  & LIBSSH2_KNOWNHOST_TYPE_MASK) {
    case LIBSSH2_KNOWNHOST_TYPE_PLAIN:
    case LIBSSH2_KNOWNHOST_TYPE_CUSTOM:
        entry->name = LIBSSH2_ALLOC(hosts->session, hostlen + 1);
        if(!entry->name) {
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for host name");
            goto error;
        }
        memcpy(entry->name, host, hostlen + 1);
        entry->name_len = hostlen;
        break;
    case LIBSSH2_KNOWNHOST_TYPE_SHA1:
        rc = _libssh2_base64_decode(hosts->session, &ptr, &ptrlen,
                                    host, hostlen);
        if(rc)
            goto error;
        entry->name = ptr;
        entry->name_len = ptrlen;

        rc = _libssh2_base64_decode(hosts->session, &ptr, &ptrlen,
                                    salt, strlen(salt));
        if(rc)
            goto error;
        entry->salt = ptr;
        entry->salt_len = ptrlen;
        break;
    default:
        rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                            "Unknown host name type");
        goto error;
    }

    if(typemask & LIBSSH2_KNOWNHOST_KEYENC_BASE64) {
        
        if(!keylen)
            keylen = strlen(key);
        entry->key = LIBSSH2_ALLOC(hosts->session, keylen + 1);
        if(!entry->key) {
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for key");
            goto error;
        }
        memcpy(entry->key, key, keylen + 1);
        entry->key[keylen] = 0; 
    }
    else {
        
        size_t nlen = _libssh2_base64_encode(hosts->session, key, keylen,
                                             &ptr);
        if(!nlen) {
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for "
                                "base64-encoded key");
            goto error;
        }

        entry->key = ptr;
    }

    if(key_type_name && ((typemask & LIBSSH2_KNOWNHOST_KEY_MASK) ==
                          LIBSSH2_KNOWNHOST_KEY_UNKNOWN)) {
        entry->key_type_name = LIBSSH2_ALLOC(hosts->session, key_type_len + 1);
        if(!entry->key_type_name) {
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for key type");
            goto error;
        }
        memcpy(entry->key_type_name, key_type_name, key_type_len);
        entry->key_type_name[key_type_len] = 0;
        entry->key_type_len = key_type_len;
    }

    if(comment) {
        entry->comment = LIBSSH2_ALLOC(hosts->session, commentlen + 1);
        if(!entry->comment) {
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for comment");
            goto error;
        }
        memcpy(entry->comment, comment, commentlen + 1);
        entry->comment[commentlen] = 0; 
        entry->comment_len = commentlen;
    }
    else {
        entry->comment = NULL;
    }

    
    _libssh2_list_add(&hosts->head, &entry->node);

    if(store)
        *store = knownhost_to_external(entry);

    return LIBSSH2_ERROR_NONE;
error:
    free_host(hosts->session, entry);
    return rc;
}



LIBSSH2_API int
libssh2_knownhost_add(LIBSSH2_KNOWNHOSTS *hosts,
                      const char *host, const char *salt,
                      const char *key, size_t keylen,
                      int typemask, struct libssh2_knownhost **store)
{
    return knownhost_add(hosts, host, salt, NULL, 0, key, keylen, NULL,
                         0, typemask, store);
}




LIBSSH2_API int
libssh2_knownhost_addc(LIBSSH2_KNOWNHOSTS *hosts,
                       const char *host, const char *salt,
                       const char *key, size_t keylen,
                       const char *comment, size_t commentlen,
                       int typemask, struct libssh2_knownhost **store)
{
    return knownhost_add(hosts, host, salt, NULL, 0, key, keylen,
                         comment, commentlen, typemask, store);
}


static int
knownhost_check(LIBSSH2_KNOWNHOSTS *hosts,
                const char *hostp, int port,
                const char *key, size_t keylen,
                int typemask,
                struct libssh2_knownhost **ext)
{
    struct known_host *node;
    struct known_host *badkey = NULL;
    int type = typemask & LIBSSH2_KNOWNHOST_TYPE_MASK;
    char *keyalloc = NULL;
    int rc = LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;
    char hostbuff[270]; 
    const char *host;
    int numcheck; 
    int match = 0;

    if(type == LIBSSH2_KNOWNHOST_TYPE_SHA1)
        
        return LIBSSH2_KNOWNHOST_CHECK_MISMATCH;

    
    if(port >= 0) {
        int len = snprintf(hostbuff, sizeof(hostbuff), "[%s]:%d", hostp, port);
        if(len < 0 || len >= (int)sizeof(hostbuff)) {
            _libssh2_error(hosts->session,
                           LIBSSH2_ERROR_BUFFER_TOO_SMALL,
                           "Known-host write buffer too small");
            return LIBSSH2_KNOWNHOST_CHECK_FAILURE;
        }
        host = hostbuff;
        numcheck = 2; 
    }
    else {
        host = hostp;
        numcheck = 1; 
    }

    if(!(typemask & LIBSSH2_KNOWNHOST_KEYENC_BASE64)) {
        
        size_t nlen = _libssh2_base64_encode(hosts->session, key, keylen,
                                             &keyalloc);
        if(!nlen) {
            _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for base64-encoded "
                           "key");
            return LIBSSH2_KNOWNHOST_CHECK_FAILURE;
        }

        
        key = keyalloc;
    }

    do {
        node = _libssh2_list_first(&hosts->head);
        while(node) {
            switch(node->typemask & LIBSSH2_KNOWNHOST_TYPE_MASK) {
            case LIBSSH2_KNOWNHOST_TYPE_PLAIN:
                if(type == LIBSSH2_KNOWNHOST_TYPE_PLAIN)
                    match = !strcmp(host, node->name);
                break;
            case LIBSSH2_KNOWNHOST_TYPE_CUSTOM:
                if(type == LIBSSH2_KNOWNHOST_TYPE_CUSTOM)
                    match = !strcmp(host, node->name);
                break;
            case LIBSSH2_KNOWNHOST_TYPE_SHA1:
                if(type == LIBSSH2_KNOWNHOST_TYPE_PLAIN) {
                    
                    unsigned char hash[SHA_DIGEST_LENGTH];
                    libssh2_hmac_ctx ctx;
                    if(!_libssh2_hmac_ctx_init(&ctx))
                        break;

                    if(SHA_DIGEST_LENGTH != node->name_len) {
                        
                        break;
                    }
                    if(!_libssh2_hmac_sha1_init(&ctx,
                                                node->salt, node->salt_len))
                        break;
                    if(!_libssh2_hmac_update(&ctx, host, strlen(host)) ||
                       !_libssh2_hmac_final(&ctx, hash)) {
                        _libssh2_hmac_cleanup(&ctx);
                        break;
                    }
                    _libssh2_hmac_cleanup(&ctx);

                    if(!memcmp(hash, node->name, SHA_DIGEST_LENGTH))
                        
                        match = 1;
                }
                break;
            default: 
                break;
            }
            if(match) {
                int host_key_type = typemask & LIBSSH2_KNOWNHOST_KEY_MASK;
                int known_key_type =
                    node->typemask & LIBSSH2_KNOWNHOST_KEY_MASK;
                
                if(host_key_type != LIBSSH2_KNOWNHOST_KEY_UNKNOWN &&
                     (host_key_type == 0 ||
                      host_key_type == known_key_type)) {
                    
                    if(!strcmp(key, node->key)) {
                        
                        if(ext)
                            *ext = knownhost_to_external(node);
                        badkey = NULL;
                        rc = LIBSSH2_KNOWNHOST_CHECK_MATCH;
                        break;
                    }
                    else {
                        
                        if(!badkey)
                            badkey = node;
                    }
                }
                match = 0; 
            }
            node = _libssh2_list_next(&node->node);
        }
        host = hostp;
    } while(!match && --numcheck);

    if(badkey) {
        
        if(ext)
            *ext = knownhost_to_external(badkey);
        rc = LIBSSH2_KNOWNHOST_CHECK_MISMATCH;
    }

    if(keyalloc)
        LIBSSH2_FREE(hosts->session, keyalloc);

    return rc;
}


LIBSSH2_API int
libssh2_knownhost_check(LIBSSH2_KNOWNHOSTS *hosts,
                        const char *hostp, const char *key, size_t keylen,
                        int typemask,
                        struct libssh2_knownhost **ext)
{
    return knownhost_check(hosts, hostp, -1, key, keylen,
                           typemask, ext);
}


LIBSSH2_API int
libssh2_knownhost_checkp(LIBSSH2_KNOWNHOSTS *hosts,
                         const char *hostp, int port,
                         const char *key, size_t keylen,
                         int typemask,
                         struct libssh2_knownhost **ext)
{
    return knownhost_check(hosts, hostp, port, key, keylen,
                           typemask, ext);
}



LIBSSH2_API int
libssh2_knownhost_del(LIBSSH2_KNOWNHOSTS *hosts,
                      struct libssh2_knownhost *entry)
{
    struct known_host *node;

    
    if(!entry || (entry->magic != KNOWNHOST_MAGIC))
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_INVAL,
                              "Invalid host information");

    
    node = entry->node;

    
    _libssh2_list_remove(&node->node);

    
    memset(entry, 0, sizeof(struct libssh2_knownhost));

    
    free_host(hosts->session, node);

    return 0;
}


LIBSSH2_API void
libssh2_knownhost_free(LIBSSH2_KNOWNHOSTS *hosts)
{
    struct known_host *node;
    struct known_host *next;

    for(node = _libssh2_list_first(&hosts->head); node; node = next) {
        next = _libssh2_list_next(&node->node);
        free_host(hosts->session, node);
    }
    LIBSSH2_FREE(hosts->session, hosts);
}



static int oldstyle_hostline(LIBSSH2_KNOWNHOSTS *hosts,
                             const char *host, size_t hostlen,
                             const char *key_type_name, size_t key_type_len,
                             const char *key, size_t keylen, int key_type,
                             const char *comment, size_t commentlen)
{
    int rc = 0;
    size_t namelen = 0;
    const char *name = host + hostlen;

    if(hostlen < 1)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Failed to parse known_hosts line "
                              "(no host names)");

    while(name > host) {
        --name;
        ++namelen;

        
        if((name == host) || (*(name-1) == ',')) {

            char hostbuf[256];

            
            if(namelen >= sizeof(hostbuf)-1)
                return _libssh2_error(hosts->session,
                                      LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                                      "Failed to parse known_hosts line "
                                      "(unexpected length)");

            
            memcpy(hostbuf, name, namelen);
            hostbuf[namelen] = 0;

            rc = knownhost_add(hosts, hostbuf, NULL,
                               key_type_name, key_type_len,
                               key, keylen,
                               comment, commentlen,
                               key_type | LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                               LIBSSH2_KNOWNHOST_KEYENC_BASE64, NULL);
            if(rc)
                return rc;

            if(name > host) {
                namelen = 0;
                --name; 
            }
        }
    }

    return rc;
}


static int hashed_hostline(LIBSSH2_KNOWNHOSTS *hosts,
                           const char *host, size_t hostlen,
                           const char *key_type_name, size_t key_type_len,
                           const char *key, size_t keylen, int key_type,
                           const char *comment, size_t commentlen)
{
    const char *p;
    char saltbuf[32];
    char hostbuf[256];

    const char *salt = &host[3]; 
    hostlen -= 3;    

    
    for(p = salt; *p && (*p != '|'); p++)
        ;

    if(*p == '|') {
        const char *hash = NULL;
        size_t saltlen = p - salt;
        if(saltlen >= (sizeof(saltbuf)-1)) 
            return _libssh2_error(hosts->session,
                                  LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                                  "Failed to parse known_hosts line "
                                  "(unexpectedly long salt)");

        memcpy(saltbuf, salt, saltlen);
        saltbuf[saltlen] = 0; 
        salt = saltbuf; 

        hash = p + 1; 

        
        host = hash;
        hostlen -= saltlen + 1; 

        
        if(hostlen >= sizeof(hostbuf)-1)
            return _libssh2_error(hosts->session,
                                  LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                                  "Failed to parse known_hosts line "
                                  "(unexpected length)");

        memcpy(hostbuf, host, hostlen);
        hostbuf[hostlen] = 0;

        return knownhost_add(hosts, hostbuf, salt,
                             key_type_name, key_type_len,
                             key, keylen,
                             comment, commentlen,
                             key_type | LIBSSH2_KNOWNHOST_TYPE_SHA1 |
                             LIBSSH2_KNOWNHOST_KEYENC_BASE64, NULL);
    }
    else
        return 0; 
}


static int hostline(LIBSSH2_KNOWNHOSTS *hosts,
                    const char *host, size_t hostlen,
                    const char *key, size_t keylen)
{
    const char *comment = NULL;
    const char *key_type_name = NULL;
    size_t commentlen = 0;
    size_t key_type_len = 0;
    int key_type;

    
    if(keylen < 20)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Failed to parse known_hosts line "
                              "(key too short)");

    switch(key[0]) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        key_type = LIBSSH2_KNOWNHOST_KEY_RSA1;

        
        break;

    default:
        key_type_name = key;
        while(keylen && *key &&
               (*key != ' ') && (*key != '\t')) {
            key++;
            keylen--;
        }
        key_type_len = key - key_type_name;

        if(!strncmp(key_type_name, "ssh-ed25519", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_ED25519;
        else if(!strncmp(key_type_name, "ecdsa-sha2-nistp256", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        else if(!strncmp(key_type_name, "ecdsa-sha2-nistp384", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        else if(!strncmp(key_type_name, "ecdsa-sha2-nistp521", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
        else if(!strncmp(key_type_name, "ssh-rsa", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_SSHRSA;
#if LIBSSH2_DSA
        else if(!strncmp(key_type_name, "ssh-dss", key_type_len))
            key_type = LIBSSH2_KNOWNHOST_KEY_SSHDSS;
#endif
        else
            key_type = LIBSSH2_KNOWNHOST_KEY_UNKNOWN;

        
        while((*key ==' ') || (*key == '\t')) {
            key++;
            keylen--;
        }

        comment = key;
        commentlen = keylen;

        
        while(commentlen && *comment &&
              (*comment != ' ') && (*comment != '\t')) {
            comment++;
            commentlen--;
        }

        
        keylen -= commentlen;

        
        if(commentlen == 0)
            comment = NULL;

        
        while(commentlen && *comment &&
              ((*comment ==' ') || (*comment == '\t'))) {
            comment++;
            commentlen--;
        }
        break;
    }

    
    if((hostlen > 2) && memcmp(host, "|1|", 3)) {
        
        return oldstyle_hostline(hosts, host, hostlen, key_type_name,
                                 key_type_len, key, keylen, key_type,
                                 comment, commentlen);
    }
    else {
        
        return hashed_hostline(hosts, host, hostlen, key_type_name,
                               key_type_len, key, keylen, key_type,
                               comment, commentlen);
    }
}


LIBSSH2_API int
libssh2_knownhost_readline(LIBSSH2_KNOWNHOSTS *hosts,
                           const char *line, size_t len, int type)
{
    const char *cp;
    const char *hostp;
    const char *keyp;
    size_t hostlen;
    size_t keylen;
    int rc;

    if(type != LIBSSH2_KNOWNHOST_FILE_OPENSSH)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Unsupported type of known-host information "
                              "store");

    cp = line;

    
    while(len && ((*cp == ' ') || (*cp == '\t'))) {
        cp++;
        len--;
    }

    if(!len || !*cp || (*cp == '#') || (*cp == '\n'))
        
        return LIBSSH2_ERROR_NONE;

    
    hostp = cp;

    
    while(len && *cp && (*cp != ' ') && (*cp != '\t')) {
        cp++;
        len--;
    }

    hostlen = cp - hostp;

    
    while(len && *cp && ((*cp == ' ') || (*cp == '\t'))) {
        cp++;
        len--;
    }

    if(!*cp || !len) 
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Failed to parse known_hosts line");

    keyp = cp; 
    keylen = len;

    
    while(len && *cp && (*cp != '\n')) {
        cp++;
        len--;
    }

    
    if(*cp == '\n')
        keylen--; 

    
    rc = hostline(hosts, hostp, hostlen, keyp, keylen);
    if(rc)
        return rc; 

    return LIBSSH2_ERROR_NONE; 
}



LIBSSH2_API int
libssh2_knownhost_readfile(LIBSSH2_KNOWNHOSTS *hosts,
                           const char *filename, int type)
{
    FILE *file;
    int num = 0;
    char buf[4092];

    if(type != LIBSSH2_KNOWNHOST_FILE_OPENSSH)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Unsupported type of known-host information "
                              "store");

    file = fopen(filename, FOPEN_READTEXT);
    if(file) {
        while(fgets(buf, sizeof(buf), file)) {
            if(libssh2_knownhost_readline(hosts, buf, strlen(buf), type)) {
                num = _libssh2_error(hosts->session, LIBSSH2_ERROR_KNOWN_HOSTS,
                                     "Failed to parse known hosts file");
                break;
            }
            num++;
        }
        fclose(file);
    }
    else
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_FILE,
                              "Failed to open file");

    return num;
}


static int
knownhost_writeline(LIBSSH2_KNOWNHOSTS *hosts,
                    struct known_host *node,
                    char *buf, size_t buflen,
                    size_t *outlen, int type)
{
    size_t required_size;

    const char *key_type_name;
    size_t key_type_len;

    
    if(type != LIBSSH2_KNOWNHOST_FILE_OPENSSH)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Unsupported type of known-host information "
                              "store");

    switch(node->typemask & LIBSSH2_KNOWNHOST_KEY_MASK) {
    case LIBSSH2_KNOWNHOST_KEY_RSA1:
        key_type_name = NULL;
        key_type_len = 0;
        break;
    case LIBSSH2_KNOWNHOST_KEY_SSHRSA:
        key_type_name = "ssh-rsa";
        key_type_len = 7;
        break;
#if LIBSSH2_DSA
    case LIBSSH2_KNOWNHOST_KEY_SSHDSS:
        key_type_name = "ssh-dss";
        key_type_len = 7;
        break;
#endif
    case LIBSSH2_KNOWNHOST_KEY_ECDSA_256:
        key_type_name = "ecdsa-sha2-nistp256";
        key_type_len = 19;
        break;
    case LIBSSH2_KNOWNHOST_KEY_ECDSA_384:
        key_type_name = "ecdsa-sha2-nistp384";
        key_type_len = 19;
        break;
    case LIBSSH2_KNOWNHOST_KEY_ECDSA_521:
        key_type_name = "ecdsa-sha2-nistp521";
        key_type_len = 19;
        break;
    case LIBSSH2_KNOWNHOST_KEY_ED25519:
        key_type_name = "ssh-ed25519";
        key_type_len = 11;
        break;
    case LIBSSH2_KNOWNHOST_KEY_UNKNOWN:
        key_type_name = node->key_type_name;
        if(key_type_name) {
            key_type_len = node->key_type_len;
            break;
        }
        
        LIBSSH2_FALLTHROUGH();
    default:
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Unsupported type of known-host entry");
    }

    

    required_size = strlen(node->key);

    if(key_type_len)
        required_size += key_type_len + 1; 
    if(node->comment)
        required_size += node->comment_len + 1; 

    if((node->typemask & LIBSSH2_KNOWNHOST_TYPE_MASK) ==
       LIBSSH2_KNOWNHOST_TYPE_SHA1) {
        char *namealloc;
        size_t name_base64_len;
        char *saltalloc;
        size_t salt_base64_len;

        name_base64_len = _libssh2_base64_encode(hosts->session, node->name,
                                                 node->name_len, &namealloc);
        if(!name_base64_len)
            return _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "base64-encoded host name");

        salt_base64_len = _libssh2_base64_encode(hosts->session,
                                                 node->salt, node->salt_len,
                                                 &saltalloc);
        if(!salt_base64_len) {
            LIBSSH2_FREE(hosts->session, namealloc);
            return _libssh2_error(hosts->session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "base64-encoded salt");
        }

        required_size += salt_base64_len + name_base64_len + 7;
        

        if(required_size <= buflen) {
            if(node->comment && key_type_len)
                snprintf(buf, buflen, "|1|%s|%s %s %s %s\n", saltalloc,
                         namealloc, key_type_name, node->key, node->comment);
            else if(node->comment)
                snprintf(buf, buflen, "|1|%s|%s %s %s\n", saltalloc, namealloc,
                         node->key, node->comment);
            else if(key_type_len)
                snprintf(buf, buflen, "|1|%s|%s %s %s\n", saltalloc, namealloc,
                         key_type_name, node->key);
            else
                snprintf(buf, buflen, "|1|%s|%s %s\n", saltalloc, namealloc,
                         node->key);
        }

        LIBSSH2_FREE(hosts->session, namealloc);
        LIBSSH2_FREE(hosts->session, saltalloc);
    }
    else {
        required_size += node->name_len + 3;
        

        if(required_size <= buflen) {
            if(node->comment && key_type_len)
                snprintf(buf, buflen, "%s %s %s %s\n", node->name,
                         key_type_name, node->key, node->comment);
            else if(node->comment)
                snprintf(buf, buflen, "%s %s %s\n", node->name, node->key,
                         node->comment);
            else if(key_type_len)
                snprintf(buf, buflen, "%s %s %s\n", node->name, key_type_name,
                         node->key);
            else
                snprintf(buf, buflen, "%s %s\n", node->name, node->key);
        }
    }

    
    *outlen = required_size-1;

    if(required_size <= buflen)
        return LIBSSH2_ERROR_NONE;
    else
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_BUFFER_TOO_SMALL,
                              "Known-host write buffer too small");
}


LIBSSH2_API int
libssh2_knownhost_writeline(LIBSSH2_KNOWNHOSTS *hosts,
                            struct libssh2_knownhost *known,
                            char *buffer, size_t buflen,
                            size_t *outlen, 
                            int type)
{
    struct known_host *node;

    if(known->magic != KNOWNHOST_MAGIC)
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_INVAL,
                              "Invalid host information");

    node = known->node;

    return knownhost_writeline(hosts, node, buffer, buflen, outlen, type);
}


LIBSSH2_API int
libssh2_knownhost_writefile(LIBSSH2_KNOWNHOSTS *hosts,
                            const char *filename, int type)
{
    struct known_host *node;
    FILE *file;
    int rc = LIBSSH2_ERROR_NONE;
    char buffer[4092];

    
    if(type != LIBSSH2_KNOWNHOST_FILE_OPENSSH)
        return _libssh2_error(hosts->session,
                              LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                              "Unsupported type of known-host information "
                              "store");

    file = fopen(filename, FOPEN_WRITETEXT);
    if(!file)
        return _libssh2_error(hosts->session, LIBSSH2_ERROR_FILE,
                              "Failed to open file");

    for(node = _libssh2_list_first(&hosts->head);
        node;
        node = _libssh2_list_next(&node->node)) {
        size_t wrote = 0;
        size_t nwrote;
        rc = knownhost_writeline(hosts, node, buffer, sizeof(buffer), &wrote,
                                 type);
        if(rc)
            break;

        nwrote = fwrite(buffer, 1, wrote, file);
        if(nwrote != wrote) {
            
            rc = _libssh2_error(hosts->session, LIBSSH2_ERROR_FILE,
                                "Write failed");
            break;
        }
    }
    fclose(file);

    return rc;
}



LIBSSH2_API int
libssh2_knownhost_get(LIBSSH2_KNOWNHOSTS *hosts,
                      struct libssh2_knownhost **ext,
                      struct libssh2_knownhost *oprev)
{
    struct known_host *node;
    if(oprev && oprev->node) {
        
        struct known_host *prev = oprev->node;

        
        node = _libssh2_list_next(&prev->node);

    }
    else
        node = _libssh2_list_first(&hosts->head);

    if(!node)
        
        return 1;

    *ext = knownhost_to_external(node);

    return 0;
}
