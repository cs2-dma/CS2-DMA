



#include "libssh2_priv.h"

#include <errno.h>
#include <ctype.h>
#include <assert.h>

#include "transport.h"
#include "mac.h"

#ifdef LIBSSH2DEBUG
#define UNPRINTABLE_CHAR '.'
static void
debugdump(LIBSSH2_SESSION * session,
          const char *desc, const unsigned char *ptr, size_t size)
{
    size_t i;
    size_t c;
    unsigned int width = 0x10;
    char buffer[256];  
    size_t used;
    static const char *hex_chars = "0123456789ABCDEF";

    if(!(session->showmask & LIBSSH2_TRACE_TRANS)) {
        
        return;
    }

    used = snprintf(buffer, sizeof(buffer), "=> %s (%lu bytes)\n",
                    desc, (unsigned long) size);
    if(session->tracehandler)
        (session->tracehandler)(session, session->tracehandler_context,
                                buffer, used);
    else
        fprintf(stderr, "%s", buffer);

    for(i = 0; i < size; i += width) {

        used = snprintf(buffer, sizeof(buffer), "%04lx: ", (long)i);

        
        for(c = 0; c < width; c++) {
            if(i + c < size) {
                buffer[used++] = hex_chars[(ptr[i + c] >> 4) & 0xF];
                buffer[used++] = hex_chars[ptr[i + c] & 0xF];
            }
            else {
                buffer[used++] = ' ';
                buffer[used++] = ' ';
            }

            buffer[used++] = ' ';
            if((width/2) - 1 == c)
                buffer[used++] = ' ';
        }

        buffer[used++] = ':';
        buffer[used++] = ' ';

        for(c = 0; (c < width) && (i + c < size); c++) {
            buffer[used++] = isprint(ptr[i + c]) ?
                ptr[i + c] : UNPRINTABLE_CHAR;
        }
        buffer[used++] = '\n';
        buffer[used] = 0;

        if(session->tracehandler)
            (session->tracehandler)(session, session->tracehandler_context,
                                    buffer, used);
        else
            fprintf(stderr, "%s", buffer);
    }
}
#else
#define debugdump(a,x,y,z) do {} while(0)
#endif




static int
decrypt(LIBSSH2_SESSION * session, unsigned char *source,
        unsigned char *dest, ssize_t len, int firstlast)
{
    struct transportpacket *p = &session->packet;
    int blocksize = session->remote.crypt->blocksize;

    
    if(!CRYPT_FLAG_L(session, PKTLEN_AAD))
        assert((len % blocksize) == 0);

    while(len > 0) {
        
        ssize_t decryptlen = LIBSSH2_MIN(blocksize, len);
        
        int lowerfirstlast = IS_FIRST(firstlast) ? FIRST_BLOCK :
            ((len <= blocksize) ? firstlast : MIDDLE_BLOCK);
        
        if(CRYPT_FLAG_L(session, PKTLEN_AAD) && IS_LAST(firstlast)
           && (len < blocksize*2)) {
            decryptlen = len;
            lowerfirstlast = LAST_BLOCK;
        }

        if(session->remote.crypt->crypt(session, 0, source, decryptlen,
                                        &session->remote.crypt_abstract,
                                        lowerfirstlast)) {
            LIBSSH2_FREE(session, p->payload);
            return LIBSSH2_ERROR_DECRYPT;
        }

        
        memcpy(dest, source, decryptlen);

        len -= decryptlen;       
        dest += decryptlen;      
        source += decryptlen;    
    }
    return LIBSSH2_ERROR_NONE;         
}


static int
fullpacket(LIBSSH2_SESSION * session, int encrypted  )
{
    unsigned char macbuf[MAX_MACSIZE];
    struct transportpacket *p = &session->packet;
    int rc;
    int compressed;
    const LIBSSH2_MAC_METHOD *remote_mac = NULL;
    uint32_t seq = session->remote.seqno;

    if(!encrypted || (!CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET) &&
                      !CRYPT_FLAG_R(session, INTEGRATED_MAC))) {
        remote_mac = session->remote.mac;
    }

    if(session->fullpacket_state == libssh2_NB_state_idle) {
        session->fullpacket_macstate = LIBSSH2_MAC_CONFIRMED;
        session->fullpacket_payload_len = p->packet_length - 1;

        if(encrypted && remote_mac) {

            
            int etm = remote_mac->etm;
            size_t mac_len = remote_mac->mac_len;
            if(etm) {
                
                remote_mac->hash(session, macbuf,
                                 session->remote.seqno,
                                 p->payload, p->total_num - mac_len,
                                 NULL, 0,
                                 &session->remote.mac_abstract);
            }
            else {
                
                remote_mac->hash(session, macbuf,
                                 session->remote.seqno,
                                 p->init, 5,
                                 p->payload,
                                 session->fullpacket_payload_len,
                                 &session->remote.mac_abstract);
            }

            
            if(memcmp(macbuf, p->payload + p->total_num - mac_len, mac_len)) {
                _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                               "Failed MAC check"));
                session->fullpacket_macstate = LIBSSH2_MAC_INVALID;

            }
            else if(etm) {
                
                unsigned char first_block[MAX_BLOCKSIZE];
                ssize_t decrypt_size;
                unsigned char *decrypt_buffer;
                int blocksize = session->remote.crypt->blocksize;

                rc = decrypt(session, p->payload + 4,
                             first_block, blocksize, FIRST_BLOCK);
                if(rc) {
                    return rc;
                }

                
                decrypt_size = p->total_num - mac_len - 4;
                decrypt_buffer = LIBSSH2_ALLOC(session, decrypt_size);
                if(!decrypt_buffer) {
                    return LIBSSH2_ERROR_ALLOC;
                }

                
                p->padding_length = first_block[0];
                if(blocksize > 1) {
                    memcpy(decrypt_buffer, first_block + 1, blocksize - 1);
                }

                
                if(blocksize < decrypt_size) {
                    rc = decrypt(session, p->payload + blocksize + 4,
                                 decrypt_buffer + blocksize - 1,
                                 decrypt_size - blocksize, LAST_BLOCK);
                    if(rc) {
                        LIBSSH2_FREE(session, decrypt_buffer);
                        return rc;
                    }
                }

                
                LIBSSH2_FREE(session, p->payload);
                p->payload = decrypt_buffer;
            }
        }
        else if(encrypted && CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
            
            memmove(p->payload, &p->payload[1], p->packet_length - 1);
        }

        session->remote.seqno++;

        
        session->fullpacket_payload_len -= p->padding_length;

        
        compressed = session->local.comp &&
                     session->local.comp->compress &&
                     ((session->state & LIBSSH2_STATE_AUTHENTICATED) ||
                      session->local.comp->use_in_auth);

        if(compressed && session->remote.comp_abstract) {
            

            unsigned char *data;
            size_t data_len;
            rc = session->remote.comp->decomp(session,
                                              &data, &data_len,
                                              LIBSSH2_PACKET_MAXDECOMP,
                                              p->payload,
                                              session->fullpacket_payload_len,
                                              &session->remote.comp_abstract);
            LIBSSH2_FREE(session, p->payload);
            if(rc)
                return rc;

            p->payload = data;
            session->fullpacket_payload_len = data_len;
        }

        session->fullpacket_packet_type = p->payload[0];

        debugdump(session, "libssh2_transport_read() plain",
                  p->payload, session->fullpacket_payload_len);

        session->fullpacket_state = libssh2_NB_state_created;
    }

    if(session->fullpacket_state == libssh2_NB_state_created) {
        rc = _libssh2_packet_add(session, p->payload,
                                 session->fullpacket_payload_len,
                                 session->fullpacket_macstate, seq);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
        if(rc) {
            session->fullpacket_state = libssh2_NB_state_idle;
            return rc;
        }
    }

    session->fullpacket_state = libssh2_NB_state_idle;

    if(session->kex_strict &&
        session->fullpacket_packet_type == SSH_MSG_NEWKEYS) {
        session->remote.seqno = 0;
    }

    return session->fullpacket_packet_type;
}





int _libssh2_transport_read(LIBSSH2_SESSION * session)
{
    int rc;
    struct transportpacket *p = &session->packet;
    ssize_t remainpack; 
    ssize_t remainbuf;  
    ssize_t numbytes;   
    ssize_t numdecrypt; 
    unsigned char block[MAX_BLOCKSIZE]; 
    int blocksize;  
    int encrypted = 1; 
    int firstlast = FIRST_BLOCK; 
    unsigned int auth_len = 0; 
    const LIBSSH2_MAC_METHOD *remote_mac = NULL; 

    
    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_INBOUND;

    

    if(session->state & LIBSSH2_STATE_EXCHANGING_KEYS &&
        !(session->state & LIBSSH2_STATE_KEX_ACTIVE)) {

        
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS, "Redirecting into the"
                       " key re-exchange from _libssh2_transport_read"));
        rc = _libssh2_kex_exchange(session, 1, &session->startup_key_state);
        if(rc)
            return rc;
    }

    
    if(session->readPack_state == libssh2_NB_state_jump1) {
        session->readPack_state = libssh2_NB_state_idle;
        encrypted = session->readPack_encrypted;
        goto libssh2_transport_read_point1;
    }

    do {
        int etm;
        if(session->socket_state == LIBSSH2_SOCKET_DISCONNECTED) {
            return LIBSSH2_ERROR_SOCKET_DISCONNECT;
        }

        if(session->state & LIBSSH2_STATE_NEWKEYS) {
            blocksize = session->remote.crypt->blocksize;
        }
        else {
            encrypted = 0;      
            blocksize = 5;      
        }

        if(encrypted) {
            if(CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
                auth_len = session->remote.crypt->auth_len;
            }
            else {
                remote_mac = session->remote.mac;
            }
        }

        etm = encrypted && remote_mac ? remote_mac->etm : 0;

        

        
        remainbuf = p->writeidx - p->readidx;

        
        assert(remainbuf >= 0);

        if(remainbuf < blocksize ||
           (CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)
            && ((ssize_t)p->total_num) > remainbuf)) {
            
            ssize_t nread;

            
            if(remainbuf) {
                memmove(p->buf, &p->buf[p->readidx], remainbuf);
                p->readidx = 0;
                p->writeidx = remainbuf;
            }
            else {
                
                p->readidx = p->writeidx = 0;
            }

            
            nread = LIBSSH2_RECV(session, &p->buf[remainbuf],
                                 PACKETBUFSIZE - remainbuf,
                                 LIBSSH2_SOCKET_RECV_FLAGS(session));
            if(nread <= 0) {
                
                if((nread < 0) && (nread == -EAGAIN)) {
                    session->socket_block_directions |=
                        LIBSSH2_SESSION_BLOCK_INBOUND;
                    return LIBSSH2_ERROR_EAGAIN;
                }
                _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                               "Error recving %ld bytes (got %ld)",
                               (long)(PACKETBUFSIZE - remainbuf),
                               (long)-nread));
                return LIBSSH2_ERROR_SOCKET_RECV;
            }
            _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                           "Recved %ld/%ld bytes to %p+%ld", (long)nread,
                           (long)(PACKETBUFSIZE - remainbuf), (void *)p->buf,
                           (long)remainbuf));

            debugdump(session, "libssh2_transport_read() raw",
                      &p->buf[remainbuf], nread);
            
            p->writeidx += nread;

            
            remainbuf = p->writeidx - p->readidx;
        }

        
        numbytes = remainbuf;

        if(!p->total_num) {
            size_t total_num; 

            
            ssize_t required_size = etm ? 4 : blocksize;

            

            if(numbytes < required_size) {
                
                session->socket_block_directions |=
                    LIBSSH2_SESSION_BLOCK_INBOUND;
                return LIBSSH2_ERROR_EAGAIN;
            }

            if(etm) {
                
                memcpy(block, &p->buf[p->readidx], 4);
                memcpy(p->init, &p->buf[p->readidx], 4);
            }
            else if(encrypted && session->remote.crypt->get_len) {
                unsigned int len = 0;
                unsigned char *ptr = NULL;

                rc = session->remote.crypt->get_len(session,
                                            session->remote.seqno,
                                            &p->buf[p->readidx],
                                            numbytes,
                                            &len,
                                            &session->remote.crypt_abstract);

                if(rc != LIBSSH2_ERROR_NONE) {
                    p->total_num = 0;   
                    if(p->payload)
                        LIBSSH2_FREE(session, p->payload);
                    p->payload = NULL;
                    return rc;
                }

                
                ptr = &block[0];
                _libssh2_store_u32(&ptr, len);

                ptr = &p->init[0];
                _libssh2_store_u32(&ptr, len);
            }
            else {
                if(encrypted) {
                    
                    rc = decrypt(session, &p->buf[p->readidx],
                                 block, blocksize, FIRST_BLOCK);
                    if(rc != LIBSSH2_ERROR_NONE) {
                        return rc;
                    }
                    
                    memcpy(p->init, block, 5);
                }
                else {
                    
                    memcpy(block, &p->buf[p->readidx], blocksize);
                }

                
                p->readidx += blocksize;

                
                p->packet_length = _libssh2_ntohu32(block);
            }

            if(!encrypted || !CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
                if(p->packet_length < 1) {
                    return LIBSSH2_ERROR_DECRYPT;
                }
                else if(p->packet_length > LIBSSH2_PACKET_MAXPAYLOAD) {
                    return LIBSSH2_ERROR_OUT_OF_BOUNDARY;
                }

                if(etm) {
                    
                    p->packet_length = _libssh2_ntohu32(block);
                    total_num = 4 + p->packet_length +
                    remote_mac->mac_len;
                }
                else {
                    
                    p->padding_length = block[4];
                    if(p->padding_length > p->packet_length - 1) {
                        return LIBSSH2_ERROR_DECRYPT;
                    }

                    
                    total_num = p->packet_length - 1 +
                    (encrypted ? remote_mac->mac_len : 0);
                }
            }
            else {
                

                
                total_num = 4;

                p->packet_length = _libssh2_ntohu32(block);
                if(p->packet_length < 1)
                    return LIBSSH2_ERROR_DECRYPT;

                

                total_num += p->packet_length +
                    (remote_mac ? remote_mac->mac_len : 0) + auth_len;

                
                p->padding_length = 0;
            }

            
            if(total_num > LIBSSH2_PACKET_MAXPAYLOAD || total_num == 0) {
                return LIBSSH2_ERROR_OUT_OF_BOUNDARY;
            }

            
            p->payload = LIBSSH2_ALLOC(session, total_num);
            if(!p->payload) {
                return LIBSSH2_ERROR_ALLOC;
            }
            p->total_num = total_num;
            
            p->wptr = p->payload;

            if(!encrypted || !CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
                if(!etm && blocksize > 5) {
                    
                    if(blocksize - 5 <= (int) total_num) {
                        memcpy(p->wptr, &block[5], blocksize - 5);
                        p->wptr += blocksize - 5; 
                        if(etm) {
                            
                            p->wptr += 4;
                        }
                    }
                    else {
                        if(p->payload)
                            LIBSSH2_FREE(session, p->payload);
                        return LIBSSH2_ERROR_OUT_OF_BOUNDARY;
                    }
                }

                
                p->data_num = p->wptr - p->payload;

                
                if(!etm)
                    numbytes -= blocksize;
            }
            else {
                
                p->data_num = 0;

                
                if(!encrypted)
                    numbytes -= 4;
            }
        }

        
        remainpack = p->total_num - p->data_num;

        if(numbytes > remainpack) {
            
            numbytes = remainpack;
        }

        if(encrypted && CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
            if(numbytes < remainpack) {
                
                session->socket_block_directions |=
                LIBSSH2_SESSION_BLOCK_INBOUND;
                return LIBSSH2_ERROR_EAGAIN;
            }

            
            numbytes -= 4;
            p->total_num -= 4;
        }

        if(encrypted && !etm) {
            
            int skip = (remote_mac ? remote_mac->mac_len : 0) + auth_len;

            if(CRYPT_FLAG_R(session, INTEGRATED_MAC))
                
                skip = 0;

            
            if((p->data_num + numbytes) >= (p->total_num - skip)) {
                
                numdecrypt = LIBSSH2_MAX(0,
                    (int)(p->total_num - skip) - (int)p->data_num);
                firstlast = LAST_BLOCK;
            }
            else {
                ssize_t frac;
                numdecrypt = numbytes;
                frac = numdecrypt % blocksize;
                if(frac) {
                    
                    numdecrypt -= frac;
                    
                    numbytes = 0;
                }
                if(CRYPT_FLAG_R(session, INTEGRATED_MAC)) {
                    
                    numdecrypt = LIBSSH2_MIN(numdecrypt,
                        (int)(p->total_num - skip - blocksize - p->data_num));
                    numbytes = 0;
                }
                firstlast = MIDDLE_BLOCK;
            }
        }
        else {
            
            numdecrypt = 0;
        }
        assert(numdecrypt >= 0);

        
        if(numdecrypt > 0) {
            
            if(CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
                rc = session->remote.crypt->crypt(session,
                                               session->remote.seqno,
                                               &p->buf[p->readidx],
                                               numdecrypt,
                                               &session->remote.crypt_abstract,
                                               0);

                if(rc != LIBSSH2_ERROR_NONE) {
                    p->total_num = 0;   
                    return rc;
                }

                memcpy(p->wptr, &p->buf[p->readidx], numbytes);

                
                p->readidx += 4;

                
                numdecrypt += auth_len;

                
                p->padding_length = p->wptr[0];

                if(p->padding_length > p->packet_length - 1) {
                    return LIBSSH2_ERROR_DECRYPT;
                }
            }
            else {
                rc = decrypt(session, &p->buf[p->readidx], p->wptr, numdecrypt,
                             firstlast);

                if(rc != LIBSSH2_ERROR_NONE) {
                    p->total_num = 0;   
                    return rc;
                }
            }

            
            p->readidx += numdecrypt;
            
            p->wptr += numdecrypt;
            
            p->data_num += numdecrypt;

            
            numbytes -= numdecrypt;
        }

        
        if(numbytes > 0) {

            if((size_t)numbytes <= (p->total_num - (p->wptr - p->payload))) {
                memcpy(p->wptr, &p->buf[p->readidx], numbytes);
            }
            else {
                if(p->payload)
                    LIBSSH2_FREE(session, p->payload);
                return LIBSSH2_ERROR_OUT_OF_BOUNDARY;
            }

            
            p->readidx += numbytes;
            
            p->wptr += numbytes;
            
            p->data_num += numbytes;
        }

        
        remainpack = p->total_num - p->data_num;

        if(!remainpack) {
            
libssh2_transport_read_point1:
            rc = fullpacket(session, encrypted);
            if(rc == LIBSSH2_ERROR_EAGAIN) {

                if(session->packAdd_state != libssh2_NB_state_idle) {
                    
                    session->readPack_encrypted = encrypted;
                    session->readPack_state = libssh2_NB_state_jump1;
                }

                return rc;
            }

            p->total_num = 0;   

            return rc;
        }
    } while(1);                

    return LIBSSH2_ERROR_SOCKET_RECV; 
}

static int
send_existing(LIBSSH2_SESSION *session, const unsigned char *data,
              size_t data_len, ssize_t *ret)
{
    ssize_t rc;
    ssize_t length;
    struct transportpacket *p = &session->packet;

    if(!p->olen) {
        *ret = 0;
        return LIBSSH2_ERROR_NONE;
    }

    
    if((data != p->odata) || (data_len != p->olen)) {
        
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Address is different, returning EAGAIN"));
        return LIBSSH2_ERROR_EAGAIN;
    }

    *ret = 1;                   

    
    length = p->ototal_num - p->osent;

    rc = LIBSSH2_SEND(session, &p->outbuf[p->osent], length,
                      LIBSSH2_SOCKET_SEND_FLAGS(session));
    if(rc < 0)
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Error sending %ld bytes: %ld",
                       (long)length, (long)-rc));
    else {
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Sent %ld/%ld bytes at %p+%lu", (long)rc, (long)length,
                       (void *)p->outbuf, (unsigned long)p->osent));
        debugdump(session, "libssh2_transport_write send()",
                  &p->outbuf[p->osent], rc);
    }

    if(rc == length) {
        
        p->ototal_num = 0;
        p->olen = 0;
        
        return LIBSSH2_ERROR_NONE;

    }
    else if(rc < 0) {
        
        if(rc != -EAGAIN)
            
            return LIBSSH2_ERROR_SOCKET_SEND;

        session->socket_block_directions |= LIBSSH2_SESSION_BLOCK_OUTBOUND;
        return LIBSSH2_ERROR_EAGAIN;
    }

    p->osent += rc;         

    return rc < length ? LIBSSH2_ERROR_EAGAIN : LIBSSH2_ERROR_NONE;
}


int _libssh2_transport_send(LIBSSH2_SESSION *session,
                            const unsigned char *data, size_t data_len,
                            const unsigned char *data2, size_t data2_len)
{
    int blocksize =
        (session->state & LIBSSH2_STATE_NEWKEYS) ?
        session->local.crypt->blocksize : 8;
    ssize_t padding_length;
    size_t packet_length;
    ssize_t total_length;
#ifdef LIBSSH2_RANDOM_PADDING
    int rand_max;
    int seed = data[0];         
#endif
    struct transportpacket *p = &session->packet;
    int encrypted;
    int compressed;
    int etm;
    ssize_t ret;
    int rc;
    const unsigned char *orgdata = data;
    const LIBSSH2_MAC_METHOD *local_mac = NULL;
    unsigned int auth_len = 0;
    size_t orgdata_len = data_len;
    size_t crypt_offset, etm_crypt_offset;

    
    if(session->state & LIBSSH2_STATE_EXCHANGING_KEYS &&
        !(session->state & LIBSSH2_STATE_KEX_ACTIVE)) {
        
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS, "Redirecting into the"
                       " key re-exchange from _libssh2_transport_send"));
        rc = _libssh2_kex_exchange(session, 1, &session->startup_key_state);
        if(rc)
            return rc;
    }

    debugdump(session, "libssh2_transport_write plain", data, data_len);
    if(data2)
        debugdump(session, "libssh2_transport_write plain2", data2, data2_len);

    
    rc = send_existing(session, data, data_len, &ret);
    if(rc)
        return rc;

    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_OUTBOUND;

    if(ret)
        
        return rc;

    encrypted = (session->state & LIBSSH2_STATE_NEWKEYS) ? 1 : 0;

    if(encrypted && session->local.crypt &&
        CRYPT_FLAG_R(session, REQUIRES_FULL_PACKET)) {
        auth_len = session->local.crypt->auth_len;
    }
    else {
        local_mac = session->local.mac;
    }

    etm = encrypted && local_mac ? local_mac->etm : 0;

    compressed = session->local.comp &&
                 session->local.comp->compress &&
                 ((session->state & LIBSSH2_STATE_AUTHENTICATED) ||
                  session->local.comp->use_in_auth);

    if(encrypted && compressed && session->local.comp_abstract) {
        
        size_t dest_len = MAX_SSH_PACKET_LEN-5-256;
        size_t dest2_len = dest_len;

        
        rc = session->local.comp->comp(session,
                                       &p->outbuf[5], &dest_len,
                                       data, data_len,
                                       &session->local.comp_abstract);
        if(rc)
            return rc;     

        if(data2 && data2_len) {
            
            dest2_len -= dest_len;

            rc = session->local.comp->comp(session,
                                           &p->outbuf[5 + dest_len],
                                           &dest2_len,
                                           data2, data2_len,
                                           &session->local.comp_abstract);
        }
        else
            dest2_len = 0;
        if(rc)
            return rc;     

        data_len = dest_len + dest2_len; 
    }
    else {
        if((data_len + data2_len) >= (MAX_SSH_PACKET_LEN-0x100))
            
            return LIBSSH2_ERROR_INVAL;

        
        memcpy(&p->outbuf[5], data, data_len);
        if(data2 && data2_len)
            memcpy(&p->outbuf[5 + data_len], data2, data2_len);
        data_len += data2_len; 
    }


    

    

    packet_length = data_len + 1 + 4;   
    
    crypt_offset = (etm || auth_len ||
                    (encrypted && CRYPT_FLAG_R(session, PKTLEN_AAD)))
                   ? 4 : 0;
    etm_crypt_offset = etm ? 4 : 0;

    

    
    padding_length = blocksize - ((packet_length - crypt_offset) % blocksize);

    
    if(padding_length < 4) {
        padding_length += blocksize;
    }
#ifdef LIBSSH2_RANDOM_PADDING
    

    
    rand_max = (255 - padding_length) / blocksize + 1;
    padding_length += blocksize * (seed % rand_max);
#endif

    packet_length += padding_length;

    
    total_length =
        packet_length + (encrypted && local_mac ? local_mac->mac_len : 0);

    total_length += auth_len;

    
    _libssh2_htonu32(p->outbuf, (uint32_t)(packet_length - 4));
    
    p->outbuf[4] = (unsigned char)padding_length;

    
    if(_libssh2_random(p->outbuf + 5 + data_len, padding_length)) {
        return _libssh2_error(session, LIBSSH2_ERROR_RANDGEN,
                              "Unable to get random bytes for packet padding");
    }

    if(encrypted) {
        size_t i;

        
        if(!etm && local_mac && !CRYPT_FLAG_L(session, INTEGRATED_MAC)) {
            if(local_mac->hash(session, p->outbuf + packet_length,
                               session->local.seqno, p->outbuf,
                               packet_length, NULL, 0,
                               &session->local.mac_abstract))
                return _libssh2_error(session, LIBSSH2_ERROR_MAC_FAILURE,
                                      "Failed to calculate MAC");
        }

        if(CRYPT_FLAG_L(session, REQUIRES_FULL_PACKET)) {
            if(session->local.crypt->crypt(session,
                                           session->local.seqno,
                                           p->outbuf,
                                           packet_length,
                                           &session->local.crypt_abstract,
                                           0)) {
                return LIBSSH2_ERROR_ENCRYPT;
            }
        }
        else {
            
            
            for(i = etm_crypt_offset; i < packet_length;
                i += session->local.crypt->blocksize) {
                unsigned char *ptr = &p->outbuf[i];
                size_t bsize = LIBSSH2_MIN(session->local.crypt->blocksize,
                                           (int)(packet_length-i));
                
                int firstlast = i == 0 ? FIRST_BLOCK :
                (!CRYPT_FLAG_L(session, INTEGRATED_MAC)
                 && (i == packet_length - session->local.crypt->blocksize)
                 ? LAST_BLOCK : MIDDLE_BLOCK);
                
                if(!CRYPT_FLAG_L(session, INTEGRATED_MAC))
                    if(i > packet_length - 2*bsize) {
                        
                        bsize = packet_length - i;
                        
                        i += bsize - session->local.crypt->blocksize;
                    }
                _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                                "crypting bytes %lu-%lu", (unsigned long)i,
                                (unsigned long)(i + bsize - 1)));
                if(session->local.crypt->crypt(session, 0, ptr,
                                               bsize,
                                               &session->local.crypt_abstract,
                                               firstlast))
                    return LIBSSH2_ERROR_ENCRYPT;     
            }

            
            if(CRYPT_FLAG_L(session, INTEGRATED_MAC)) {
                int authlen = local_mac->mac_len;
                assert((size_t)total_length <=
                       packet_length + session->local.crypt->blocksize);
                if(session->local.crypt->crypt(session,
                                               0,
                                               &p->outbuf[packet_length],
                                               authlen,
                                               &session->local.crypt_abstract,
                                               LAST_BLOCK))
                    return LIBSSH2_ERROR_ENCRYPT;     
            }
        }

        if(etm) {
            
            if(local_mac->hash(session, p->outbuf + packet_length,
                               session->local.seqno, p->outbuf,
                               packet_length, NULL, 0,
                               &session->local.mac_abstract))
                return _libssh2_error(session, LIBSSH2_ERROR_MAC_FAILURE,
                                      "Failed to calculate MAC");
        }
    }

    session->local.seqno++;

    if(session->kex_strict && data[0] == SSH_MSG_NEWKEYS) {
        session->local.seqno = 0;
    }

    ret = LIBSSH2_SEND(session, p->outbuf, total_length,
                       LIBSSH2_SOCKET_SEND_FLAGS(session));
    if(ret < 0)
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Error sending %ld bytes: %ld",
                       (long)total_length, (long)-ret));
    else {
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Sent %ld/%ld bytes at %p",
                       (long)ret, (long)total_length, (void *)p->outbuf));
        debugdump(session, "libssh2_transport_write send()", p->outbuf, ret);
    }

    if(ret != total_length) {
        if(ret >= 0 || ret == -EAGAIN) {
            
            session->socket_block_directions |= LIBSSH2_SESSION_BLOCK_OUTBOUND;
            p->odata = orgdata;
            p->olen = orgdata_len;
            p->osent = ret <= 0 ? 0 : ret;
            p->ototal_num = total_length;
            return LIBSSH2_ERROR_EAGAIN;
        }
        return LIBSSH2_ERROR_SOCKET_SEND;
    }

    
    p->odata = NULL;
    p->olen = 0;

    return LIBSSH2_ERROR_NONE;         
}
