

#include "libssh2_priv.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <assert.h>

#include "channel.h"
#include "transport.h"
#include "packet.h"
#include "session.h"


uint32_t
_libssh2_channel_nextid(LIBSSH2_SESSION * session)
{
    uint32_t id = session->next_channel;
    LIBSSH2_CHANNEL *channel;

    channel = _libssh2_list_first(&session->channels);

    while(channel) {
        if(channel->local.id > id) {
            id = channel->local.id;
        }
        channel = _libssh2_list_next(&channel->node);
    }

    
    session->next_channel = id + 1;
    _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                   "Allocated new channel ID#%u", id));
    return id;
}


LIBSSH2_CHANNEL *
_libssh2_channel_locate(LIBSSH2_SESSION *session, uint32_t channel_id)
{
    LIBSSH2_CHANNEL *channel;
    LIBSSH2_LISTENER *l;

    for(channel = _libssh2_list_first(&session->channels);
        channel;
        channel = _libssh2_list_next(&channel->node)) {
        if(channel->local.id == channel_id)
            return channel;
    }

    
    for(l = _libssh2_list_first(&session->listeners); l;
        l = _libssh2_list_next(&l->node)) {
        for(channel = _libssh2_list_first(&l->queue);
            channel;
            channel = _libssh2_list_next(&channel->node)) {
            if(channel->local.id == channel_id)
                return channel;
        }
    }

    return NULL;
}


LIBSSH2_CHANNEL *
_libssh2_channel_open(LIBSSH2_SESSION * session, const char *channel_type,
                      uint32_t channel_type_len,
                      uint32_t window_size,
                      uint32_t packet_size,
                      const unsigned char *message,
                      size_t message_len)
{
    static const unsigned char reply_codes[3] = {
        SSH_MSG_CHANNEL_OPEN_CONFIRMATION,
        SSH_MSG_CHANNEL_OPEN_FAILURE,
        0
    };
    unsigned char *s;
    int rc;

    if(session->open_state == libssh2_NB_state_idle) {
        session->open_channel = NULL;
        session->open_packet = NULL;
        session->open_data = NULL;
        
        session->open_packet_len = channel_type_len + 17;
        session->open_local_channel = _libssh2_channel_nextid(session);

        
        memset(&session->open_packet_requirev_state, 0,
               sizeof(session->open_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Opening Channel - win %d pack %d", window_size,
                       packet_size));
        session->open_channel =
            LIBSSH2_CALLOC(session, sizeof(LIBSSH2_CHANNEL));
        if(!session->open_channel) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate space for channel data");
            return NULL;
        }
        session->open_channel->channel_type_len = channel_type_len;
        session->open_channel->channel_type =
            LIBSSH2_ALLOC(session, channel_type_len);
        if(!session->open_channel->channel_type) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Failed allocating memory for channel type name");
            LIBSSH2_FREE(session, session->open_channel);
            session->open_channel = NULL;
            return NULL;
        }
        memcpy(session->open_channel->channel_type, channel_type,
               channel_type_len);

        
        session->open_channel->local.id = session->open_local_channel;
        session->open_channel->remote.window_size = window_size;
        session->open_channel->remote.window_size_initial = window_size;
        session->open_channel->remote.packet_size = packet_size;
        session->open_channel->session = session;

        _libssh2_list_add(&session->channels,
                          &session->open_channel->node);

        s = session->open_packet =
            LIBSSH2_ALLOC(session, session->open_packet_len);
        if(!session->open_packet) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate temporary space for packet");
            goto channel_error;
        }
        *(s++) = SSH_MSG_CHANNEL_OPEN;
        _libssh2_store_str(&s, channel_type, channel_type_len);
        _libssh2_store_u32(&s, session->open_local_channel);
        _libssh2_store_u32(&s, window_size);
        _libssh2_store_u32(&s, packet_size);

        

        session->open_state = libssh2_NB_state_created;
    }

    if(session->open_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session,
                                     session->open_packet,
                                     session->open_packet_len,
                                     message, message_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending channel-open request");
            return NULL;
        }
        else if(rc) {
            _libssh2_error(session, rc,
                           "Unable to send channel-open request");
            goto channel_error;
        }

        session->open_state = libssh2_NB_state_sent;
    }

    if(session->open_state == libssh2_NB_state_sent) {
        rc = _libssh2_packet_requirev(session, reply_codes,
                                      &session->open_data,
                                      &session->open_data_len, 1,
                                      session->open_packet + 5 +
                                      channel_type_len, 4,
                                      &session->open_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN, "Would block");
            return NULL;
        }
        else if(rc) {
            _libssh2_error(session, rc, "Unexpected error");
            goto channel_error;
        }

        if(session->open_data_len < 1) {
            _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                           "Unexpected packet size");
            goto channel_error;
        }

        if(session->open_data[0] == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {

            if(session->open_data_len < 17) {
                _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                               "Unexpected packet size");
                goto channel_error;
            }

            session->open_channel->remote.id =
                _libssh2_ntohu32(session->open_data + 5);
            session->open_channel->local.window_size =
                _libssh2_ntohu32(session->open_data + 9);
            session->open_channel->local.window_size_initial =
                _libssh2_ntohu32(session->open_data + 9);
            session->open_channel->local.packet_size =
                _libssh2_ntohu32(session->open_data + 13);
            _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                           "Connection Established - ID: %u/%u win: %u/%u"
                           " pack: %u/%u",
                           session->open_channel->local.id,
                           session->open_channel->remote.id,
                           session->open_channel->local.window_size,
                           session->open_channel->remote.window_size,
                           session->open_channel->local.packet_size,
                           session->open_channel->remote.packet_size));
            LIBSSH2_FREE(session, session->open_packet);
            session->open_packet = NULL;
            LIBSSH2_FREE(session, session->open_data);
            session->open_data = NULL;

            session->open_state = libssh2_NB_state_idle;
            return session->open_channel;
        }

        if(session->open_data[0] == SSH_MSG_CHANNEL_OPEN_FAILURE) {
            unsigned int reason_code =
                _libssh2_ntohu32(session->open_data + 5);
            switch(reason_code) {
            case SSH_OPEN_ADMINISTRATIVELY_PROHIBITED:
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Channel open failure "
                               "(administratively prohibited)");
                break;
            case SSH_OPEN_CONNECT_FAILED:
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Channel open failure (connect failed)");
                break;
            case SSH_OPEN_UNKNOWN_CHANNELTYPE:
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Channel open failure (unknown channel type)");
                break;
            case SSH_OPEN_RESOURCE_SHORTAGE:
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Channel open failure (resource shortage)");
                break;
            default:
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Channel open failure");
            }
        }
    }

channel_error:

    if(session->open_data) {
        LIBSSH2_FREE(session, session->open_data);
        session->open_data = NULL;
    }
    if(session->open_packet) {
        LIBSSH2_FREE(session, session->open_packet);
        session->open_packet = NULL;
    }
    if(session->open_channel) {
        unsigned char channel_id[4];
        LIBSSH2_FREE(session, session->open_channel->channel_type);

        _libssh2_list_remove(&session->open_channel->node);

        
        _libssh2_htonu32(channel_id, session->open_channel->local.id);
        while((_libssh2_packet_ask(session, SSH_MSG_CHANNEL_DATA,
                                   &session->open_data,
                                   &session->open_data_len, 1,
                                   channel_id, 4) >= 0)
              ||
              (_libssh2_packet_ask(session, SSH_MSG_CHANNEL_EXTENDED_DATA,
                                   &session->open_data,
                                   &session->open_data_len, 1,
                                   channel_id, 4) >= 0)) {
            LIBSSH2_FREE(session, session->open_data);
            session->open_data = NULL;
        }

        LIBSSH2_FREE(session, session->open_channel);
        session->open_channel = NULL;
    }

    session->open_state = libssh2_NB_state_idle;
    return NULL;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_open_ex(LIBSSH2_SESSION *session, const char *type,
                        unsigned int type_len,
                        unsigned int window_size, unsigned int packet_size,
                        const char *msg, unsigned int msg_len)
{
    LIBSSH2_CHANNEL *ptr;

    if(!session)
        return NULL;

    BLOCK_ADJUST_ERRNO(ptr, session,
                       _libssh2_channel_open(session, type, type_len,
                                             window_size, packet_size,
                                             (unsigned char *)msg,
                                             msg_len));
    return ptr;
}


static LIBSSH2_CHANNEL *
channel_direct_tcpip(LIBSSH2_SESSION * session, const char *host,
                     int port, const char *shost, int sport)
{
    LIBSSH2_CHANNEL *channel;
    unsigned char *s;

    if(session->direct_state == libssh2_NB_state_idle) {
        session->direct_host_len = strlen(host);
        session->direct_shost_len = strlen(shost);
        
        session->direct_message_len =
            session->direct_host_len + session->direct_shost_len + 16;

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Requesting direct-tcpip session from %s:%d to %s:%d",
                       shost, sport, host, port));

        s = session->direct_message =
            LIBSSH2_ALLOC(session, session->direct_message_len);
        if(!session->direct_message) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for "
                           "direct-tcpip connection");
            return NULL;
        }
        _libssh2_store_str(&s, host, session->direct_host_len);
        _libssh2_store_u32(&s, port);
        _libssh2_store_str(&s, shost, session->direct_shost_len);
        _libssh2_store_u32(&s, sport);
    }

    channel =
        _libssh2_channel_open(session, "direct-tcpip",
                              sizeof("direct-tcpip") - 1,
                              LIBSSH2_CHANNEL_WINDOW_DEFAULT,
                              LIBSSH2_CHANNEL_PACKET_DEFAULT,
                              session->direct_message,
                              session->direct_message_len);

    if(!channel &&
        libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
        
        session->direct_state = libssh2_NB_state_created;
        return NULL;
    }
    
    session->direct_state = libssh2_NB_state_idle;

    LIBSSH2_FREE(session, session->direct_message);
    session->direct_message = NULL;

    return channel;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_direct_tcpip_ex(LIBSSH2_SESSION *session, const char *host,
                                int port, const char *shost, int sport)
{
    LIBSSH2_CHANNEL *ptr;

    if(!session)
        return NULL;

    BLOCK_ADJUST_ERRNO(ptr, session,
                       channel_direct_tcpip(session, host, port,
                                            shost, sport));
    return ptr;
}


static LIBSSH2_CHANNEL *
channel_direct_streamlocal(LIBSSH2_SESSION * session, const char *socket_path,
                           const char *shost, int sport)
{
    LIBSSH2_CHANNEL *channel;
    unsigned char *s;

    if(session->direct_state == libssh2_NB_state_idle) {
        session->direct_host_len = strlen(socket_path);
        session->direct_shost_len = strlen(shost);
        session->direct_message_len =
            session->direct_host_len + session->direct_shost_len + 12;

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Requesting direct-streamlocal session to %s",
                       socket_path));

        s = session->direct_message =
            LIBSSH2_ALLOC(session, session->direct_message_len);
        if(!session->direct_message) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                "Unable to allocate memory for direct-streamlocal connection");
            return NULL;
        }
        _libssh2_store_str(&s, socket_path, session->direct_host_len);
        _libssh2_store_str(&s, shost, session->direct_shost_len);
        _libssh2_store_u32(&s, sport);
    }

    channel =
        _libssh2_channel_open(session, "direct-streamlocal@openssh.com",
                              sizeof("direct-streamlocal@openssh.com") - 1,
                              LIBSSH2_CHANNEL_WINDOW_DEFAULT,
                              LIBSSH2_CHANNEL_PACKET_DEFAULT,
                              session->direct_message,
                              session->direct_message_len);

    if(!channel &&
        libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
        
        session->direct_state = libssh2_NB_state_created;
        return NULL;
    }
    
    session->direct_state = libssh2_NB_state_idle;

    LIBSSH2_FREE(session, session->direct_message);
    session->direct_message = NULL;

    return channel;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_direct_streamlocal_ex(LIBSSH2_SESSION * session,
                                      const char *socket_path,
                                      const char *shost, int sport)
{
    LIBSSH2_CHANNEL *ptr;

    if(!session)
        return NULL;

    BLOCK_ADJUST_ERRNO(ptr, session,
                       channel_direct_streamlocal(session,
                       socket_path, shost, sport));
    return ptr;
}


static LIBSSH2_LISTENER *
channel_forward_listen(LIBSSH2_SESSION * session, const char *host,
                       int port, int *bound_port, int queue_maxsize)
{
    unsigned char *s;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_REQUEST_SUCCESS, SSH_MSG_REQUEST_FAILURE, 0 };
    int rc;

    if(!host)
        host = "0.0.0.0";

    if(session->fwdLstn_state == libssh2_NB_state_idle) {
        session->fwdLstn_host_len = (uint32_t)strlen(host);
        
        session->fwdLstn_packet_len =
            session->fwdLstn_host_len +
            (uint32_t)(sizeof("tcpip-forward") - 1) + 14;

        
        memset(&session->fwdLstn_packet_requirev_state, 0,
               sizeof(session->fwdLstn_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Requesting tcpip-forward session for %s:%d", host,
                       port));

        s = session->fwdLstn_packet =
            LIBSSH2_ALLOC(session, session->fwdLstn_packet_len);
        if(!session->fwdLstn_packet) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for setenv packet");
            return NULL;
        }

        *(s++) = SSH_MSG_GLOBAL_REQUEST;
        _libssh2_store_str(&s, "tcpip-forward", sizeof("tcpip-forward") - 1);
        *(s++) = 0x01;          

        _libssh2_store_str(&s, host, session->fwdLstn_host_len);
        _libssh2_store_u32(&s, port);

        session->fwdLstn_state = libssh2_NB_state_created;
    }

    if(session->fwdLstn_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session,
                                     session->fwdLstn_packet,
                                     session->fwdLstn_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block sending global-request packet for "
                           "forward listen request");
            return NULL;
        }
        else if(rc) {
            _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                           "Unable to send global-request packet for forward "
                           "listen request");
            LIBSSH2_FREE(session, session->fwdLstn_packet);
            session->fwdLstn_packet = NULL;
            session->fwdLstn_state = libssh2_NB_state_idle;
            return NULL;
        }
        LIBSSH2_FREE(session, session->fwdLstn_packet);
        session->fwdLstn_packet = NULL;

        session->fwdLstn_state = libssh2_NB_state_sent;
    }

    if(session->fwdLstn_state == libssh2_NB_state_sent) {
        unsigned char *data;
        size_t data_len;
        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      0, NULL, 0,
                                      &session->fwdLstn_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN, "Would block");
            return NULL;
        }
        else if(rc || (data_len < 1)) {
            _libssh2_error(session, LIBSSH2_ERROR_PROTO, "Unknown");
            session->fwdLstn_state = libssh2_NB_state_idle;
            return NULL;
        }

        if(data[0] == SSH_MSG_REQUEST_SUCCESS) {
            LIBSSH2_LISTENER *listener;

            listener = LIBSSH2_CALLOC(session, sizeof(LIBSSH2_LISTENER));
            if(!listener)
                _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                               "Unable to allocate memory for listener queue");
            else {
                listener->host =
                    LIBSSH2_ALLOC(session, session->fwdLstn_host_len + 1);
                if(!listener->host) {
                    _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                   "Unable to allocate memory "
                                   "for listener queue");
                    LIBSSH2_FREE(session, listener);
                    listener = NULL;
                }
                else {
                    listener->session = session;
                    memcpy(listener->host, host, session->fwdLstn_host_len);
                    listener->host[session->fwdLstn_host_len] = 0;
                    if(data_len >= 5 && !port) {
                        listener->port = _libssh2_ntohu32(data + 1);
                        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                                       "Dynamic tcpip-forward port "
                                       "allocated: %d",
                                       listener->port));
                    }
                    else
                        listener->port = port;

                    listener->queue_size = 0;
                    listener->queue_maxsize = queue_maxsize;

                    
                    _libssh2_list_add(&session->listeners, &listener->node);

                    if(bound_port) {
                        *bound_port = listener->port;
                    }
                }
            }

            LIBSSH2_FREE(session, data);
            session->fwdLstn_state = libssh2_NB_state_idle;
            return listener;
        }
        else if(data[0] == SSH_MSG_REQUEST_FAILURE) {
            LIBSSH2_FREE(session, data);
            _libssh2_error(session, LIBSSH2_ERROR_REQUEST_DENIED,
                           "Unable to complete request for forward-listen");
            session->fwdLstn_state = libssh2_NB_state_idle;
            return NULL;
        }
    }

    session->fwdLstn_state = libssh2_NB_state_idle;

    return NULL;
}


LIBSSH2_API LIBSSH2_LISTENER *
libssh2_channel_forward_listen_ex(LIBSSH2_SESSION *session, const char *host,
                                  int port, int *bound_port, int queue_maxsize)
{
    LIBSSH2_LISTENER *ptr;

    if(!session)
        return NULL;

    BLOCK_ADJUST_ERRNO(ptr, session,
                       channel_forward_listen(session, host, port, bound_port,
                                              queue_maxsize));
    return ptr;
}


int _libssh2_channel_forward_cancel(LIBSSH2_LISTENER *listener)
{
    LIBSSH2_SESSION *session = listener->session;
    LIBSSH2_CHANNEL *queued;
    unsigned char *packet, *s;
    size_t host_len = strlen(listener->host);
    
    size_t packet_len =
        host_len + 14 + sizeof("cancel-tcpip-forward") - 1;
    int rc;
    int retcode = 0;

    if(listener->chanFwdCncl_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Cancelling tcpip-forward session for %s:%d",
                       listener->host, listener->port));

        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for setenv packet");
            return LIBSSH2_ERROR_ALLOC;
        }

        *(s++) = SSH_MSG_GLOBAL_REQUEST;
        _libssh2_store_str(&s, "cancel-tcpip-forward",
                           sizeof("cancel-tcpip-forward") - 1);
        *(s++) = 0x00;          

        _libssh2_store_str(&s, listener->host, host_len);
        _libssh2_store_u32(&s, listener->port);

        listener->chanFwdCncl_state = libssh2_NB_state_created;
    }
    else {
        packet = listener->chanFwdCncl_data;
    }

    if(listener->chanFwdCncl_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, packet, packet_len, NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending forward request");
            listener->chanFwdCncl_data = packet;
            return rc;
        }
        else if(rc) {
            _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                           "Unable to send global-request packet for forward "
                           "listen request");
            
            listener->chanFwdCncl_state = libssh2_NB_state_sent;
            retcode = LIBSSH2_ERROR_SOCKET_SEND;
        }
        LIBSSH2_FREE(session, packet);

        listener->chanFwdCncl_state = libssh2_NB_state_sent;
    }

    queued = _libssh2_list_first(&listener->queue);
    while(queued) {
        LIBSSH2_CHANNEL *next = _libssh2_list_next(&queued->node);

        rc = _libssh2_channel_free(queued);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        queued = next;
    }
    LIBSSH2_FREE(session, listener->host);

    
    _libssh2_list_remove(&listener->node);

    LIBSSH2_FREE(session, listener);

    return retcode;
}


LIBSSH2_API int
libssh2_channel_forward_cancel(LIBSSH2_LISTENER *listener)
{
    int rc;

    if(!listener)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, listener->session,
                 _libssh2_channel_forward_cancel(listener));
    return rc;
}


static LIBSSH2_CHANNEL *
channel_forward_accept(LIBSSH2_LISTENER *listener)
{
    int rc;

    do {
        rc = _libssh2_transport_read(listener->session);
    } while(rc > 0);

    if(_libssh2_list_first(&listener->queue)) {
        LIBSSH2_CHANNEL *channel = _libssh2_list_first(&listener->queue);

        
        _libssh2_list_remove(&channel->node);

        listener->queue_size--;

        
        _libssh2_list_add(&channel->session->channels, &channel->node);

        return channel;
    }

    if(rc == LIBSSH2_ERROR_EAGAIN) {
        _libssh2_error(listener->session, LIBSSH2_ERROR_EAGAIN,
                       "Would block waiting for packet");
    }
    else
        _libssh2_error(listener->session, LIBSSH2_ERROR_CHANNEL_UNKNOWN,
                       "Channel not found");
    return NULL;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_forward_accept(LIBSSH2_LISTENER *listener)
{
    LIBSSH2_CHANNEL *ptr;

    if(!listener)
        return NULL;

    BLOCK_ADJUST_ERRNO(ptr, listener->session,
                       channel_forward_accept(listener));
    return ptr;

}


static int channel_setenv(LIBSSH2_CHANNEL *channel,
                          const char *varname, unsigned int varname_len,
                          const char *value, unsigned int value_len)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s, *data;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_CHANNEL_SUCCESS, SSH_MSG_CHANNEL_FAILURE, 0 };
    size_t data_len;
    int rc;

    if(channel->setenv_state == libssh2_NB_state_idle) {
        
        channel->setenv_packet_len = varname_len + value_len + 21;

        
        memset(&channel->setenv_packet_requirev_state, 0,
               sizeof(channel->setenv_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Setting remote environment variable: %s=%s on "
                       "channel %u/%u",
                       varname, value, channel->local.id, channel->remote.id));

        s = channel->setenv_packet =
            LIBSSH2_ALLOC(session, channel->setenv_packet_len);
        if(!channel->setenv_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory "
                                  "for setenv packet");
        }

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, "env", sizeof("env") - 1);
        *(s++) = 0x01;
        _libssh2_store_str(&s, varname, varname_len);
        _libssh2_store_str(&s, value, value_len);

        channel->setenv_state = libssh2_NB_state_created;
    }

    if(channel->setenv_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session,
                                     channel->setenv_packet,
                                     channel->setenv_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending setenv request");
            return rc;
        }
        else if(rc) {
            LIBSSH2_FREE(session, channel->setenv_packet);
            channel->setenv_packet = NULL;
            channel->setenv_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send channel-request packet for "
                                  "setenv request");
        }
        LIBSSH2_FREE(session, channel->setenv_packet);
        channel->setenv_packet = NULL;

        _libssh2_htonu32(channel->setenv_local_channel, channel->local.id);

        channel->setenv_state = libssh2_NB_state_sent;
    }

    if(channel->setenv_state == libssh2_NB_state_sent) {
        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      1, channel->setenv_local_channel, 4,
                                      &channel->
                                      setenv_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        if(rc) {
            channel->setenv_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Failed getting response for "
                                  "channel-setenv");
        }
        else if(data_len < 1) {
            channel->setenv_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                                  "Unexpected packet size");
        }

        if(data[0] == SSH_MSG_CHANNEL_SUCCESS) {
            LIBSSH2_FREE(session, data);
            channel->setenv_state = libssh2_NB_state_idle;
            return 0;
        }

        LIBSSH2_FREE(session, data);
    }

    channel->setenv_state = libssh2_NB_state_idle;
    return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED,
                          "Unable to complete request for channel-setenv");
}


LIBSSH2_API int
libssh2_channel_setenv_ex(LIBSSH2_CHANNEL *channel,
                          const char *varname, unsigned int varname_len,
                          const char *value, unsigned int value_len)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 channel_setenv(channel, varname, varname_len,
                                value, value_len));
    return rc;
}


static int channel_request_pty(LIBSSH2_CHANNEL *channel,
                               const char *term, unsigned int term_len,
                               const char *modes, unsigned int modes_len,
                               int width, int height,
                               int width_px, int height_px)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_CHANNEL_SUCCESS, SSH_MSG_CHANNEL_FAILURE, 0 };
    int rc;

    if(channel->reqPTY_state == libssh2_NB_state_idle) {
        
        if(term_len + modes_len > 256) {
            return _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                                  "term + mode lengths too large");
        }

        channel->reqPTY_packet_len = term_len + modes_len + 41;

        
        memset(&channel->reqPTY_packet_requirev_state, 0,
               sizeof(channel->reqPTY_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Allocating tty on channel %u/%u", channel->local.id,
                       channel->remote.id));

        s = channel->reqPTY_packet;

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, (char *)"pty-req", sizeof("pty-req") - 1);

        *(s++) = 0x01;

        _libssh2_store_str(&s, term, term_len);
        _libssh2_store_u32(&s, width);
        _libssh2_store_u32(&s, height);
        _libssh2_store_u32(&s, width_px);
        _libssh2_store_u32(&s, height_px);
        _libssh2_store_str(&s, modes, modes_len);

        channel->reqPTY_state = libssh2_NB_state_created;
    }

    if(channel->reqPTY_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, channel->reqPTY_packet,
                                     channel->reqPTY_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending pty request");
            return rc;
        }
        else if(rc) {
            channel->reqPTY_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Unable to send pty-request packet");
        }
        _libssh2_htonu32(channel->reqPTY_local_channel, channel->local.id);

        channel->reqPTY_state = libssh2_NB_state_sent;
    }

    if(channel->reqPTY_state == libssh2_NB_state_sent) {
        unsigned char *data;
        size_t data_len;
        unsigned char code;
        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      1, channel->reqPTY_local_channel, 4,
                                      &channel->reqPTY_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc || data_len < 1) {
            channel->reqPTY_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                                  "Failed to require the PTY package");
        }

        code = data[0];

        LIBSSH2_FREE(session, data);
        channel->reqPTY_state = libssh2_NB_state_idle;

        if(code == SSH_MSG_CHANNEL_SUCCESS)
            return 0;
    }

    return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED,
                          "Unable to complete request for "
                          "channel request-pty");
}


static int channel_request_auth_agent(LIBSSH2_CHANNEL *channel,
                                      const char *request_str,
                                      int request_str_len)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_CHANNEL_SUCCESS, SSH_MSG_CHANNEL_FAILURE, 0 };
    int rc;

    if(channel->req_auth_agent_state == libssh2_NB_state_idle) {
        
        if(request_str_len > 26) {
            return _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                                  "request_str length too large");
        }

        
        channel->req_auth_agent_packet_len = 10 + request_str_len;

        
        memset(&channel->req_auth_agent_requirev_state, 0,
               sizeof(channel->req_auth_agent_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Requesting auth agent on channel %u/%u",
                       channel->local.id, channel->remote.id));

        
        s = channel->req_auth_agent_packet;
        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, (char *)request_str, request_str_len);
        *(s++) = 0x01;

        channel->req_auth_agent_state = libssh2_NB_state_created;
    }

    if(channel->req_auth_agent_state == libssh2_NB_state_created) {
        
        rc = _libssh2_transport_send(session, channel->req_auth_agent_packet,
                                     channel->req_auth_agent_packet_len,
                                     NULL, 0);

        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending auth-agent request");
            return rc;
        }
        else if(rc) {
            channel->req_auth_agent_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Unable to send auth-agent request");
        }
        _libssh2_htonu32(channel->req_auth_agent_local_channel,
                         channel->local.id);
        channel->req_auth_agent_state = libssh2_NB_state_sent;
    }

    if(channel->req_auth_agent_state == libssh2_NB_state_sent) {
        unsigned char *data;
        size_t data_len;
        unsigned char code;

        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      1, channel->req_auth_agent_local_channel,
                                      4,
                                      &channel->req_auth_agent_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc) {
            channel->req_auth_agent_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                                  "Failed to request auth-agent");
        }

        code = data[0];

        LIBSSH2_FREE(session, data);
        channel->req_auth_agent_state = libssh2_NB_state_idle;

        if(code == SSH_MSG_CHANNEL_SUCCESS)
            return 0;
    }

    return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED,
                          "Unable to complete request for auth-agent");
}


LIBSSH2_API int
libssh2_channel_request_auth_agent(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    rc = LIBSSH2_ERROR_CHANNEL_UNKNOWN;

    
    if(channel->req_auth_agent_try_state == libssh2_NB_state_idle) {
        BLOCK_ADJUST(rc, channel->session,
                     channel_request_auth_agent(channel,
                                                "auth-agent-req@openssh.com",
                                                26));

        
        if(rc != LIBSSH2_ERROR_NONE &&
           rc != LIBSSH2_ERROR_EAGAIN)
            channel->req_auth_agent_try_state = libssh2_NB_state_sent;
    }

    if(channel->req_auth_agent_try_state == libssh2_NB_state_sent) {
        BLOCK_ADJUST(rc, channel->session,
                     channel_request_auth_agent(channel,
                                                "auth-agent-req", 14));

        
        if(rc != LIBSSH2_ERROR_NONE &&
           rc != LIBSSH2_ERROR_EAGAIN)
            channel->req_auth_agent_try_state = libssh2_NB_state_sent1;
    }

    
    if(rc == LIBSSH2_ERROR_NONE)
        channel->req_auth_agent_try_state = libssh2_NB_state_idle;

    return rc;
}


LIBSSH2_API int
libssh2_channel_request_pty_ex(LIBSSH2_CHANNEL *channel, const char *term,
                               unsigned int term_len, const char *modes,
                               unsigned int modes_len, int width, int height,
                               int width_px, int height_px)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 channel_request_pty(channel, term, term_len, modes,
                                     modes_len, width, height,
                                     width_px, height_px));
    return rc;
}

static int
channel_request_pty_size(LIBSSH2_CHANNEL * channel, int width,
                         int height, int width_px, int height_px)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s;
    int rc;
    int retcode = LIBSSH2_ERROR_PROTO;

    if(channel->reqPTY_state == libssh2_NB_state_idle) {
        channel->reqPTY_packet_len = 39;

        
        memset(&channel->reqPTY_packet_requirev_state, 0,
               sizeof(channel->reqPTY_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "changing tty size on channel %u/%u",
                       channel->local.id,
                       channel->remote.id));

        s = channel->reqPTY_packet;

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, (char *)"window-change",
                           sizeof("window-change") - 1);
        *(s++) = 0x00; 
        _libssh2_store_u32(&s, width);
        _libssh2_store_u32(&s, height);
        _libssh2_store_u32(&s, width_px);
        _libssh2_store_u32(&s, height_px);

        channel->reqPTY_state = libssh2_NB_state_created;
    }

    if(channel->reqPTY_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, channel->reqPTY_packet,
                                     channel->reqPTY_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending window-change request");
            return rc;
        }
        else if(rc) {
            channel->reqPTY_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Unable to send window-change packet");
        }
        _libssh2_htonu32(channel->reqPTY_local_channel, channel->local.id);
        retcode = LIBSSH2_ERROR_NONE;
    }

    channel->reqPTY_state = libssh2_NB_state_idle;
    return retcode;
}

LIBSSH2_API int
libssh2_channel_request_pty_size_ex(LIBSSH2_CHANNEL *channel, int width,
                                    int height, int width_px, int height_px)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 channel_request_pty_size(channel, width, height, width_px,
                                          height_px));
    return rc;
}


#define LIBSSH2_X11_RANDOM_COOKIE_LEN       32


static int
channel_x11_req(LIBSSH2_CHANNEL *channel, int single_connection,
                const char *auth_proto, const char *auth_cookie,
                int screen_number)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_CHANNEL_SUCCESS, SSH_MSG_CHANNEL_FAILURE, 0 };
    size_t proto_len =
        auth_proto ? strlen(auth_proto) : (sizeof("MIT-MAGIC-COOKIE-1") - 1);
    size_t cookie_len =
        auth_cookie ? strlen(auth_cookie) : LIBSSH2_X11_RANDOM_COOKIE_LEN;
    int rc;

    if(channel->reqX11_state == libssh2_NB_state_idle) {
        
        channel->reqX11_packet_len = proto_len + cookie_len + 30;

        
        memset(&channel->reqX11_packet_requirev_state, 0,
               sizeof(channel->reqX11_packet_requirev_state));

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Requesting x11-req for channel %u/%u: single=%d "
                       "proto=%s cookie=%s screen=%d",
                       channel->local.id, channel->remote.id,
                       single_connection,
                       auth_proto ? auth_proto : "MIT-MAGIC-COOKIE-1",
                       auth_cookie ? auth_cookie : "<random>", screen_number));

        s = channel->reqX11_packet =
            LIBSSH2_ALLOC(session, channel->reqX11_packet_len);
        if(!channel->reqX11_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for pty-request");
        }

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, "x11-req", sizeof("x11-req") - 1);

        *(s++) = 0x01;          
        *(s++) = single_connection ? 0x01 : 0x00;

        _libssh2_store_str(&s, auth_proto ? auth_proto : "MIT-MAGIC-COOKIE-1",
                           proto_len);

        _libssh2_store_u32(&s, (uint32_t)cookie_len);
        if(auth_cookie) {
            memcpy(s, auth_cookie, cookie_len);
        }
        else {
            int i;
            
            unsigned char buffer[(LIBSSH2_X11_RANDOM_COOKIE_LEN / 2) + 1];

            if(_libssh2_random(buffer, LIBSSH2_X11_RANDOM_COOKIE_LEN / 2)) {
                return _libssh2_error(session, LIBSSH2_ERROR_RANDGEN,
                                      "Unable to get random bytes "
                                      "for x11-req cookie");
            }
            for(i = 0; i < (LIBSSH2_X11_RANDOM_COOKIE_LEN / 2); i++) {
                snprintf((char *)&s[i*2], 3, "%02X", buffer[i]);
            }
        }
        s += cookie_len;

        _libssh2_store_u32(&s, screen_number);
        channel->reqX11_state = libssh2_NB_state_created;
    }

    if(channel->reqX11_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, channel->reqX11_packet,
                                     channel->reqX11_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending X11-req packet");
            return rc;
        }
        if(rc) {
            LIBSSH2_FREE(session, channel->reqX11_packet);
            channel->reqX11_packet = NULL;
            channel->reqX11_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Unable to send x11-req packet");
        }
        LIBSSH2_FREE(session, channel->reqX11_packet);
        channel->reqX11_packet = NULL;

        _libssh2_htonu32(channel->reqX11_local_channel, channel->local.id);

        channel->reqX11_state = libssh2_NB_state_sent;
    }

    if(channel->reqX11_state == libssh2_NB_state_sent) {
        size_t data_len;
        unsigned char *data;
        unsigned char code;

        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      1, channel->reqX11_local_channel, 4,
                                      &channel->reqX11_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc || data_len < 1) {
            channel->reqX11_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "waiting for x11-req response packet");
        }

        code = data[0];
        LIBSSH2_FREE(session, data);
        channel->reqX11_state = libssh2_NB_state_idle;

        if(code == SSH_MSG_CHANNEL_SUCCESS)
            return 0;
    }

    return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED,
                          "Unable to complete request for channel x11-req");
}


LIBSSH2_API int
libssh2_channel_x11_req_ex(LIBSSH2_CHANNEL *channel, int single_connection,
                           const char *auth_proto, const char *auth_cookie,
                           int screen_number)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 channel_x11_req(channel, single_connection, auth_proto,
                                 auth_cookie, screen_number));
    return rc;
}



int
_libssh2_channel_process_startup(LIBSSH2_CHANNEL *channel,
                                 const char *request, size_t request_len,
                                 const char *message, size_t message_len)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *s;
    static const unsigned char reply_codes[3] =
        { SSH_MSG_CHANNEL_SUCCESS, SSH_MSG_CHANNEL_FAILURE, 0 };
    int rc;

    if(channel->process_state == libssh2_NB_state_end) {
        return _libssh2_error(session, LIBSSH2_ERROR_BAD_USE,
                              "Channel can not be reused");
    }

    if(channel->process_state == libssh2_NB_state_idle) {
        
        channel->process_packet_len = request_len + 10;

        
        memset(&channel->process_packet_requirev_state, 0,
               sizeof(channel->process_packet_requirev_state));

        if(message)
            channel->process_packet_len += + 4;

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "starting request(%s) on channel %u/%u, message=%s",
                       request, channel->local.id, channel->remote.id,
                       message ? message : "<null>"));
        s = channel->process_packet =
            LIBSSH2_ALLOC(session, channel->process_packet_len);
        if(!channel->process_packet)
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory "
                                  "for channel-process request");

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, request, request_len);
        *(s++) = 0x01;

        if(message)
            _libssh2_store_u32(&s, (uint32_t)message_len);

        channel->process_state = libssh2_NB_state_created;
    }

    if(channel->process_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session,
                                     channel->process_packet,
                                     channel->process_packet_len,
                                     (unsigned char *)message, message_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending channel request");
            return rc;
        }
        else if(rc) {
            LIBSSH2_FREE(session, channel->process_packet);
            channel->process_packet = NULL;
            channel->process_state = libssh2_NB_state_end;
            return _libssh2_error(session, rc,
                                  "Unable to send channel request");
        }
        LIBSSH2_FREE(session, channel->process_packet);
        channel->process_packet = NULL;

        _libssh2_htonu32(channel->process_local_channel, channel->local.id);

        channel->process_state = libssh2_NB_state_sent;
    }

    if(channel->process_state == libssh2_NB_state_sent) {
        unsigned char *data;
        size_t data_len;
        unsigned char code;
        rc = _libssh2_packet_requirev(session, reply_codes, &data, &data_len,
                                      1, channel->process_local_channel, 4,
                                      &channel->process_packet_requirev_state);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc || data_len < 1) {
            channel->process_state = libssh2_NB_state_end;
            return _libssh2_error(session, rc,
                                  "Failed waiting for channel success");
        }

        code = data[0];
        LIBSSH2_FREE(session, data);
        channel->process_state = libssh2_NB_state_end;

        if(code == SSH_MSG_CHANNEL_SUCCESS)
            return 0;
    }

    return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED,
                          "Unable to complete request for "
                          "channel-process-startup");
}


LIBSSH2_API int
libssh2_channel_process_startup(LIBSSH2_CHANNEL *channel,
                                const char *req, unsigned int req_len,
                                const char *msg, unsigned int msg_len)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_process_startup(channel, req, req_len,
                                                  msg, msg_len));
    return rc;
}



LIBSSH2_API void
libssh2_channel_set_blocking(LIBSSH2_CHANNEL * channel, int blocking)
{
    if(channel)
        (void)_libssh2_session_set_blocking(channel->session, blocking);
}


int
_libssh2_channel_flush(LIBSSH2_CHANNEL *channel, int streamid)
{
    if(channel->flush_state == libssh2_NB_state_idle) {
        LIBSSH2_PACKET *packet =
            _libssh2_list_first(&channel->session->packets);
        channel->flush_refund_bytes = 0;
        channel->flush_flush_bytes = 0;

        while(packet) {
            unsigned char packet_type;
            LIBSSH2_PACKET *next = _libssh2_list_next(&packet->node);

            if(packet->data_len < 1) {
                packet = next;
                _libssh2_debug((channel->session, LIBSSH2_TRACE_ERROR,
                               "Unexpected packet length"));
                continue;
            }

            packet_type = packet->data[0];

            if(((packet_type == SSH_MSG_CHANNEL_DATA)
                || (packet_type == SSH_MSG_CHANNEL_EXTENDED_DATA))
               && ((packet->data_len >= 5)
                   && (_libssh2_ntohu32(packet->data + 1)
                       == channel->local.id))) {
                
                int packet_stream_id;

                if(packet_type == SSH_MSG_CHANNEL_DATA) {
                    packet_stream_id = 0;
                }
                else if(packet->data_len >= 9) {
                    packet_stream_id = _libssh2_ntohu32(packet->data + 5);
                }
                else {
                    channel->flush_state = libssh2_NB_state_idle;
                    return _libssh2_error(channel->session,
                                          LIBSSH2_ERROR_PROTO,
                                          "Unexpected packet length");
                }

                if((streamid == LIBSSH2_CHANNEL_FLUSH_ALL)
                    || ((packet_type == SSH_MSG_CHANNEL_EXTENDED_DATA)
                        && ((streamid == LIBSSH2_CHANNEL_FLUSH_EXTENDED_DATA)
                            || (streamid == packet_stream_id)))
                    || ((packet_type == SSH_MSG_CHANNEL_DATA)
                        && (streamid == 0))) {
                    size_t bytes_to_flush = packet->data_len -
                        packet->data_head;

                    _libssh2_debug((channel->session, LIBSSH2_TRACE_CONN,
                                   "Flushing %ld bytes of data from stream "
                                   "%d on channel %u/%u",
                                   (long)bytes_to_flush, packet_stream_id,
                                   channel->local.id, channel->remote.id));

                    
                    channel->flush_refund_bytes += packet->data_len - 13;
                    channel->flush_flush_bytes += bytes_to_flush;

                    LIBSSH2_FREE(channel->session, packet->data);

                    
                    _libssh2_list_remove(&packet->node);
                    LIBSSH2_FREE(channel->session, packet);
                }
            }
            packet = next;
        }

        channel->flush_state = libssh2_NB_state_created;
    }

    channel->read_avail -= channel->flush_flush_bytes;
    channel->remote.window_size -= (uint32_t)channel->flush_flush_bytes;

    if(channel->flush_refund_bytes) {
        int rc =
            _libssh2_channel_receive_window_adjust(channel,
                                         (uint32_t)channel->flush_refund_bytes,
                                         1, NULL);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
    }

    channel->flush_state = libssh2_NB_state_idle;

    return (int)channel->flush_flush_bytes;
}


LIBSSH2_API int
libssh2_channel_flush_ex(LIBSSH2_CHANNEL *channel, int stream)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_flush(channel, stream));
    return rc;
}


LIBSSH2_API int
libssh2_channel_get_exit_status(LIBSSH2_CHANNEL *channel)
{
    if(!channel)
        return 0;

    return channel->exit_status;
}


LIBSSH2_API int
libssh2_channel_get_exit_signal(LIBSSH2_CHANNEL *channel,
                                char **exitsignal,
                                size_t *exitsignal_len,
                                char **errmsg,
                                size_t *errmsg_len,
                                char **langtag,
                                size_t *langtag_len)
{
    size_t namelen = 0;

    if(channel) {
        LIBSSH2_SESSION *session = channel->session;

        if(channel->exit_signal) {
            namelen = strlen(channel->exit_signal);
            if(exitsignal) {
                *exitsignal = LIBSSH2_ALLOC(session, namelen + 1);
                if(!*exitsignal) {
                    return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for signal name");
                }
                memcpy(*exitsignal, channel->exit_signal, namelen);
                (*exitsignal)[namelen] = '\0';
            }
            if(exitsignal_len)
                *exitsignal_len = namelen;
        }
        else {
            if(exitsignal)
                *exitsignal = NULL;
            if(exitsignal_len)
                *exitsignal_len = 0;
        }

        

        if(errmsg)
            *errmsg = NULL;

        if(errmsg_len)
            *errmsg_len = 0;

        if(langtag)
            *langtag = NULL;

        if(langtag_len)
            *langtag_len = 0;
    }

    return LIBSSH2_ERROR_NONE;
}


int
_libssh2_channel_receive_window_adjust(LIBSSH2_CHANNEL * channel,
                                       uint32_t adjustment,
                                       unsigned char force,
                                       unsigned int *store)
{
    int rc;

    if(store)
        *store = channel->remote.window_size;

    if(channel->adjust_state == libssh2_NB_state_idle) {
        if(!force
            && (adjustment + channel->adjust_queue <
                LIBSSH2_CHANNEL_MINADJUST)) {
            _libssh2_debug((channel->session, LIBSSH2_TRACE_CONN,
                           "Queueing %u bytes for receive window adjustment "
                           "for channel %u/%u",
                           adjustment, channel->local.id, channel->remote.id));
            channel->adjust_queue += adjustment;
            return 0;
        }

        if(!adjustment && !channel->adjust_queue) {
            return 0;
        }

        adjustment += channel->adjust_queue;
        channel->adjust_queue = 0;

        
        channel->adjust_adjust[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
        _libssh2_htonu32(&channel->adjust_adjust[1], channel->remote.id);
        _libssh2_htonu32(&channel->adjust_adjust[5], adjustment);
        _libssh2_debug((channel->session, LIBSSH2_TRACE_CONN,
                       "Adjusting window %u bytes for data on "
                       "channel %u/%u",
                       adjustment, channel->local.id, channel->remote.id));

        channel->adjust_state = libssh2_NB_state_created;
    }

    rc = _libssh2_transport_send(channel->session, channel->adjust_adjust, 9,
                                 NULL, 0);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        _libssh2_error(channel->session, rc,
                       "Would block sending window adjust");
        return rc;
    }
    else if(rc) {
        channel->adjust_queue = adjustment;
        return _libssh2_error(channel->session, LIBSSH2_ERROR_SOCKET_SEND,
                              "Unable to send transfer-window adjustment "
                              "packet, deferring");
    }
    else {
        channel->remote.window_size += adjustment;
    }

    channel->adjust_state = libssh2_NB_state_idle;

    return 0;
}

#ifndef LIBSSH2_NO_DEPRECATED

LIBSSH2_API unsigned long
libssh2_channel_receive_window_adjust(LIBSSH2_CHANNEL *channel,
                                      unsigned long adj,
                                      unsigned char force)
{
    unsigned int window;
    int rc;

    if(!channel)
        return (unsigned long)LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_receive_window_adjust(channel,
                                                        (uint32_t)adj,
                                                        force, &window));

    
    return rc ? (unsigned long)rc : window;
}
#endif


LIBSSH2_API int
libssh2_channel_receive_window_adjust2(LIBSSH2_CHANNEL *channel,
                                       unsigned long adj,
                                       unsigned char force,
                                       unsigned int *window)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_receive_window_adjust(channel,
                                                        (uint32_t)adj,
                                                        force, window));
    return rc;
}

int
_libssh2_channel_extended_data(LIBSSH2_CHANNEL *channel, int ignore_mode)
{
    if(channel->extData2_state == libssh2_NB_state_idle) {
        _libssh2_debug((channel->session, LIBSSH2_TRACE_CONN,
                       "Setting channel %u/%u handle_extended_data"
                       " mode to %d",
                       channel->local.id, channel->remote.id, ignore_mode));
        channel->remote.extended_data_ignore_mode = (char)ignore_mode;

        channel->extData2_state = libssh2_NB_state_created;
    }

    if(channel->extData2_state == libssh2_NB_state_idle) {
        if(ignore_mode == LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE) {
            int rc =
                _libssh2_channel_flush(channel,
                                       LIBSSH2_CHANNEL_FLUSH_EXTENDED_DATA);
            if(LIBSSH2_ERROR_EAGAIN == rc)
                return rc;
        }
    }

    channel->extData2_state = libssh2_NB_state_idle;
    return 0;
}


LIBSSH2_API int
libssh2_channel_handle_extended_data2(LIBSSH2_CHANNEL *channel,
                                      int mode)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, _libssh2_channel_extended_data(channel,
                                                                      mode));
    return rc;
}

#ifndef LIBSSH2_NO_DEPRECATED

LIBSSH2_API void
libssh2_channel_handle_extended_data(LIBSSH2_CHANNEL *channel,
                                     int ignore_mode)
{
    (void)libssh2_channel_handle_extended_data2(channel, ignore_mode);
}
#endif



ssize_t _libssh2_channel_read(LIBSSH2_CHANNEL *channel, int stream_id,
                              char *buf, size_t buflen)
{
    LIBSSH2_SESSION *session = channel->session;
    int rc;
    size_t bytes_read = 0;
    size_t bytes_want;
    int unlink_packet;
    LIBSSH2_PACKET *read_packet;
    LIBSSH2_PACKET *read_next;

    _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                   "channel_read() wants %ld bytes from channel %u/%u "
                   "stream #%d",
                   (long)buflen, channel->local.id, channel->remote.id,
                   stream_id));

    
    if((channel->read_state == libssh2_NB_state_jump1) ||
       (channel->remote.window_size <
        channel->remote.window_size_initial / 4 * 3 + buflen)) {

        uint32_t adjustment = (uint32_t)(channel->remote.window_size_initial +
            buflen - channel->remote.window_size);
        if(adjustment < LIBSSH2_CHANNEL_MINADJUST)
            adjustment = LIBSSH2_CHANNEL_MINADJUST;

        
        channel->read_state = libssh2_NB_state_jump1;
        rc = _libssh2_channel_receive_window_adjust(channel, adjustment,
                                                    0, NULL);
        if(rc)
            return rc;

        channel->read_state = libssh2_NB_state_idle;
    }

    
    do {
        rc = _libssh2_transport_read(session);
    } while(rc > 0);

    if((rc < 0) && (rc != LIBSSH2_ERROR_EAGAIN))
        return _libssh2_error(session, rc, "transport read");

    read_packet = _libssh2_list_first(&session->packets);
    while(read_packet && (bytes_read < buflen)) {
        

        LIBSSH2_PACKET *readpkt = read_packet;

        
        read_next = _libssh2_list_next(&readpkt->node);

        if(readpkt->data_len < 5) {
            read_packet = read_next;

            if(readpkt->data_len != 1 ||
                readpkt->data[0] != SSH_MSG_REQUEST_FAILURE) {
                _libssh2_debug((channel->session, LIBSSH2_TRACE_ERROR,
                               "Unexpected packet length"));
            }

            continue;
        }

        channel->read_local_id =
            _libssh2_ntohu32(readpkt->data + 1);

        
        if((stream_id
             && (readpkt->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA)
             && (channel->local.id == channel->read_local_id)
             && (readpkt->data_len >= 9)
             && (stream_id == (int) _libssh2_ntohu32(readpkt->data + 5)))
            || (!stream_id && (readpkt->data[0] == SSH_MSG_CHANNEL_DATA)
                && (channel->local.id == channel->read_local_id))
            || (!stream_id
                && (readpkt->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA)
                && (channel->local.id == channel->read_local_id)
                && (channel->remote.extended_data_ignore_mode ==
                    LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE))) {

            
            bytes_want = buflen - bytes_read;
            unlink_packet = FALSE;

            if(bytes_want >= (readpkt->data_len - readpkt->data_head)) {
                
                bytes_want = readpkt->data_len - readpkt->data_head;
                unlink_packet = TRUE;
            }

            _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                           "channel_read() got %ld of data from %u/%u/%d%s",
                           (long)bytes_want, channel->local.id,
                           channel->remote.id, stream_id,
                           unlink_packet ? " [ul]" : ""));

            
            memcpy(&buf[bytes_read],
                   &readpkt->data[readpkt->data_head], bytes_want);

            
            readpkt->data_head += bytes_want;
            bytes_read += bytes_want;

            
            if(unlink_packet) {
                
                _libssh2_list_remove(&readpkt->node);

                LIBSSH2_FREE(session, readpkt->data);
                LIBSSH2_FREE(session, readpkt);
            }
        }

        
        read_packet = read_next;
    }

    if(!bytes_read) {
        
        if(channel->remote.eof || channel->remote.close)
            return 0;
        else if(rc != LIBSSH2_ERROR_EAGAIN)
            return 0;

        
        return _libssh2_error(session, rc, "would block");
    }

    channel->read_avail -= bytes_read;
    channel->remote.window_size -= (uint32_t)bytes_read;

    return bytes_read;
}


LIBSSH2_API ssize_t
libssh2_channel_read_ex(LIBSSH2_CHANNEL *channel, int stream_id, char *buf,
                        size_t buflen)
{
    ssize_t rc;
    unsigned long recv_window;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    recv_window = libssh2_channel_window_read_ex(channel, NULL, NULL);

    if(buflen > recv_window) {
        BLOCK_ADJUST(rc, channel->session,
                     _libssh2_channel_receive_window_adjust(channel,
                                                   (uint32_t)buflen, 1, NULL));
    }

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_read(channel, stream_id, buf, buflen));
    return rc;
}


size_t
_libssh2_channel_packet_data_len(LIBSSH2_CHANNEL * channel, int stream_id)
{
    LIBSSH2_SESSION *session = channel->session;
    LIBSSH2_PACKET *read_packet;
    LIBSSH2_PACKET *next_packet;
    uint32_t read_local_id;

    read_packet = _libssh2_list_first(&session->packets);
    if(!read_packet)
        return 0;

    while(read_packet) {

        next_packet = _libssh2_list_next(&read_packet->node);

        if(read_packet->data_len < 5) {
            read_packet = next_packet;
            _libssh2_debug((channel->session, LIBSSH2_TRACE_ERROR,
                           "Unexpected packet length"));
            continue;
        }

        read_local_id = _libssh2_ntohu32(read_packet->data + 1);

        
        if((stream_id
             && (read_packet->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA)
             && (channel->local.id == read_local_id)
             && (read_packet->data_len >= 9)
             && (stream_id == (int) _libssh2_ntohu32(read_packet->data + 5)))
            ||
            (!stream_id
             && (read_packet->data[0] == SSH_MSG_CHANNEL_DATA)
             && (channel->local.id == read_local_id))
            ||
            (!stream_id
             && (read_packet->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA)
             && (channel->local.id == read_local_id)
             && (channel->remote.extended_data_ignore_mode
                 == LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE))) {
            return read_packet->data_len - read_packet->data_head;
        }

        read_packet = next_packet;
    }

    return 0;
}


ssize_t
_libssh2_channel_write(LIBSSH2_CHANNEL *channel, int stream_id,
                       const unsigned char *buf, size_t buflen)
{
    int rc = 0;
    LIBSSH2_SESSION *session = channel->session;
    ssize_t wrote = 0; 

    
    if(buflen > 32700)
        buflen = 32700;

    if(channel->write_state == libssh2_NB_state_idle) {
        unsigned char *s = channel->write_packet;

        _libssh2_debug((channel->session, LIBSSH2_TRACE_CONN,
                       "Writing %ld bytes on channel %u/%u, stream #%d",
                       (long)buflen, channel->local.id, channel->remote.id,
                       stream_id));

        if(channel->local.close)
            return _libssh2_error(channel->session,
                                  LIBSSH2_ERROR_CHANNEL_CLOSED,
                                  "We have already closed this channel");
        else if(channel->local.eof)
            return _libssh2_error(channel->session,
                                  LIBSSH2_ERROR_CHANNEL_EOF_SENT,
                                  "EOF has already been received, "
                                  "data might be ignored");

        
        do
            rc = _libssh2_transport_read(session);
        while(rc > 0);

        if((rc < 0) && (rc != LIBSSH2_ERROR_EAGAIN)) {
            return _libssh2_error(channel->session, rc,
                                  "Failure while draining incoming flow");
        }

        if(channel->local.window_size <= 0) {
            

            
            session->socket_block_directions = LIBSSH2_SESSION_BLOCK_INBOUND;

            return rc == LIBSSH2_ERROR_EAGAIN ? rc : 0;
        }

        channel->write_bufwrite = buflen;

        *(s++) = stream_id ? SSH_MSG_CHANNEL_EXTENDED_DATA :
            SSH_MSG_CHANNEL_DATA;
        _libssh2_store_u32(&s, channel->remote.id);
        if(stream_id)
            _libssh2_store_u32(&s, stream_id);

        
        
        if(channel->write_bufwrite > channel->local.window_size) {
            _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                           "Splitting write block due to %u byte "
                           "window_size on %u/%u/%d",
                           channel->local.window_size, channel->local.id,
                           channel->remote.id, stream_id));
            channel->write_bufwrite = channel->local.window_size;
        }
        if(channel->write_bufwrite > channel->local.packet_size) {
            _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                           "Splitting write block due to %u byte "
                           "packet_size on %u/%u/%d",
                           channel->local.packet_size, channel->local.id,
                           channel->remote.id, stream_id));
            channel->write_bufwrite = channel->local.packet_size;
        }
        
        _libssh2_store_u32(&s, (uint32_t)channel->write_bufwrite);
        channel->write_packet_len = s - channel->write_packet;

        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Sending %ld bytes on channel %u/%u, stream_id=%d",
                       (long)channel->write_bufwrite, channel->local.id,
                       channel->remote.id, stream_id));

        channel->write_state = libssh2_NB_state_created;
    }

    if(channel->write_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, channel->write_packet,
                                     channel->write_packet_len,
                                     buf, channel->write_bufwrite);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return _libssh2_error(session, rc,
                                  "Unable to send channel data");
        }
        else if(rc) {
            channel->write_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "Unable to send channel data");
        }
        
        channel->local.window_size -= (uint32_t)channel->write_bufwrite;

        wrote += channel->write_bufwrite;

        

        channel->write_state = libssh2_NB_state_idle;

        return wrote;
    }

    return LIBSSH2_ERROR_INVAL; 
}


LIBSSH2_API ssize_t
libssh2_channel_write_ex(LIBSSH2_CHANNEL *channel, int stream_id,
                         const char *buf, size_t buflen)
{
    ssize_t rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 _libssh2_channel_write(channel, stream_id,
                                        (unsigned char *)buf, buflen));
    return rc;
}


static int channel_send_eof(LIBSSH2_CHANNEL *channel)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char packet[5];    
    int rc;

    _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                   "Sending EOF on channel %u/%u",
                   channel->local.id, channel->remote.id));
    packet[0] = SSH_MSG_CHANNEL_EOF;
    _libssh2_htonu32(packet + 1, channel->remote.id);
    rc = _libssh2_transport_send(session, packet, 5, NULL, 0);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        _libssh2_error(session, rc,
                       "Would block sending EOF");
        return rc;
    }
    else if(rc) {
        return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                              "Unable to send EOF on channel");
    }
    channel->local.eof = 1;

    return 0;
}


LIBSSH2_API int
libssh2_channel_send_eof(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, channel_send_eof(channel));
    return rc;
}


LIBSSH2_API int
libssh2_channel_eof(LIBSSH2_CHANNEL * channel)
{
    LIBSSH2_SESSION *session;
    LIBSSH2_PACKET *packet;
    LIBSSH2_PACKET *next_packet;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    session = channel->session;
    packet = _libssh2_list_first(&session->packets);

    while(packet) {

        next_packet = _libssh2_list_next(&packet->node);

        if(packet->data_len < 1) {
            packet = next_packet;
            _libssh2_debug((channel->session, LIBSSH2_TRACE_ERROR,
                           "Unexpected packet length"));
            continue;
        }

        if(((packet->data[0] == SSH_MSG_CHANNEL_DATA)
            || (packet->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA))
            && ((packet->data_len >= 5)
            && (channel->local.id == _libssh2_ntohu32(packet->data + 1)))) {
            
            return 0;
        }
        packet = next_packet;
    }

    return channel->remote.eof;
}


static int channel_wait_eof(LIBSSH2_CHANNEL *channel)
{
    LIBSSH2_SESSION *session = channel->session;
    int rc;

    if(channel->wait_eof_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Awaiting EOF for channel %u/%u", channel->local.id,
                       channel->remote.id));

        channel->wait_eof_state = libssh2_NB_state_created;
    }

    
    do {
        if(channel->remote.eof) {
            break;
        }

        if((channel->remote.window_size == channel->read_avail) &&
            session->api_block_mode)
            return _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_WINDOW_FULL,
                                  "Receiving channel window "
                                  "has been exhausted");

        rc = _libssh2_transport_read(session);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc < 0) {
            channel->wait_eof_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc,
                                  "_libssh2_transport_read() bailed out");
        }
    } while(1);

    channel->wait_eof_state = libssh2_NB_state_idle;

    return 0;
}


LIBSSH2_API int
libssh2_channel_wait_eof(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, channel_wait_eof(channel));
    return rc;
}

int _libssh2_channel_close(LIBSSH2_CHANNEL * channel)
{
    LIBSSH2_SESSION *session = channel->session;
    int rc = 0;

    if(channel->local.close) {
        
        channel->close_state = libssh2_NB_state_idle;
        return 0;
    }

    if(!channel->local.eof) {
        rc = channel_send_eof(channel);
        if(rc) {
            if(rc == LIBSSH2_ERROR_EAGAIN) {
                return rc;
            }
            _libssh2_error(session, rc,
                           "Unable to send EOF, but closing channel anyway");
        }
    }

    

    if(channel->close_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_CONN, "Closing channel %u/%u",
                       channel->local.id, channel->remote.id));

        channel->close_packet[0] = SSH_MSG_CHANNEL_CLOSE;
        _libssh2_htonu32(channel->close_packet + 1, channel->remote.id);

        channel->close_state = libssh2_NB_state_created;
    }

    if(channel->close_state == libssh2_NB_state_created) {
        rc = _libssh2_transport_send(session, channel->close_packet, 5,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc,
                           "Would block sending close-channel");
            return rc;

        }
        else if(rc) {
            _libssh2_error(session, rc,
                           "Unable to send close-channel request, "
                           "but closing anyway");
            

        }
        else
            channel->close_state = libssh2_NB_state_sent;
    }

    if(channel->close_state == libssh2_NB_state_sent) {
        

        while(!channel->remote.close && !rc &&
               (session->socket_state != LIBSSH2_SOCKET_DISCONNECTED))
            rc = _libssh2_transport_read(session);
    }

    if(rc != LIBSSH2_ERROR_EAGAIN) {
        
        channel->local.close = 1;

        
        if(channel->close_cb) {
            LIBSSH2_CHANNEL_CLOSE(session, channel);
        }

        channel->close_state = libssh2_NB_state_idle;
    }

    
    return rc >= 0 ? 0 : rc;
}


LIBSSH2_API int
libssh2_channel_close(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, _libssh2_channel_close(channel));
    return rc;
}


static int channel_wait_closed(LIBSSH2_CHANNEL *channel)
{
    LIBSSH2_SESSION *session = channel->session;
    int rc;

    if(!channel->remote.eof) {
        return _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                              "libssh2_channel_wait_closed() invoked when "
                              "channel is not in EOF state");
    }

    if(channel->wait_closed_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Awaiting close of channel %u/%u", channel->local.id,
                       channel->remote.id));

        channel->wait_closed_state = libssh2_NB_state_created;
    }

    
    if(!channel->remote.close) {
        do {
            rc = _libssh2_transport_read(session);
            if(channel->remote.close)
                
                break;
        } while(rc > 0);
        if(rc < 0)
            return rc;
    }

    channel->wait_closed_state = libssh2_NB_state_idle;

    return 0;
}


LIBSSH2_API int
libssh2_channel_wait_closed(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, channel_wait_closed(channel));
    return rc;
}


int _libssh2_channel_free(LIBSSH2_CHANNEL *channel)
{
    LIBSSH2_SESSION *session = channel->session;
    unsigned char channel_id[4];
    unsigned char *data;
    size_t data_len;
    int rc;

    assert(session);

    if(channel->free_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                       "Freeing channel %u/%u resources", channel->local.id,
                       channel->remote.id));

        channel->free_state = libssh2_NB_state_created;
    }

    
    if(!channel->local.close
        && (session->socket_state == LIBSSH2_SOCKET_CONNECTED)) {
        rc = _libssh2_channel_close(channel);

        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;

        
    }

    channel->free_state = libssh2_NB_state_idle;

    if(channel->exit_signal) {
        LIBSSH2_FREE(session, channel->exit_signal);
    }

    

    
    _libssh2_htonu32(channel_id, channel->local.id);
    while((_libssh2_packet_ask(session, SSH_MSG_CHANNEL_DATA, &data,
                               &data_len, 1, channel_id, 4) >= 0)
          ||
          (_libssh2_packet_ask(session, SSH_MSG_CHANNEL_EXTENDED_DATA, &data,
                               &data_len, 1, channel_id, 4) >= 0)) {
        LIBSSH2_FREE(session, data);
    }

    
    if(channel->channel_type) {
        LIBSSH2_FREE(session, channel->channel_type);
    }

    
    _libssh2_list_remove(&channel->node);

    
    if(channel->setenv_packet) {
        LIBSSH2_FREE(session, channel->setenv_packet);
    }
    if(channel->reqX11_packet) {
        LIBSSH2_FREE(session, channel->reqX11_packet);
    }
    if(channel->process_packet) {
        LIBSSH2_FREE(session, channel->process_packet);
    }

    LIBSSH2_FREE(session, channel);

    return 0;
}


LIBSSH2_API int
libssh2_channel_free(LIBSSH2_CHANNEL *channel)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session, _libssh2_channel_free(channel));
    return rc;
}

LIBSSH2_API unsigned long
libssh2_channel_window_read_ex(LIBSSH2_CHANNEL *channel,
         unsigned long *read_avail,
                               unsigned long *window_size_initial)
{
    if(!channel)
        return 0; 

    if(window_size_initial) {
        *window_size_initial = channel->remote.window_size_initial;
    }

    if(read_avail) {
        size_t bytes_queued = 0;
        LIBSSH2_PACKET *next_packet;
        LIBSSH2_PACKET *packet =
            _libssh2_list_first(&channel->session->packets);

        while(packet) {
            unsigned char packet_type;
               next_packet = _libssh2_list_next(&packet->node);

            if(packet->data_len < 1) {
                packet = next_packet;
                _libssh2_debug((channel->session, LIBSSH2_TRACE_ERROR,
                               "Unexpected packet length"));
                continue;
            }

            packet_type = packet->data[0];

            if(((packet_type == SSH_MSG_CHANNEL_DATA)
                || (packet_type == SSH_MSG_CHANNEL_EXTENDED_DATA))
               && ((packet->data_len >= 5)
                   && (_libssh2_ntohu32(packet->data + 1) ==
                       channel->local.id))) {
                bytes_queued += packet->data_len - packet->data_head;
            }

            packet = next_packet;
        }

        *read_avail = (unsigned long)bytes_queued;
    }

    return channel->remote.window_size;
}


LIBSSH2_API unsigned long
libssh2_channel_window_write_ex(LIBSSH2_CHANNEL *channel,
                                unsigned long *window_size_initial)
{
    if(!channel)
        return 0; 

    if(window_size_initial) {
        
        *window_size_initial = channel->local.window_size_initial;
    }

    return channel->local.window_size;
}


static int channel_signal(LIBSSH2_CHANNEL *channel,
                          const char *signame,
                          size_t signame_len)
{
    LIBSSH2_SESSION *session = channel->session;
    int retcode = LIBSSH2_ERROR_PROTO;

    if(channel->sendsignal_state == libssh2_NB_state_idle) {
        unsigned char *s;

        
        channel->sendsignal_packet_len = 20 + signame_len;

        s = channel->sendsignal_packet =
            LIBSSH2_ALLOC(session, channel->sendsignal_packet_len);
        if(!channel->sendsignal_packet)
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "signal request");

        *(s++) = SSH_MSG_CHANNEL_REQUEST;
        _libssh2_store_u32(&s, channel->remote.id);
        _libssh2_store_str(&s, "signal", sizeof("signal") - 1);
        *(s++) = 0x00;  
        _libssh2_store_str(&s, signame, signame_len);

        channel->sendsignal_state = libssh2_NB_state_created;
    }

    if(channel->sendsignal_state == libssh2_NB_state_created) {
        int rc;

        rc = _libssh2_transport_send(session, channel->sendsignal_packet,
                                     channel->sendsignal_packet_len,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, rc, "Would block sending signal request");
            return rc;
        }
        else if(rc) {
            LIBSSH2_FREE(session, channel->sendsignal_packet);
            channel->sendsignal_state = libssh2_NB_state_idle;
            return _libssh2_error(session, rc, "Unable to send signal packet");
        }
        LIBSSH2_FREE(session, channel->sendsignal_packet);
        retcode = LIBSSH2_ERROR_NONE;
    }

    channel->sendsignal_state = libssh2_NB_state_idle;

    return retcode;
}

LIBSSH2_API int
libssh2_channel_signal_ex(LIBSSH2_CHANNEL *channel,
                          const char *signame,
                          size_t signame_len)
{
    int rc;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    BLOCK_ADJUST(rc, channel->session,
                 channel_signal(channel, signame, signame_len));
    return rc;
}
