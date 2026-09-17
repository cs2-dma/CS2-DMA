

#include "libssh2_priv.h"

#ifdef LIBSSH2_HAVE_ZLIB
#include <zlib.h>
#undef compress 
#endif

#include "comp.h"




static int
comp_method_none_comp(LIBSSH2_SESSION *session,
                      unsigned char *dest,
                      size_t *dest_len,
                      const unsigned char *src,
                      size_t src_len,
                      void **abstract)
{
    (void)session;
    (void)abstract;
    (void)dest;
    (void)dest_len;
    (void)src;
    (void)src_len;

    return 0;
}


static int
comp_method_none_decomp(LIBSSH2_SESSION * session,
                        unsigned char **dest,
                        size_t *dest_len,
                        size_t payload_limit,
                        const unsigned char *src,
                        size_t src_len, void **abstract)
{
    (void)session;
    (void)payload_limit;
    (void)abstract;
    *dest = (unsigned char *) src;
    *dest_len = src_len;
    return 0;
}



static const LIBSSH2_COMP_METHOD comp_method_none = {
    "none",
    0, 
    0, 
    NULL,
    comp_method_none_comp,
    comp_method_none_decomp,
    NULL
};

#ifdef LIBSSH2_HAVE_ZLIB




static voidpf
comp_method_zlib_alloc(voidpf opaque, uInt items, uInt size)
{
    LIBSSH2_SESSION *session = (LIBSSH2_SESSION *) opaque;

    return (voidpf) LIBSSH2_ALLOC(session, items * size);
}

static void
comp_method_zlib_free(voidpf opaque, voidpf address)
{
    LIBSSH2_SESSION *session = (LIBSSH2_SESSION *) opaque;

    LIBSSH2_FREE(session, address);
}




static int
comp_method_zlib_init(LIBSSH2_SESSION * session, int compr,
                      void **abstract)
{
    z_stream *strm;
    int status;

    strm = LIBSSH2_CALLOC(session, sizeof(z_stream));
    if(!strm) {
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate memory for "
                              "zlib compression/decompression");
    }

    strm->opaque = (voidpf) session;
    strm->zalloc = (alloc_func) comp_method_zlib_alloc;
    strm->zfree = (free_func) comp_method_zlib_free;
    if(compr) {
        
        status = deflateInit(strm, Z_DEFAULT_COMPRESSION);
    }
    else {
        
        status = inflateInit(strm);
    }

    if(status != Z_OK) {
        LIBSSH2_FREE(session, strm);
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "unhandled zlib error %d", status));
        return LIBSSH2_ERROR_COMPRESS;
    }
    *abstract = strm;

    return LIBSSH2_ERROR_NONE;
}


static int
comp_method_zlib_comp(LIBSSH2_SESSION *session,
                      unsigned char *dest,

                      
                      size_t *dest_len,
                      const unsigned char *src,
                      size_t src_len,
                      void **abstract)
{
    z_stream *strm = *abstract;
    uInt out_maxlen = (uInt)*dest_len;
    int status;

    strm->next_in = (unsigned char *) src;
    strm->avail_in = (uInt)src_len;
    strm->next_out = dest;
    strm->avail_out = out_maxlen;

    status = deflate(strm, Z_PARTIAL_FLUSH);

    if((status == Z_OK) && (strm->avail_out > 0)) {
        *dest_len = out_maxlen - strm->avail_out;
        return 0;
    }

    _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                   "unhandled zlib compression error %d, avail_out %u",
                   status, strm->avail_out));
    return _libssh2_error(session, LIBSSH2_ERROR_ZLIB, "compression failure");
}


static int
comp_method_zlib_decomp(LIBSSH2_SESSION * session,
                        unsigned char **dest,
                        size_t *dest_len,
                        size_t payload_limit,
                        const unsigned char *src,
                        size_t src_len, void **abstract)
{
    z_stream *strm = *abstract;
    
    char *out;
    size_t out_maxlen;

    if(src_len <= SIZE_MAX / 4)
        out_maxlen = (uInt)src_len * 4;
    else
        out_maxlen = payload_limit;

    
    if(!strm)
        return _libssh2_error(session, LIBSSH2_ERROR_COMPRESS,
                              "decompression uninitialized");

    
    if(out_maxlen < 25)
        out_maxlen = 25;

    if(out_maxlen > payload_limit)
        out_maxlen = payload_limit;

    strm->next_in = (unsigned char *) src;
    strm->avail_in = (uInt)src_len;
    strm->next_out = (unsigned char *) LIBSSH2_ALLOC(session,
                                                     (uInt)out_maxlen);
    out = (char *) strm->next_out;
    strm->avail_out = (uInt)out_maxlen;
    if(!strm->next_out)
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate decompression buffer");

    
    for(;;) {
        int status;
        size_t out_ofs;
        char *newout;

        status = inflate(strm, Z_PARTIAL_FLUSH);

        if(status == Z_OK) {
            if(strm->avail_out > 0)
                
                break;
        }
        else if(status == Z_BUF_ERROR) {
            
            break;
        }
        else {
            
            LIBSSH2_FREE(session, out);
            _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                           "unhandled zlib error %d", status));
            return _libssh2_error(session, LIBSSH2_ERROR_ZLIB,
                                  "decompression failure");
        }

        if(out_maxlen > payload_limit || out_maxlen > SIZE_MAX / 2) {
            LIBSSH2_FREE(session, out);
            return _libssh2_error(session, LIBSSH2_ERROR_ZLIB,
                                  "Excessive growth in decompression phase");
        }

        
        out_ofs = out_maxlen - strm->avail_out;
        out_maxlen *= 2;
        newout = LIBSSH2_REALLOC(session, out, out_maxlen);
        if(!newout) {
            LIBSSH2_FREE(session, out);
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to expand decompression buffer");
        }
        out = newout;
        strm->next_out = (unsigned char *) out + out_ofs;
        strm->avail_out = (uInt)(out_maxlen - out_ofs);
    }

    *dest = (unsigned char *) out;
    *dest_len = out_maxlen - strm->avail_out;

    return 0;
}



static int
comp_method_zlib_dtor(LIBSSH2_SESSION *session, int compr, void **abstract)
{
    z_stream *strm = *abstract;

    if(strm) {
        if(compr)
            deflateEnd(strm);
        else
            inflateEnd(strm);
        LIBSSH2_FREE(session, strm);
    }

    *abstract = NULL;
    return 0;
}

static const LIBSSH2_COMP_METHOD comp_method_zlib = {
    "zlib",
    1, 
    1, 
    comp_method_zlib_init,
    comp_method_zlib_comp,
    comp_method_zlib_decomp,
    comp_method_zlib_dtor,
};

static const LIBSSH2_COMP_METHOD comp_method_zlib_openssh = {
    "zlib@openssh.com",
    1, 
    0, 
    comp_method_zlib_init,
    comp_method_zlib_comp,
    comp_method_zlib_decomp,
    comp_method_zlib_dtor,
};
#endif 


static const LIBSSH2_COMP_METHOD *comp_methods[] = {
#ifdef LIBSSH2_HAVE_ZLIB
    &comp_method_zlib,
    &comp_method_zlib_openssh,
#endif 
    &comp_method_none,
    NULL
};


static const LIBSSH2_COMP_METHOD *no_comp_methods[] = {
    &comp_method_none,
    NULL
};

const LIBSSH2_COMP_METHOD **
_libssh2_comp_methods(LIBSSH2_SESSION *session)
{
    if(session->flag.compress)
        return comp_methods;
    else
        return no_comp_methods;
}
