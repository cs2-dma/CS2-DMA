

#include "libssh2_priv.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>
#include <assert.h>

#ifdef _WIN32

#define recv(s, b, l, f)  recv((s), (b), (int)(l), (f))
#define send(s, b, l, f)  send((s), (b), (int)(l), (f))
#endif


#ifdef LIBSSH2_SNPRINTF
#include <stdarg.h>


int _libssh2_snprintf(char *cp, size_t cp_max_len, const char *fmt, ...)
{
    va_list args;
    int n;

    if(cp_max_len < 2)
        return 0;
    va_start(args, fmt);
    n = vsnprintf(cp, cp_max_len, fmt, args);
    va_end(args);
    return (n < (int)cp_max_len) ? n : (int)(cp_max_len - 1);
}
#endif

int _libssh2_error_flags(LIBSSH2_SESSION* session, int errcode,
                         const char *errmsg, int errflags)
{
    if(!session) {
        if(errmsg)
            fprintf(stderr, "Session is NULL, error: %s\n", errmsg);
        return errcode;
    }

    if(session->err_flags & LIBSSH2_ERR_FLAG_DUP)
        LIBSSH2_FREE(session, (char *)session->err_msg);

    session->err_code = errcode;
    session->err_flags = 0;

    if(errmsg && ((errflags & LIBSSH2_ERR_FLAG_DUP) != 0)) {
        size_t len = strlen(errmsg);
        char *copy = LIBSSH2_ALLOC(session, len + 1);
        if(copy) {
            memcpy(copy, errmsg, len + 1);
            session->err_flags = LIBSSH2_ERR_FLAG_DUP;
            session->err_msg = copy;
        }
        else
            
            session->err_msg = "former error forgotten (OOM)";
    }
    else
        session->err_msg = errmsg;

#ifdef LIBSSH2DEBUG
    if((errcode == LIBSSH2_ERROR_EAGAIN) && !session->api_block_mode)
        
        return errcode;
    _libssh2_debug((session, LIBSSH2_TRACE_ERROR, "%d - %s", session->err_code,
                   session->err_msg));
#endif

    return errcode;
}

int _libssh2_error(LIBSSH2_SESSION* session, int errcode, const char *errmsg)
{
    return _libssh2_error_flags(session, errcode, errmsg, 0);
}

#ifdef _WIN32
int _libssh2_wsa2errno(void)
{
    switch(WSAGetLastError()) {
    case WSAEWOULDBLOCK:
        return EAGAIN;

    case WSAENOTSOCK:
        return EBADF;

    case WSAEINTR:
        return EINTR;

    default:
        
        return EIO;
    }
}
#endif


ssize_t
_libssh2_recv(libssh2_socket_t sock, void *buffer, size_t length,
              int flags, void **abstract)
{
    ssize_t rc;

    (void)abstract;

    rc = recv(sock, buffer, length, flags);
    if(rc < 0) {
        int err;
#ifdef _WIN32
        err = _libssh2_wsa2errno();
#else
        err = errno;
#endif
        
        if(err == EINTR)
            return -EAGAIN;
        
        if(err == ENOENT)
            return -EAGAIN;
#ifdef EWOULDBLOCK 
        else if(err == EWOULDBLOCK)
            return -EAGAIN;
#endif
        else
            return -err;
    }
    return rc;
}


ssize_t
_libssh2_send(libssh2_socket_t sock, const void *buffer, size_t length,
              int flags, void **abstract)
{
    ssize_t rc;

    (void)abstract;

    rc = send(sock, buffer, length, flags);
    if(rc < 0) {
        int err;
#ifdef _WIN32
        err = _libssh2_wsa2errno();
#else
        err = errno;
#endif
        
        if(err == EINTR)
            return -EAGAIN;
#ifdef EWOULDBLOCK 
        if(err == EWOULDBLOCK)
            return -EAGAIN;
#endif
        return -err;
    }
    return rc;
}


uint32_t
_libssh2_ntohu32(const unsigned char *buf)
{
    return ((uint32_t)buf[0] << 24)
         | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] << 8)
         | ((uint32_t)buf[3]);
}



libssh2_uint64_t
_libssh2_ntohu64(const unsigned char *buf)
{
    return ((libssh2_uint64_t)buf[0] << 56)
         | ((libssh2_uint64_t)buf[1] << 48)
         | ((libssh2_uint64_t)buf[2] << 40)
         | ((libssh2_uint64_t)buf[3] << 32)
         | ((libssh2_uint64_t)buf[4] << 24)
         | ((libssh2_uint64_t)buf[5] << 16)
         | ((libssh2_uint64_t)buf[6] <<  8)
         | ((libssh2_uint64_t)buf[7]);
}


void
_libssh2_htonu32(unsigned char *buf, uint32_t value)
{
    buf[0] = (unsigned char)((value >> 24) & 0xFF);
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
}


void _libssh2_store_u32(unsigned char **buf, uint32_t value)
{
    _libssh2_htonu32(*buf, value);
    *buf += sizeof(uint32_t);
}


void _libssh2_store_u64(unsigned char **buf, libssh2_uint64_t value)
{
    unsigned char *ptr = *buf;

    ptr[0] = (unsigned char)((value >> 56) & 0xFF);
    ptr[1] = (unsigned char)((value >> 48) & 0xFF);
    ptr[2] = (unsigned char)((value >> 40) & 0xFF);
    ptr[3] = (unsigned char)((value >> 32) & 0xFF);
    ptr[4] = (unsigned char)((value >> 24) & 0xFF);
    ptr[5] = (unsigned char)((value >> 16) & 0xFF);
    ptr[6] = (unsigned char)((value >> 8) & 0xFF);
    ptr[7] = (unsigned char)(value & 0xFF);

    *buf += sizeof(libssh2_uint64_t);
}


int _libssh2_store_str(unsigned char **buf, const char *str, size_t len)
{
    uint32_t len_stored = (uint32_t)len;

    _libssh2_store_u32(buf, len_stored);
    if(len_stored) {
        memcpy(*buf, str, len_stored);
        *buf += len_stored;
    }

    assert(len_stored == len);
    return len_stored == len;
}


int _libssh2_store_bignum2_bytes(unsigned char **buf,
                                 const unsigned char *bytes,
                                 size_t len)
{
    uint32_t len_stored;
    uint32_t extraByte;
    const unsigned char *p;

    for(p = bytes; len > 0 && *p == 0; --len, ++p) {}

    extraByte = (len > 0 && (p[0] & 0x80) != 0);
    len_stored = (uint32_t)len;
    if(extraByte && len_stored == UINT32_MAX)
        len_stored--;
    _libssh2_store_u32(buf, len_stored + extraByte);

    if(extraByte) {
        *buf[0] = 0;
        *buf += 1;
    }

    if(len_stored) {
        memcpy(*buf, p, len_stored);
        *buf += len_stored;
    }

    assert(len_stored == len);
    return len_stored == len;
}



static const short base64_reverse_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};


LIBSSH2_API int
libssh2_base64_decode(LIBSSH2_SESSION *session, char **data,
                      unsigned int *datalen, const char *src,
                      unsigned int src_len)
{
    int rc;
    size_t dlen;

    rc = _libssh2_base64_decode(session, data, &dlen, src, src_len);

    if(datalen)
        *datalen = (unsigned int)dlen;

    return rc;
}


int _libssh2_base64_decode(LIBSSH2_SESSION *session,
                           char **data, size_t *datalen,
                           const char *src, size_t src_len)
{
    unsigned char *d;
    const char *s;
    short v;
    ssize_t i = 0, len = 0;

    *data = LIBSSH2_ALLOC(session, ((src_len / 4) * 3) + 1);
    d = (unsigned char *) *data;
    if(!d) {
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate memory for base64 decoding");
    }

    for(s = src; s < (src + src_len); s++) {
        v = base64_reverse_table[(unsigned char)*s];
        if(v < 0)
            continue;
        switch(i % 4) {
        case 0:
            d[len] = (unsigned char)(v << 2);
            break;
        case 1:
            d[len++] |= (unsigned char)(v >> 4);
            d[len] = (unsigned char)(v << 4);
            break;
        case 2:
            d[len++] |= (unsigned char)(v >> 2);
            d[len] = (unsigned char)(v << 6);
            break;
        case 3:
            d[len++] |= (unsigned char)v;
            break;
        }
        i++;
    }
    if((i % 4) == 1) {
        
        LIBSSH2_FREE(session, *data);
        *data = NULL;
        return _libssh2_error(session, LIBSSH2_ERROR_INVAL, "Invalid base64");
    }

    *datalen = len;
    return 0;
}


static const char table64[]=
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


size_t _libssh2_base64_encode(LIBSSH2_SESSION *session,
                              const char *inp, size_t insize, char **outptr)
{
    unsigned char ibuf[3];
    unsigned char obuf[4];
    int i;
    int inputparts;
    char *output;
    char *base64data;
    const char *indata = inp;

    *outptr = NULL; 

    if(insize == 0)
        insize = strlen(indata);

    base64data = output = LIBSSH2_ALLOC(session, insize * 4 / 3 + 4);
    if(!output)
        return 0;

    while(insize > 0) {
        for(i = inputparts = 0; i < 3; i++) {
            if(insize > 0) {
                inputparts++;
                ibuf[i] = *indata;
                indata++;
                insize--;
            }
            else
                ibuf[i] = 0;
        }

        obuf[0] = (unsigned char)  ((ibuf[0] & 0xFC) >> 2);
        obuf[1] = (unsigned char) (((ibuf[0] & 0x03) << 4) | \
                                   ((ibuf[1] & 0xF0) >> 4));
        obuf[2] = (unsigned char) (((ibuf[1] & 0x0F) << 2) | \
                                   ((ibuf[2] & 0xC0) >> 6));
        obuf[3] = (unsigned char)   (ibuf[2] & 0x3F);

        switch(inputparts) {
        case 1: 
            output[0] = table64[obuf[0]];
            output[1] = table64[obuf[1]];
            output[2] = '=';
            output[3] = '=';
            break;
        case 2: 
            output[0] = table64[obuf[0]];
            output[1] = table64[obuf[1]];
            output[2] = table64[obuf[2]];
            output[3] = '=';
            break;
        default:
            output[0] = table64[obuf[0]];
            output[1] = table64[obuf[1]];
            output[2] = table64[obuf[2]];
            output[3] = table64[obuf[3]];
            break;
        }
        output += 4;
    }
    *output = 0;
    *outptr = base64data; 

    return strlen(base64data); 
}


LIBSSH2_API void
libssh2_free(LIBSSH2_SESSION *session, void *ptr)
{
    LIBSSH2_FREE(session, ptr);
}

#ifdef LIBSSH2DEBUG
#include <stdarg.h>

LIBSSH2_API int
libssh2_trace(LIBSSH2_SESSION * session, int bitmask)
{
    session->showmask = bitmask;
    return 0;
}

LIBSSH2_API int
libssh2_trace_sethandler(LIBSSH2_SESSION *session, void *handler_context,
                         libssh2_trace_handler_func callback)
{
    session->tracehandler = callback;
    session->tracehandler_context = handler_context;
    return 0;
}

void
_libssh2_debug_low(LIBSSH2_SESSION * session, int context, const char *format,
                   ...)
{
    char buffer[1536];
    int len, msglen, buflen = sizeof(buffer);
    va_list vargs;
    struct timeval now;
    static long firstsec;
    static const char *const contexts[] = {
        "Unknown",
        "Transport",
        "Key Ex",
        "Userauth",
        "Conn",
        "SCP",
        "SFTP",
        "Failure Event",
        "Publickey",
        "Socket",
    };
    const char *contexttext = contexts[0];
    unsigned int contextindex;

    if(!(session->showmask & context)) {
        
        return;
    }

    
    for(contextindex = 0; contextindex < ARRAY_SIZE(contexts);
         contextindex++) {
        if((context & (1 << contextindex)) != 0) {
            contexttext = contexts[contextindex];
            break;
        }
    }

    gettimeofday(&now, NULL);
    if(!firstsec) {
        firstsec = now.tv_sec;
    }
    now.tv_sec -= firstsec;

    len = snprintf(buffer, buflen, "[libssh2] %d.%06d %s: ",
                   (int)now.tv_sec, (int)now.tv_usec, contexttext);

    if(len >= buflen)
        msglen = buflen - 1;
    else {
        buflen -= len;
        msglen = len;
        va_start(vargs, format);
        len = vsnprintf(buffer + msglen, buflen, format, vargs);
        va_end(vargs);
        msglen += len < buflen ? len : buflen - 1;
    }

    if(session->tracehandler)
        (session->tracehandler)(session, session->tracehandler_context, buffer,
                                msglen);
    else
        fprintf(stderr, "%s\n", buffer);
}

#else
LIBSSH2_API int
libssh2_trace(LIBSSH2_SESSION * session, int bitmask)
{
    (void)session;
    (void)bitmask;
    return 0;
}

LIBSSH2_API int
libssh2_trace_sethandler(LIBSSH2_SESSION *session, void *handler_context,
                         libssh2_trace_handler_func callback)
{
    (void)session;
    (void)handler_context;
    (void)callback;
    return 0;
}
#endif


void _libssh2_list_init(struct list_head *head)
{
    head->first = head->last = NULL;
}


void _libssh2_list_add(struct list_head *head,
                       struct list_node *entry)
{
    
    entry->head = head;

    
    entry->next = NULL;

    
    entry->prev = head->last;

    
    head->last = entry;

    
    if(entry->prev)
        entry->prev->next = entry;
    else
        head->first = entry;
}


void *_libssh2_list_first(struct list_head *head)
{
    return head->first;
}


void *_libssh2_list_next(struct list_node *node)
{
    return node->next;
}


void *_libssh2_list_prev(struct list_node *node)
{
    return node->prev;
}


void _libssh2_list_remove(struct list_node *entry)
{
    if(entry->prev)
        entry->prev->next = entry->next;
    else
        entry->head->first = entry->next;

    if(entry->next)
        entry->next->prev = entry->prev;
    else
        entry->head->last = entry->prev;
}

#if 0

void _libssh2_list_insert(struct list_node *after, 
                          struct list_node *entry)
{
    
    bentry->next = after;

    
    entry->prev = after->prev;

    
    if(entry->prev)
        entry->prev->next = entry;
    else
      
      after->head->first = entry;

    
    after->prev = entry;

    

    
    entry->head = after->head;
}

#endif


#ifdef LIBSSH2_GETTIMEOFDAY




int _libssh2_gettimeofday(struct timeval *tp, void *tzp)
{
    (void)tzp;
    if(tp) {
#ifdef _WIN32
        
        #define _WIN32_FT_OFFSET (116444736000000000)

        union {
            libssh2_uint64_t ns100; 
            FILETIME ft;
        } _now;
        GetSystemTimeAsFileTime(&_now.ft);
        tp->tv_usec = (long)((_now.ns100 / 10) % 1000000);
        tp->tv_sec = (long)((_now.ns100 - _WIN32_FT_OFFSET) / 10000000);
#else
        
        tp->tv_usec = 0;
        tp->tv_sec = 0;
#endif
    }
    
    return 0;
}
#endif

void *_libssh2_calloc(LIBSSH2_SESSION* session, size_t size)
{
    void *p = LIBSSH2_ALLOC(session, size);
    if(p) {
        memset(p, 0, size);
    }
    return p;
}


void _libssh2_xor_data(unsigned char *output,
                       const unsigned char *input1,
                       const unsigned char *input2,
                       size_t length)
{
    size_t i;

    for(i = 0; i < length; i++)
        *output++ = *input1++ ^ *input2++;
}


void _libssh2_aes_ctr_increment(unsigned char *ctr,
                                size_t length)
{
    unsigned char *pc;
    unsigned int val, carry;

    pc = ctr + length - 1;
    carry = 1;

    while(pc >= ctr) {
        val = (unsigned int)*pc + carry;
        *pc-- = val & 0xFF;
        carry = val >> 8;
    }
}

#ifdef LIBSSH2_MEMZERO
static void * (* const volatile memset_libssh)(void *, int, size_t) = memset;

void _libssh2_memzero(void *buf, size_t size)
{
    memset_libssh(buf, 0, size);
}
#endif



struct string_buf *_libssh2_string_buf_new(LIBSSH2_SESSION *session)
{
    struct string_buf *ret;

    ret = _libssh2_calloc(session, sizeof(*ret));
    if(!ret)
        return NULL;

    return ret;
}

void _libssh2_string_buf_free(LIBSSH2_SESSION *session, struct string_buf *buf)
{
    if(!buf)
        return;

    if(buf->data)
        LIBSSH2_FREE(session, buf->data);

    LIBSSH2_FREE(session, buf);
    buf = NULL;
}

int _libssh2_get_byte(struct string_buf *buf, unsigned char *out)
{
    if(!_libssh2_check_length(buf, 1)) {
        return -1;
    }

    *out = buf->dataptr[0];
    buf->dataptr += 1;
    return 0;
}

int _libssh2_get_boolean(struct string_buf *buf, unsigned char *out)
{
    if(!_libssh2_check_length(buf, 1)) {
        return -1;
    }


    *out = buf->dataptr[0] == 0 ? 0 : 1;
    buf->dataptr += 1;
    return 0;
}

int _libssh2_get_u32(struct string_buf *buf, uint32_t *out)
{
    if(!_libssh2_check_length(buf, 4)) {
        return -1;
    }

    *out = _libssh2_ntohu32(buf->dataptr);
    buf->dataptr += 4;
    return 0;
}

int _libssh2_get_u64(struct string_buf *buf, libssh2_uint64_t *out)
{
    if(!_libssh2_check_length(buf, 8)) {
        return -1;
    }

    *out = _libssh2_ntohu64(buf->dataptr);
    buf->dataptr += 8;
    return 0;
}

int _libssh2_match_string(struct string_buf *buf, const char *match)
{
    unsigned char *out;
    size_t len = 0;
    if(_libssh2_get_string(buf, &out, &len) || len != strlen(match) ||
        strncmp((char *)out, match, strlen(match)) != 0) {
        return -1;
    }
    return 0;
}

int _libssh2_get_string(struct string_buf *buf, unsigned char **outbuf,
                        size_t *outlen)
{
    uint32_t data_len;
    if(!buf || _libssh2_get_u32(buf, &data_len) != 0) {
        return -1;
    }
    if(!_libssh2_check_length(buf, data_len)) {
        return -1;
    }
    *outbuf = buf->dataptr;
    buf->dataptr += data_len;

    if(outlen)
        *outlen = (size_t)data_len;

    return 0;
}

int _libssh2_copy_string(LIBSSH2_SESSION *session, struct string_buf *buf,
                         unsigned char **outbuf, size_t *outlen)
{
    size_t str_len;
    unsigned char *str;

    if(_libssh2_get_string(buf, &str, &str_len)) {
        return -1;
    }

    if(str_len) {
        *outbuf = LIBSSH2_ALLOC(session, str_len);
        if(*outbuf) {
            memcpy(*outbuf, str, str_len);
        }
        else {
            return -1;
        }
    }
    else {
        *outbuf = NULL;
    }

    if(outlen)
        *outlen = str_len;

    return 0;
}

int _libssh2_get_bignum_bytes(struct string_buf *buf, unsigned char **outbuf,
                              size_t *outlen)
{
    uint32_t data_len;
    uint32_t bn_len;
    unsigned char *bnptr;

    if(_libssh2_get_u32(buf, &data_len)) {
        return -1;
    }
    if(!_libssh2_check_length(buf, data_len)) {
        return -1;
    }

    bn_len = data_len;
    bnptr = buf->dataptr;

    
    while(bn_len > 0 && *bnptr == 0x00) {
        bn_len--;
        bnptr++;
    }

    *outbuf = bnptr;
    buf->dataptr += data_len;

    if(outlen)
        *outlen = (size_t)bn_len;

    return 0;
}



int _libssh2_check_length(struct string_buf *buf, size_t len)
{
    unsigned char *endp = &buf->data[buf->len];
    size_t left = endp - buf->dataptr;
    return (len <= left) && (left <= buf->len);
}

int _libssh2_eob(struct string_buf *buf)
{
    unsigned char *endp = &buf->data[buf->len];
    return buf->dataptr >= endp;
}
