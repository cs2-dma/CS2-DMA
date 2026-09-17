

#include "libssh2_priv.h"

#include "channel.h"
#include "session.h"

#include <stdlib.h>  

#if defined(HAVE_STRTOLL)
#define scpsize_strtol strtoll
#elif defined(HAVE_STRTOI64)
#define scpsize_strtol _strtoi64
#else
#define scpsize_strtol strtol
#endif


#define _libssh2_shell_quotedsize(s)     (3 * strlen(s) + 2)



static size_t
shell_quotearg(const char *path, unsigned char *buf,
               size_t bufsize)
{
    const char *src;
    unsigned char *dst, *endp;

    
    enum { UQSTRING, SQSTRING, QSTRING } state = UQSTRING;

    endp = &buf[bufsize];
    src = path;
    dst = buf;
    while(*src && dst < endp - 1) {

        switch(*src) {
            

        case '\'':
            switch(state) {
            case UQSTRING:      
                if(dst + 1 >= endp)
                    return 0;
                *dst++ = '"';
                break;
            case QSTRING:       
                break;
            case SQSTRING:      
                if(dst + 2 >= endp)
                    return 0;
                *dst++ = '\'';
                *dst++ = '"';
                break;
            default:
                break;
            }
            state = QSTRING;
            break;

            

        case '!':
            switch(state) {
            case UQSTRING:
                if(dst + 1 >= endp)
                    return 0;
                *dst++ = '\\';
                break;
            case QSTRING:
                if(dst + 2 >= endp)
                    return 0;
                *dst++ = '"';           
                *dst++ = '\\';
                break;
            case SQSTRING:              
                if(dst + 2 >= endp)
                    return 0;
                *dst++ = '\'';
                *dst++ = '\\';
                break;
            default:
                break;
            }
            state = UQSTRING;
            break;

            

        default:
            switch(state) {
            case UQSTRING:
                if(dst + 1 >= endp)
                    return 0;
                *dst++ = '\'';
                break;
            case QSTRING:
                if(dst + 2 >= endp)
                    return 0;
                *dst++ = '"';           
                *dst++ = '\'';
                break;
            case SQSTRING:      
                break;
            default:
                break;
            }
            state = SQSTRING;   
            break;
        }

        if(dst + 1 >= endp)
            return 0;
        *dst++ = *src++;
    }

    switch(state) {
    case UQSTRING:
        break;
    case QSTRING:           
        if(dst + 1 >= endp)
            return 0;
        *dst++ = '"';
        break;
    case SQSTRING:          
        if(dst + 1 >= endp)
            return 0;
        *dst++ = '\'';
        break;
    default:
        break;
    }

    if(dst + 1 >= endp)
        return 0;
    *dst = '\0';

    
    

    return dst - buf;
}


static LIBSSH2_CHANNEL *
scp_recv(LIBSSH2_SESSION * session, const char *path, libssh2_struct_stat * sb)
{
    size_t cmd_len;
    int rc;
    int tmp_err_code;
    const char *tmp_err_msg;

    if(session->scpRecv_state == libssh2_NB_state_idle) {
        session->scpRecv_mode = 0;
        session->scpRecv_size = 0;
        session->scpRecv_mtime = 0;
        session->scpRecv_atime = 0;

        session->scpRecv_command_len =
            _libssh2_shell_quotedsize(path) + sizeof("scp -f ") + (sb ? 1 : 0);

        session->scpRecv_command =
            LIBSSH2_ALLOC(session, session->scpRecv_command_len);

        if(!session->scpRecv_command) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate a command buffer for "
                           "SCP session");
            return NULL;
        }

        snprintf((char *)session->scpRecv_command,
                 session->scpRecv_command_len,
                 "scp -%sf ", sb ? "p" : "");

        cmd_len = strlen((char *)session->scpRecv_command);

        if(!session->flag.quote_paths) {
            size_t path_len;

            path_len = strlen(path);

            
            memcpy(&session->scpRecv_command[cmd_len], path, path_len);
            cmd_len += path_len;
        }
        else {
            cmd_len += shell_quotearg(path,
                                      &session->scpRecv_command[cmd_len],
                                      session->scpRecv_command_len - cmd_len);
        }

        
        session->scpRecv_command_len = cmd_len;

        _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                       "Opening channel for SCP receive"));

        session->scpRecv_state = libssh2_NB_state_created;
    }

    if(session->scpRecv_state == libssh2_NB_state_created) {
        
        session->scpRecv_channel =
            _libssh2_channel_open(session, "session",
                                  sizeof("session") - 1,
                                  LIBSSH2_CHANNEL_WINDOW_DEFAULT,
                                  LIBSSH2_CHANNEL_PACKET_DEFAULT, NULL,
                                  0);
        if(!session->scpRecv_channel) {
            if(libssh2_session_last_errno(session) !=
                LIBSSH2_ERROR_EAGAIN) {
                LIBSSH2_FREE(session, session->scpRecv_command);
                session->scpRecv_command = NULL;
                session->scpRecv_state = libssh2_NB_state_idle;
            }
            else {
                _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                               "Would block starting up channel");
            }
            return NULL;
        }

        session->scpRecv_state = libssh2_NB_state_sent;
    }

    if(session->scpRecv_state == libssh2_NB_state_sent) {
        
        rc = _libssh2_channel_process_startup(session->scpRecv_channel, "exec",
                                              sizeof("exec") - 1,
                                              (char *)session->scpRecv_command,
                                              session->scpRecv_command_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block requesting SCP startup");
            return NULL;
        }
        else if(rc) {
            LIBSSH2_FREE(session, session->scpRecv_command);
            session->scpRecv_command = NULL;
            goto scp_recv_error;
        }
        LIBSSH2_FREE(session, session->scpRecv_command);
        session->scpRecv_command = NULL;

        _libssh2_debug((session, LIBSSH2_TRACE_SCP, "Sending initial wakeup"));
        
        session->scpRecv_response[0] = '\0';

        session->scpRecv_state = libssh2_NB_state_sent1;
    }

    if(session->scpRecv_state == libssh2_NB_state_sent1) {
        rc = (int)_libssh2_channel_write(session->scpRecv_channel, 0,
                                         session->scpRecv_response, 1);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block sending initial wakeup");
            return NULL;
        }
        else if(rc != 1) {
            goto scp_recv_error;
        }

        
        session->scpRecv_response_len = 0;

        session->scpRecv_state = libssh2_NB_state_sent2;
    }

    if((session->scpRecv_state == libssh2_NB_state_sent2)
        || (session->scpRecv_state == libssh2_NB_state_sent3)) {
        while(sb && (session->scpRecv_response_len <
                     LIBSSH2_SCP_RESPONSE_BUFLEN)) {
            unsigned char *s, *p;

            if(session->scpRecv_state == libssh2_NB_state_sent2) {
                rc = (int)_libssh2_channel_read(session->scpRecv_channel, 0,
                                                (char *) session->
                                                scpRecv_response +
                                                session->scpRecv_response_len,
                                                1);
                if(rc == LIBSSH2_ERROR_EAGAIN) {
                    _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                                   "Would block waiting for SCP response");
                    return NULL;
                }
                else if(rc < 0) {
                    
                    _libssh2_error(session, rc, "Failed reading SCP response");
                    goto scp_recv_error;
                }
                else if(rc == 0)
                    goto scp_recv_empty_channel;

                session->scpRecv_response_len++;

                if(session->scpRecv_response[0] != 'T') {
                    size_t err_len;
                    char *err_msg;

                    
                    err_len =
                        _libssh2_channel_packet_data_len(session->
                                                         scpRecv_channel, 0);
                    err_msg = LIBSSH2_ALLOC(session, err_len + 1);
                    if(!err_msg) {
                        _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                       "Failed to get memory ");
                        goto scp_recv_error;
                    }

                    
                    (void)_libssh2_channel_read(session->scpRecv_channel, 0,
                                                err_msg, err_len);
                    

                    
                    err_msg[err_len] = 0;

                    _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                                   "got %02x %s", session->scpRecv_response[0],
                                   err_msg));

                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Failed to recv file");

                    LIBSSH2_FREE(session, err_msg);
                    goto scp_recv_error;
                }

                if((session->scpRecv_response_len > 1) &&
                    ((session->
                      scpRecv_response[session->scpRecv_response_len - 1] <
                      '0')
                     || (session->
                         scpRecv_response[session->scpRecv_response_len - 1] >
                         '9'))
                    && (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        ' ')
                    && (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        '\r')
                    && (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        '\n')) {
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid data in SCP response");
                    goto scp_recv_error;
                }

                if((session->scpRecv_response_len < 9)
                    || (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        '\n')) {
                    if(session->scpRecv_response_len ==
                        LIBSSH2_SCP_RESPONSE_BUFLEN) {
                        
                        _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                       "Unterminated response from "
                                       "SCP server");
                        goto scp_recv_error;
                    }
                    
                    continue;
                }

                
                while((session->
                        scpRecv_response[session->scpRecv_response_len - 1] ==
                        '\r')
                       || (session->
                           scpRecv_response[session->scpRecv_response_len -
                                            1] == '\n'))
                    session->scpRecv_response_len--;
                session->scpRecv_response[session->scpRecv_response_len] =
                    '\0';

                if(session->scpRecv_response_len < 8) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "too short");
                    goto scp_recv_error;
                }

                s = session->scpRecv_response + 1;

                p = (unsigned char *) strchr((char *) s, ' ');
                if(!p || ((p - s) <= 0)) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "malformed mtime");
                    goto scp_recv_error;
                }

                *(p++) = '\0';
                
                session->scpRecv_mtime = strtol((char *) s, NULL, 10);

                s = (unsigned char *) strchr((char *) p, ' ');
                if(!s || ((s - p) <= 0)) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "malformed mtime.usec");
                    goto scp_recv_error;
                }

                
                s++;
                p = (unsigned char *) strchr((char *) s, ' ');
                if(!p || ((p - s) <= 0)) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "too short or malformed");
                    goto scp_recv_error;
                }

                *p = '\0';
                
                session->scpRecv_atime = strtol((char *) s, NULL, 10);

                
                session->scpRecv_response[0] = '\0';

                session->scpRecv_state = libssh2_NB_state_sent3;
            }

            if(session->scpRecv_state == libssh2_NB_state_sent3) {
                rc = (int)_libssh2_channel_write(session->scpRecv_channel, 0,
                                                 session->scpRecv_response, 1);
                if(rc == LIBSSH2_ERROR_EAGAIN) {
                    _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                                   "Would block waiting to send SCP ACK");
                    return NULL;
                }
                else if(rc != 1) {
                    goto scp_recv_error;
                }

                _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                               "mtime = %ld, atime = %ld",
                               session->scpRecv_mtime,
                               session->scpRecv_atime));

                
                break;
            }
        }

        session->scpRecv_state = libssh2_NB_state_sent4;
    }

    if(session->scpRecv_state == libssh2_NB_state_sent4) {
        session->scpRecv_response_len = 0;

        session->scpRecv_state = libssh2_NB_state_sent5;
    }

    if((session->scpRecv_state == libssh2_NB_state_sent5)
        || (session->scpRecv_state == libssh2_NB_state_sent6)) {
        while(session->scpRecv_response_len < LIBSSH2_SCP_RESPONSE_BUFLEN) {
            char *s, *p, *e = NULL;

            if(session->scpRecv_state == libssh2_NB_state_sent5) {
                rc = (int)_libssh2_channel_read(session->scpRecv_channel, 0,
                                                (char *) session->
                                                scpRecv_response +
                                                session->scpRecv_response_len,
                                                1);
                if(rc == LIBSSH2_ERROR_EAGAIN) {
                    _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                                   "Would block waiting for SCP response");
                    return NULL;
                }
                else if(rc < 0) {
                    
                    _libssh2_error(session, rc, "Failed reading SCP response");
                    goto scp_recv_error;
                }
                else if(rc == 0)
                    goto scp_recv_empty_channel;

                session->scpRecv_response_len++;

                if(session->scpRecv_response[0] != 'C') {
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server");
                    goto scp_recv_error;
                }

                if((session->scpRecv_response_len > 1) &&
                    (session->
                     scpRecv_response[session->scpRecv_response_len - 1] !=
                     '\r')
                    && (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        '\n')
                    &&
                    (session->
                     scpRecv_response[session->scpRecv_response_len - 1]
                     < 32)) {
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid data in SCP response");
                    goto scp_recv_error;
                }

                if((session->scpRecv_response_len < 7)
                    || (session->
                        scpRecv_response[session->scpRecv_response_len - 1] !=
                        '\n')) {
                    if(session->scpRecv_response_len ==
                        LIBSSH2_SCP_RESPONSE_BUFLEN) {
                        
                        _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                       "Unterminated response "
                                       "from SCP server");
                        goto scp_recv_error;
                    }
                    
                    continue;
                }

                
                while((session->
                        scpRecv_response[session->scpRecv_response_len - 1] ==
                        '\r')
                       || (session->
                           scpRecv_response[session->scpRecv_response_len -
                                            1] == '\n')) {
                    session->scpRecv_response_len--;
                }
                session->scpRecv_response[session->scpRecv_response_len] =
                    '\0';

                if(session->scpRecv_response_len < 6) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "too short");
                    goto scp_recv_error;
                }

                s = (char *) session->scpRecv_response + 1;

                p = strchr(s, ' ');
                if(!p || ((p - s) <= 0)) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "malformed mode");
                    goto scp_recv_error;
                }

                *(p++) = '\0';
                
                session->scpRecv_mode = strtol(s, &e, 8);
                if(e && *e) {
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "invalid mode");
                    goto scp_recv_error;
                }

                s = strchr(p, ' ');
                if(!s || ((s - p) <= 0)) {
                    
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "too short or malformed");
                    goto scp_recv_error;
                }

                *s = '\0';
                
                session->scpRecv_size = scpsize_strtol(p, &e, 10);
                if(e && *e) {
                    _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                                   "Invalid response from SCP server, "
                                   "invalid size");
                    goto scp_recv_error;
                }

                
                session->scpRecv_response[0] = '\0';

                session->scpRecv_state = libssh2_NB_state_sent6;
            }

            if(session->scpRecv_state == libssh2_NB_state_sent6) {
                rc = (int)_libssh2_channel_write(session->scpRecv_channel, 0,
                                                 session->scpRecv_response, 1);
                if(rc == LIBSSH2_ERROR_EAGAIN) {
                    _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                                   "Would block sending SCP ACK");
                    return NULL;
                }
                else if(rc != 1) {
                    goto scp_recv_error;
                }
                _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                               "mode = 0%lo size = %ld", session->scpRecv_mode,
                               (long)session->scpRecv_size));

                
                break;
            }
        }

        session->scpRecv_state = libssh2_NB_state_sent7;
    }

    if(sb) {
        memset(sb, 0, sizeof(libssh2_struct_stat));

        sb->st_mtime = session->scpRecv_mtime;
        sb->st_atime = session->scpRecv_atime;
        sb->st_size = (libssh2_struct_stat_size)session->scpRecv_size;
        sb->st_mode = (unsigned short)session->scpRecv_mode;
    }

    session->scpRecv_state = libssh2_NB_state_idle;
    return session->scpRecv_channel;

scp_recv_empty_channel:
    
    if(libssh2_channel_eof(session->scpRecv_channel))
        _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                       "Unexpected channel close");
    else
        return session->scpRecv_channel;
    
scp_recv_error:
    tmp_err_code = session->err_code;
    tmp_err_msg = session->err_msg;
    while(libssh2_channel_free(session->scpRecv_channel) ==
          LIBSSH2_ERROR_EAGAIN);
    session->err_code = tmp_err_code;
    session->err_msg = tmp_err_msg;
    session->scpRecv_channel = NULL;
    session->scpRecv_state = libssh2_NB_state_idle;
    return NULL;
}

#ifndef LIBSSH2_NO_DEPRECATED

LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_scp_recv(LIBSSH2_SESSION *session, const char *path, struct stat *sb)
{
    LIBSSH2_CHANNEL *ptr;

    
    libssh2_struct_stat sb_intl;
    libssh2_struct_stat *sb_ptr;
    memset(&sb_intl, 0, sizeof(sb_intl));
    sb_ptr = sb ? &sb_intl : NULL;

    BLOCK_ADJUST_ERRNO(ptr, session, scp_recv(session, path, sb_ptr));

    
    if(sb) {
        memset(sb, 0, sizeof(struct stat));

        sb->st_mtime = sb_intl.st_mtime;
        sb->st_atime = sb_intl.st_atime;
        sb->st_size = (off_t)sb_intl.st_size;
        sb->st_mode = sb_intl.st_mode;
    }

    return ptr;
}
#endif


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_scp_recv2(LIBSSH2_SESSION *session, const char *path,
                  libssh2_struct_stat *sb)
{
    LIBSSH2_CHANNEL *ptr;
    BLOCK_ADJUST_ERRNO(ptr, session, scp_recv(session, path, sb));
    return ptr;
}


static LIBSSH2_CHANNEL *
scp_send(LIBSSH2_SESSION * session, const char *path, int mode,
         libssh2_int64_t size, time_t mtime, time_t atime)
{
    size_t cmd_len;
    int rc;
    int tmp_err_code;
    const char *tmp_err_msg;

    if(session->scpSend_state == libssh2_NB_state_idle) {
        session->scpSend_command_len =
            _libssh2_shell_quotedsize(path) + sizeof("scp -t ") +
            ((mtime || atime) ? 1 : 0);

        session->scpSend_command =
            LIBSSH2_ALLOC(session, session->scpSend_command_len);

        if(!session->scpSend_command) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate a command buffer for "
                           "SCP session");
            return NULL;
        }

        snprintf((char *)session->scpSend_command,
                 session->scpSend_command_len,
                 "scp -%st ", (mtime || atime) ? "p" : "");

        cmd_len = strlen((char *)session->scpSend_command);

        if(!session->flag.quote_paths) {
            size_t path_len;

            path_len = strlen(path);

            
            memcpy(&session->scpSend_command[cmd_len], path, path_len);
            cmd_len += path_len;

        }
        else {
            cmd_len += shell_quotearg(path,
                                      &session->scpSend_command[cmd_len],
                                      session->scpSend_command_len - cmd_len);
        }

        
        session->scpSend_command_len = cmd_len;

        _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                       "Opening channel for SCP send"));
        

        session->scpSend_state = libssh2_NB_state_created;
    }

    if(session->scpSend_state == libssh2_NB_state_created) {
        session->scpSend_channel =
            _libssh2_channel_open(session, "session", sizeof("session") - 1,
                                  LIBSSH2_CHANNEL_WINDOW_DEFAULT,
                                  LIBSSH2_CHANNEL_PACKET_DEFAULT, NULL, 0);
        if(!session->scpSend_channel) {
            if(libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN) {
                
                LIBSSH2_FREE(session, session->scpSend_command);
                session->scpSend_command = NULL;
                session->scpSend_state = libssh2_NB_state_idle;
            }
            else {
                _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                               "Would block starting up channel");
            }
            return NULL;
        }

        session->scpSend_state = libssh2_NB_state_sent;
    }

    if(session->scpSend_state == libssh2_NB_state_sent) {
        
        rc = _libssh2_channel_process_startup(session->scpSend_channel, "exec",
                                              sizeof("exec") - 1,
                                              (char *)session->scpSend_command,
                                              session->scpSend_command_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block requesting SCP startup");
            return NULL;
        }
        else if(rc) {
            
            LIBSSH2_FREE(session, session->scpSend_command);
            session->scpSend_command = NULL;
            _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                           "Unknown error while getting error string");
            goto scp_send_error;
        }
        LIBSSH2_FREE(session, session->scpSend_command);
        session->scpSend_command = NULL;

        session->scpSend_state = libssh2_NB_state_sent1;
    }

    if(session->scpSend_state == libssh2_NB_state_sent1) {
        
        rc = (int)_libssh2_channel_read(session->scpSend_channel, 0,
                                        (char *) session->scpSend_response, 1);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block waiting for response from remote");
            return NULL;
        }
        else if(rc < 0) {
            _libssh2_error(session, rc, "SCP failure");
            goto scp_send_error;
        }
        else if(!rc)
            
            goto scp_send_empty_channel;
        else if(session->scpSend_response[0]) {
            _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                           "Invalid ACK response from remote");
            goto scp_send_error;
        }
        if(mtime || atime) {
            
            session->scpSend_response_len =
                snprintf((char *) session->scpSend_response,
                         LIBSSH2_SCP_RESPONSE_BUFLEN, "T%ld 0 %ld 0\n",
                         (long)mtime, (long)atime);
            _libssh2_debug((session, LIBSSH2_TRACE_SCP, "Sent %s",
                           session->scpSend_response));
        }

        session->scpSend_state = libssh2_NB_state_sent2;
    }

    
    if(mtime || atime) {
        if(session->scpSend_state == libssh2_NB_state_sent2) {
            rc = (int)_libssh2_channel_write(session->scpSend_channel, 0,
                                             session->scpSend_response,
                                             session->scpSend_response_len);
            if(rc == LIBSSH2_ERROR_EAGAIN) {
                _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                               "Would block sending time data for SCP file");
                return NULL;
            }
            else if(rc != (int)session->scpSend_response_len) {
                _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                               "Unable to send time data for SCP file");
                goto scp_send_error;
            }

            session->scpSend_state = libssh2_NB_state_sent3;
        }

        if(session->scpSend_state == libssh2_NB_state_sent3) {
            
            rc = (int)_libssh2_channel_read(session->scpSend_channel, 0,
                                            (char *) session->scpSend_response,
                                            1);
            if(rc == LIBSSH2_ERROR_EAGAIN) {
                _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                               "Would block waiting for response");
                return NULL;
            }
            else if(rc < 0) {
                _libssh2_error(session, rc, "SCP failure");
                goto scp_send_error;
            }
            else if(!rc)
                
                goto scp_send_empty_channel;
            else if(session->scpSend_response[0]) {
                _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                               "Invalid SCP ACK response");
                goto scp_send_error;
            }

            session->scpSend_state = libssh2_NB_state_sent4;
        }
    }
    else {
        if(session->scpSend_state == libssh2_NB_state_sent2) {
            session->scpSend_state = libssh2_NB_state_sent4;
        }
    }

    if(session->scpSend_state == libssh2_NB_state_sent4) {
        
        const char *base = strrchr(path, '/');
        if(base)
            base++;
        else
            base = path;

        session->scpSend_response_len =
            snprintf((char *) session->scpSend_response,
                     LIBSSH2_SCP_RESPONSE_BUFLEN, "C0%o %"
                     LIBSSH2_INT64_T_FORMAT " %s\n", mode,
                     size, base);
        _libssh2_debug((session, LIBSSH2_TRACE_SCP, "Sent %s",
                       session->scpSend_response));

        session->scpSend_state = libssh2_NB_state_sent5;
    }

    if(session->scpSend_state == libssh2_NB_state_sent5) {
        rc = (int)_libssh2_channel_write(session->scpSend_channel, 0,
                                         session->scpSend_response,
                                         session->scpSend_response_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block send core file data for SCP file");
            return NULL;
        }
        else if(rc != (int)session->scpSend_response_len) {
            _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                           "Unable to send core file data for SCP file");
            goto scp_send_error;
        }

        session->scpSend_state = libssh2_NB_state_sent6;
    }

    if(session->scpSend_state == libssh2_NB_state_sent6) {
        
        rc = (int)_libssh2_channel_read(session->scpSend_channel, 0,
                                        (char *) session->scpSend_response,
                                        1);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block waiting for response");
            return NULL;
        }
        else if(rc < 0) {
            _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                           "Invalid ACK response from remote");
            goto scp_send_error;
        }
        else if(rc == 0)
            goto scp_send_empty_channel;

        else if(session->scpSend_response[0]) {
            size_t err_len;
            char *err_msg;

            err_len =
                _libssh2_channel_packet_data_len(session->scpSend_channel, 0);
            err_msg = LIBSSH2_ALLOC(session, err_len + 1);
            if(!err_msg) {
                _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                               "failed to get memory");
                goto scp_send_error;
            }

            
            rc = (int)_libssh2_channel_read(session->scpSend_channel, 0,
                                            err_msg, err_len);
            if(rc > 0) {
                err_msg[err_len] = 0;
                _libssh2_debug((session, LIBSSH2_TRACE_SCP,
                               "got %02x %s", session->scpSend_response[0],
                               err_msg));
            }
            LIBSSH2_FREE(session, err_msg);
            _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                           "failed to send file");
            goto scp_send_error;
        }
    }

    session->scpSend_state = libssh2_NB_state_idle;
    return session->scpSend_channel;

scp_send_empty_channel:
    
    if(libssh2_channel_eof(session->scpSend_channel)) {
        _libssh2_error(session, LIBSSH2_ERROR_SCP_PROTOCOL,
                       "Unexpected channel close");
    }
    else
        return session->scpSend_channel;
    
scp_send_error:
    tmp_err_code = session->err_code;
    tmp_err_msg = session->err_msg;
    while(libssh2_channel_free(session->scpSend_channel) ==
          LIBSSH2_ERROR_EAGAIN);
    session->err_code = tmp_err_code;
    session->err_msg = tmp_err_msg;
    session->scpSend_channel = NULL;
    session->scpSend_state = libssh2_NB_state_idle;
    return NULL;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_scp_send_ex(LIBSSH2_SESSION *session, const char *path, int mode,
                    size_t size, long mtime, long atime)
{
    LIBSSH2_CHANNEL *ptr;
    BLOCK_ADJUST_ERRNO(ptr, session,
                       scp_send(session, path, mode, size,
                                (time_t)mtime, (time_t)atime));
    return ptr;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_scp_send64(LIBSSH2_SESSION *session, const char *path, int mode,
                   libssh2_int64_t size, time_t mtime, time_t atime)
{
    LIBSSH2_CHANNEL *ptr;
    BLOCK_ADJUST_ERRNO(ptr, session,
                       scp_send(session, path, mode, size, mtime, atime));
    return ptr;
}
