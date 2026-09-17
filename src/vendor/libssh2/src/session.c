

#include "libssh2_priv.h"

#ifdef _WIN32
#include <ws2tcpip.h>  
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#include "transport.h"
#include "session.h"
#include "channel.h"
#include "mac.h"

#if defined(_WIN32)
#define libssh2_usec_t long
#elif defined(__APPLE__)
#define libssh2_usec_t suseconds_t
#else
#undef libssh2_usec_t
#endif


static
LIBSSH2_ALLOC_FUNC(libssh2_default_alloc)
{
    (void)abstract;
    return malloc(count);
}


static
LIBSSH2_FREE_FUNC(libssh2_default_free)
{
    (void)abstract;
    free(ptr);
}


static
LIBSSH2_REALLOC_FUNC(libssh2_default_realloc)
{
    (void)abstract;
    return realloc(ptr, count);
}


static int
banner_receive(LIBSSH2_SESSION * session)
{
    ssize_t ret;
    size_t banner_len;

    if(session->banner_TxRx_state == libssh2_NB_state_idle) {
        banner_len = 0;

        session->banner_TxRx_state = libssh2_NB_state_created;
    }
    else {
        banner_len = session->banner_TxRx_total_send;
    }

    while((banner_len < sizeof(session->banner_TxRx_banner)) &&
          ((banner_len == 0)
           || (session->banner_TxRx_banner[banner_len - 1] != '\n'))) {
        char c = '\0';

        
        session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_INBOUND;

        ret = LIBSSH2_RECV(session, &c, 1,
                           LIBSSH2_SOCKET_RECV_FLAGS(session));
        if(ret < 0) {
            if(session->api_block_mode || (ret != -EAGAIN))
                
                _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                               "Error recving %d bytes: %ld", 1, (long)-ret));
        }
        else
            _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                           "Recved %ld bytes banner", (long)ret));

        if(ret < 0) {
            if(ret == -EAGAIN) {
                session->socket_block_directions =
                    LIBSSH2_SESSION_BLOCK_INBOUND;
                session->banner_TxRx_total_send = banner_len;
                return LIBSSH2_ERROR_EAGAIN;
            }

            
            session->banner_TxRx_state = libssh2_NB_state_idle;
            session->banner_TxRx_total_send = 0;
            return LIBSSH2_ERROR_SOCKET_RECV;
        }

        if(ret == 0) {
            session->socket_state = LIBSSH2_SOCKET_DISCONNECTED;
            return LIBSSH2_ERROR_SOCKET_DISCONNECT;
        }

        if((c == '\r' || c == '\n') && banner_len == 0) {
            continue;
        }

        if(c == '\0') {
            
            session->banner_TxRx_state = libssh2_NB_state_idle;
            session->banner_TxRx_total_send = 0;
            return LIBSSH2_ERROR_BANNER_RECV;
        }

        session->banner_TxRx_banner[banner_len++] = c;
    }

    while(banner_len &&
          ((session->banner_TxRx_banner[banner_len - 1] == '\n') ||
           (session->banner_TxRx_banner[banner_len - 1] == '\r'))) {
        banner_len--;
    }

    
    session->banner_TxRx_state = libssh2_NB_state_idle;
    session->banner_TxRx_total_send = 0;

    if(!banner_len)
        return LIBSSH2_ERROR_BANNER_RECV;

    if(session->remote.banner)
        LIBSSH2_FREE(session, session->remote.banner);

    session->remote.banner = LIBSSH2_ALLOC(session, banner_len + 1);
    if(!session->remote.banner) {
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Error allocating space for remote banner");
    }
    memcpy(session->remote.banner, session->banner_TxRx_banner, banner_len);
    session->remote.banner[banner_len] = '\0';
    _libssh2_debug((session, LIBSSH2_TRACE_TRANS, "Received Banner: %s",
                   session->remote.banner));
    return LIBSSH2_ERROR_NONE;
}


static int
banner_send(LIBSSH2_SESSION * session)
{
    char *banner = (char *) LIBSSH2_SSH_DEFAULT_BANNER_WITH_CRLF;
    size_t banner_len = sizeof(LIBSSH2_SSH_DEFAULT_BANNER_WITH_CRLF) - 1;
    ssize_t ret;

    if(session->banner_TxRx_state == libssh2_NB_state_idle) {
        if(session->local.banner) {
            
            banner_len = strlen((char *) session->local.banner);
            banner = (char *) session->local.banner;
        }
#ifdef LIBSSH2DEBUG
        {
            char banner_dup[256];

            
            if(banner_len < 256) {
                memcpy(banner_dup, banner, banner_len - 2);
                banner_dup[banner_len - 2] = '\0';
            }
            else {
                memcpy(banner_dup, banner, 255);
                banner_dup[255] = '\0';
            }

            _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                           "Sending Banner: %s", banner_dup));
        }
#endif

        session->banner_TxRx_state = libssh2_NB_state_created;
    }

    
    session->socket_block_directions &= ~LIBSSH2_SESSION_BLOCK_OUTBOUND;

    ret = LIBSSH2_SEND(session,
                       banner + session->banner_TxRx_total_send,
                       banner_len - session->banner_TxRx_total_send,
                       LIBSSH2_SOCKET_SEND_FLAGS(session));
    if(ret < 0)
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Error sending %ld bytes: %ld",
                       (long)(banner_len - session->banner_TxRx_total_send),
                       (long)-ret));
    else
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Sent %ld/%ld bytes at %p+%ld", (long)ret,
                       (long)(banner_len - session->banner_TxRx_total_send),
                       (void *)banner, (long)session->banner_TxRx_total_send));

    if(ret != (ssize_t)(banner_len - session->banner_TxRx_total_send)) {
        if(ret >= 0 || ret == -EAGAIN) {
            
            session->socket_block_directions =
                LIBSSH2_SESSION_BLOCK_OUTBOUND;
            if(ret > 0)
                session->banner_TxRx_total_send += ret;
            return LIBSSH2_ERROR_EAGAIN;
        }
        session->banner_TxRx_state = libssh2_NB_state_idle;
        session->banner_TxRx_total_send = 0;
        return LIBSSH2_ERROR_SOCKET_RECV;
    }

    
    session->banner_TxRx_state = libssh2_NB_state_idle;
    session->banner_TxRx_total_send = 0;

    return 0;
}


static int
session_nonblock(libssh2_socket_t sockfd,   
                 int nonblock  )
{
#ifdef HAVE_O_NONBLOCK
    
    int flags;

    flags = fcntl(sockfd, F_GETFL, 0);
    if(nonblock)
        return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    else
        return fcntl(sockfd, F_SETFL, flags & (~O_NONBLOCK));
#elif defined(HAVE_FIONBIO)
    
    int flags;

    flags = nonblock;
    return ioctl(sockfd, FIONBIO, &flags);
#elif defined(HAVE_IOCTLSOCKET_CASE)
    
    return IoctlSocket(sockfd, FIONBIO, (long) nonblock);
#elif defined(HAVE_SO_NONBLOCK)
    
    long b = nonblock ? 1 : 0;
    return setsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &b, sizeof(b));
#elif defined(_WIN32)
    unsigned long flags;

    flags = nonblock;
    return ioctlsocket(sockfd, FIONBIO, &flags);
#else
    (void)sockfd;
    (void)nonblock;
    return 0;                   
#endif
}


static int
get_socket_nonblocking(libssh2_socket_t sockfd)
{                               
#ifdef HAVE_O_NONBLOCK
    
    int flags = fcntl(sockfd, F_GETFL, 0);

    if(flags == -1) {
        
        return 1;
    }
    return (flags & O_NONBLOCK);
#elif defined(HAVE_SO_NONBLOCK)
    
    long b;
    if(getsockopt(sockfd, SOL_SOCKET, SO_NONBLOCK, &b, sizeof(b))) {
        
        return 1;
    }
    return (int) b;
#elif defined(SO_STATE) && defined(__VMS)
    

    size_t sockstat = 0;
    int    callstat = 0;
    size_t size = sizeof(int);

    callstat = getsockopt(sockfd, SOL_SOCKET, SO_STATE,
                          (char *)&sockstat, &size);
    if(callstat == -1) {
        return 0;
    }
    if((sockstat&SS_NBIO) != 0) {
        return 1;
    }
    return 0;
#elif defined(_WIN32)
    unsigned int option_value;
    socklen_t option_len = sizeof(option_value);

    if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR,
                  (void *) &option_value, &option_len)) {
        
        return 1;
    }
    return (int) option_value;
#else
    (void)sockfd;
    return 1;                   
#endif
}


LIBSSH2_API int
libssh2_session_banner_set(LIBSSH2_SESSION * session, const char *banner)
{
    size_t banner_len = banner ? strlen(banner) : 0;

    if(session->local.banner) {
        LIBSSH2_FREE(session, session->local.banner);
        session->local.banner = NULL;
    }

    if(!banner_len)
        return 0;

    session->local.banner = LIBSSH2_ALLOC(session, banner_len + 3);
    if(!session->local.banner) {
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate memory for local banner");
    }

    memcpy(session->local.banner, banner, banner_len);

    
    session->local.banner[banner_len] = '\0';
    _libssh2_debug((session, LIBSSH2_TRACE_TRANS, "Setting local Banner: %s",
                   session->local.banner));
    session->local.banner[banner_len++] = '\r';
    session->local.banner[banner_len++] = '\n';
    session->local.banner[banner_len] = '\0';

    return 0;
}

#ifndef LIBSSH2_NO_DEPRECATED

LIBSSH2_API int
libssh2_banner_set(LIBSSH2_SESSION * session, const char *banner)
{
    return libssh2_session_banner_set(session, banner);
}
#endif


LIBSSH2_API LIBSSH2_SESSION *
libssh2_session_init_ex(LIBSSH2_ALLOC_FUNC((*my_alloc)),
                        LIBSSH2_FREE_FUNC((*my_free)),
                        LIBSSH2_REALLOC_FUNC((*my_realloc)), void *abstract)
{
    LIBSSH2_ALLOC_FUNC((*local_alloc)) = libssh2_default_alloc;
    LIBSSH2_FREE_FUNC((*local_free)) = libssh2_default_free;
    LIBSSH2_REALLOC_FUNC((*local_realloc)) = libssh2_default_realloc;
    LIBSSH2_SESSION *session;

    if(my_alloc) {
        local_alloc = my_alloc;
    }
    if(my_free) {
        local_free = my_free;
    }
    if(my_realloc) {
        local_realloc = my_realloc;
    }

    session = local_alloc(sizeof(LIBSSH2_SESSION), &abstract);
    if(session) {
        memset(session, 0, sizeof(LIBSSH2_SESSION));
        session->alloc = local_alloc;
        session->free = local_free;
        session->realloc = local_realloc;
        session->send = _libssh2_send;
        session->recv = _libssh2_recv;
        session->abstract = abstract;
        session->api_timeout = 0; 
        session->api_block_mode = 1; 
        session->state = LIBSSH2_STATE_INITIAL_KEX;
        session->fullpacket_required_type = 0;
        session->packet_read_timeout = LIBSSH2_DEFAULT_READ_TIMEOUT;
        session->flag.quote_paths = 1; 
        session->kex = NULL;
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "New session resource allocated"));
        _libssh2_init_if_needed();
    }
    return session;
}


LIBSSH2_API libssh2_cb_generic *
libssh2_session_callback_set2(LIBSSH2_SESSION *session, int cbtype,
                              libssh2_cb_generic *callback)
{
    libssh2_cb_generic *oldcb;

    switch(cbtype) {
    case LIBSSH2_CALLBACK_IGNORE:
        oldcb = (libssh2_cb_generic *)session->ssh_msg_ignore;
        session->ssh_msg_ignore = (LIBSSH2_IGNORE_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_DEBUG:
        oldcb = (libssh2_cb_generic *)session->ssh_msg_debug;
        session->ssh_msg_debug = (LIBSSH2_DEBUG_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_DISCONNECT:
        oldcb = (libssh2_cb_generic *)session->ssh_msg_disconnect;
        session->ssh_msg_disconnect = (LIBSSH2_DISCONNECT_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_MACERROR:
        oldcb = (libssh2_cb_generic *)session->macerror;
        session->macerror = (LIBSSH2_MACERROR_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_X11:
        oldcb = (libssh2_cb_generic *)session->x11;
        session->x11 = (LIBSSH2_X11_OPEN_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_SEND:
        oldcb = (libssh2_cb_generic *)session->send;
        session->send = (LIBSSH2_SEND_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_RECV:
        oldcb = (libssh2_cb_generic *)session->recv;
        session->recv = (LIBSSH2_RECV_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_AUTHAGENT:
        oldcb = (libssh2_cb_generic *)session->authagent;
        session->authagent = (LIBSSH2_AUTHAGENT_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_AUTHAGENT_IDENTITIES:
        oldcb = (libssh2_cb_generic *)session->addLocalIdentities;
        session->addLocalIdentities =
            (LIBSSH2_ADD_IDENTITIES_FUNC((*)))callback;
        return oldcb;

    case LIBSSH2_CALLBACK_AUTHAGENT_SIGN:
        oldcb = (libssh2_cb_generic *)session->agentSignCallback;
        session->agentSignCallback =
            (LIBSSH2_AUTHAGENT_SIGN_FUNC((*)))callback;
        return oldcb;
    }
    _libssh2_debug((session, LIBSSH2_TRACE_TRANS, "Setting Callback %d",
                   cbtype));

    return NULL;
}


#ifdef _MSC_VER
#pragma warning(push)

#pragma warning(disable:4054)

#pragma warning(disable:4055)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
LIBSSH2_API void *
libssh2_session_callback_set(LIBSSH2_SESSION * session,
                             int cbtype, void *callback)
{
    return (void *)libssh2_session_callback_set2(session, cbtype,
                                               (libssh2_cb_generic *)callback);
}
#ifdef _MSC_VER
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif


int _libssh2_wait_socket(LIBSSH2_SESSION *session, time_t start_time)
{
    int rc;
    int seconds_to_next;
    int dir;
    int has_timeout;
    long ms_to_next = 0;
    long elapsed_ms;

    
    session->err_code = LIBSSH2_ERROR_NONE;

    rc = libssh2_keepalive_send(session, &seconds_to_next);
    if(rc)
        return rc;

    ms_to_next = seconds_to_next * 1000;

    
    dir = libssh2_session_block_directions(session);

    if(!dir) {
        _libssh2_debug((session, LIBSSH2_TRACE_SOCKET,
                       "Nothing to wait for in wait_socket"));
        
        ms_to_next = 1000;
    }

    if(session->api_timeout > 0 &&
        (seconds_to_next == 0 ||
         ms_to_next > session->api_timeout)) {
        time_t now = time(NULL);
        elapsed_ms = (long)(1000*difftime(now, start_time));
        if(elapsed_ms > session->api_timeout) {
            return _libssh2_error(session, LIBSSH2_ERROR_TIMEOUT,
                                  "API timeout expired");
        }
        ms_to_next = (session->api_timeout - elapsed_ms);
        has_timeout = 1;
    }
    else if(ms_to_next > 0) {
        has_timeout = 1;
    }
    else
        has_timeout = 0;

#ifdef HAVE_POLL
    {
        struct pollfd sockets[1];

        sockets[0].fd = session->socket_fd;
        sockets[0].events = 0;
        sockets[0].revents = 0;

        if(dir & LIBSSH2_SESSION_BLOCK_INBOUND)
            sockets[0].events |= POLLIN;

        if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
            sockets[0].events |= POLLOUT;

        rc = poll(sockets, 1, has_timeout ? (int)ms_to_next : -1);
    }
#else
    {
        fd_set rfd;
        fd_set wfd;
        fd_set *writefd = NULL;
        fd_set *readfd = NULL;
        struct timeval tv;

        tv.tv_sec = ms_to_next / 1000;
#ifdef libssh2_usec_t
        tv.tv_usec = (libssh2_usec_t)((ms_to_next - tv.tv_sec*1000) * 1000);
#else
        tv.tv_usec = (ms_to_next - tv.tv_sec*1000) * 1000;
#endif

        if(dir & LIBSSH2_SESSION_BLOCK_INBOUND) {
            FD_ZERO(&rfd);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
            FD_SET(session->socket_fd, &rfd);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            readfd = &rfd;
        }

        if(dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
            FD_ZERO(&wfd);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
            FD_SET(session->socket_fd, &wfd);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            writefd = &wfd;
        }

        rc = select((int)(session->socket_fd + 1), readfd, writefd, NULL,
                    has_timeout ? &tv : NULL);
    }
#endif
    if(rc == 0) {
        return _libssh2_error(session, LIBSSH2_ERROR_TIMEOUT,
                              "Timed out waiting on socket");
    }
    if(rc < 0) {
        int err;
#ifdef _WIN32
        err = _libssh2_wsa2errno();
#else
        err = errno;
#endif
        
        if(err == EINTR)
            return 0;

        return _libssh2_error(session, LIBSSH2_ERROR_TIMEOUT,
                              "Error waiting on socket");
    }

    return 0; 
}

static int
session_startup(LIBSSH2_SESSION *session, libssh2_socket_t sock)
{
    int rc;

    if(session->startup_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "session_startup for socket %ld", (long)sock));
        if(LIBSSH2_INVALID_SOCKET == sock) {
            
            return _libssh2_error(session, LIBSSH2_ERROR_BAD_SOCKET,
                                  "Bad socket provided");
        }
        session->socket_fd = sock;

        session->socket_prev_blockstate =
            !get_socket_nonblocking(session->socket_fd);

        if(session->socket_prev_blockstate) {
            
            rc = session_nonblock(session->socket_fd, 1);
            if(rc) {
                return _libssh2_error(session, rc,
                                      "Failed changing socket's "
                                      "blocking state to non-blocking");
            }
        }

        session->startup_state = libssh2_NB_state_created;
    }

    if(session->startup_state == libssh2_NB_state_created) {
        rc = banner_send(session);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
        else if(rc) {
            return _libssh2_error(session, rc,
                                  "Failed sending banner");
        }
        session->startup_state = libssh2_NB_state_sent;
        session->banner_TxRx_state = libssh2_NB_state_idle;
    }

    if(session->startup_state == libssh2_NB_state_sent) {
        do {
            rc = banner_receive(session);
            if(rc == LIBSSH2_ERROR_EAGAIN)
                return rc;
            else if(rc)
                return _libssh2_error(session, rc,
                                      "Failed getting banner");
        } while(strncmp("SSH-", (const char *)session->remote.banner, 4));

        session->startup_state = libssh2_NB_state_sent1;
    }

    if(session->startup_state == libssh2_NB_state_sent1) {
        rc = _libssh2_kex_exchange(session, 0, &session->startup_key_state);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
        else if(rc)
            return _libssh2_error(session, rc,
                                  "Unable to exchange encryption keys");

        session->startup_state = libssh2_NB_state_sent2;
    }

    if(session->startup_state == libssh2_NB_state_sent2) {
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "Requesting userauth service"));

        
        session->startup_service[0] = SSH_MSG_SERVICE_REQUEST;
        _libssh2_htonu32(session->startup_service + 1,
                         sizeof("ssh-userauth") - 1);
        memcpy(session->startup_service + 5, "ssh-userauth",
               sizeof("ssh-userauth") - 1);

        session->startup_state = libssh2_NB_state_sent3;
    }

    if(session->startup_state == libssh2_NB_state_sent3) {
        rc = _libssh2_transport_send(session, session->startup_service,
                                     sizeof("ssh-userauth") + 5 - 1,
                                     NULL, 0);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return rc;
        else if(rc) {
            return _libssh2_error(session, rc,
                                  "Unable to ask for ssh-userauth service");
        }

        session->startup_state = libssh2_NB_state_sent4;
    }

    if(session->startup_state == libssh2_NB_state_sent4) {
        rc = _libssh2_packet_require(session, SSH_MSG_SERVICE_ACCEPT,
                                     &session->startup_data,
                                     &session->startup_data_len, 0, NULL, 0,
                                     &session->startup_req_state);
        if(rc)
            return _libssh2_error(session, rc,
                                  "Failed to get response to "
                                  "ssh-userauth request");

        if(session->startup_data_len < 5) {
            return _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                                  "Unexpected packet length");
        }

        session->startup_service_length =
            _libssh2_ntohu32(session->startup_data + 1);


        if((session->startup_service_length != (sizeof("ssh-userauth") - 1))
            || strncmp("ssh-userauth",
                       (const char *) session->startup_data + 5,
                       session->startup_service_length)) {
            LIBSSH2_FREE(session, session->startup_data);
            session->startup_data = NULL;
            return _libssh2_error(session, LIBSSH2_ERROR_PROTO,
                                  "Invalid response received from server");
        }
        LIBSSH2_FREE(session, session->startup_data);
        session->startup_data = NULL;

        session->startup_state = libssh2_NB_state_idle;

        return 0;
    }

    
    return LIBSSH2_ERROR_INVAL;
}


LIBSSH2_API int
libssh2_session_handshake(LIBSSH2_SESSION *session, libssh2_socket_t sock)
{
    int rc;

    BLOCK_ADJUST(rc, session, session_startup(session, sock));

    return rc;
}

#ifndef LIBSSH2_NO_DEPRECATED

LIBSSH2_API int
libssh2_session_startup(LIBSSH2_SESSION *session, int sock)
{
    return libssh2_session_handshake(session, (libssh2_socket_t) sock);
}
#endif


static int
session_free(LIBSSH2_SESSION *session)
{
    int rc;
    LIBSSH2_PACKET *pkg;
    LIBSSH2_CHANNEL *ch;
    LIBSSH2_LISTENER *l;
    int packets_left = 0;

    if(session->free_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "Freeing session resource %p",
                       (void *)session->remote.banner));

        session->free_state = libssh2_NB_state_created;
    }

    if(session->free_state == libssh2_NB_state_created) {
        
        while((ch = _libssh2_list_first(&session->channels)) != NULL) {
            rc = _libssh2_channel_free(ch);
            if(rc == LIBSSH2_ERROR_EAGAIN)
                return rc;
        }

        session->free_state = libssh2_NB_state_sent;
    }

    if(session->free_state == libssh2_NB_state_sent) {
        
        while((l = _libssh2_list_first(&session->listeners)) != NULL) {
            rc = _libssh2_channel_forward_cancel(l);
            if(rc == LIBSSH2_ERROR_EAGAIN)
                return rc;
        }

        session->free_state = libssh2_NB_state_sent1;
    }

    if(session->kex && session->kex->cleanup) {
        session->kex->cleanup(session,
                              &session->startup_key_state.key_state_low);
    }

    if(session->state & LIBSSH2_STATE_NEWKEYS) {
        
        if(session->hostkey && session->hostkey->dtor) {
            session->hostkey->dtor(session, &session->server_hostkey_abstract);
        }

        
        
        if(session->local.crypt && session->local.crypt->dtor) {
            session->local.crypt->dtor(session,
                                       &session->local.crypt_abstract);
        }
        
        if(session->local.comp && session->local.comp->dtor) {
            session->local.comp->dtor(session, 1,
                                      &session->local.comp_abstract);
        }
        
        if(session->local.mac && session->local.mac->dtor) {
            session->local.mac->dtor(session, &session->local.mac_abstract);
        }

        
        
        if(session->remote.crypt && session->remote.crypt->dtor) {
            session->remote.crypt->dtor(session,
                                        &session->remote.crypt_abstract);
        }
        
        if(session->remote.comp && session->remote.comp->dtor) {
            session->remote.comp->dtor(session, 0,
                                       &session->remote.comp_abstract);
        }
        
        if(session->remote.mac && session->remote.mac->dtor) {
            session->remote.mac->dtor(session, &session->remote.mac_abstract);
        }

        
        if(session->session_id) {
            LIBSSH2_FREE(session, session->session_id);
        }
    }

    
    if(session->remote.banner) {
        LIBSSH2_FREE(session, session->remote.banner);
    }
    if(session->local.banner) {
        LIBSSH2_FREE(session, session->local.banner);
    }

    
    if(session->kex_prefs) {
        LIBSSH2_FREE(session, session->kex_prefs);
    }
    if(session->hostkey_prefs) {
        LIBSSH2_FREE(session, session->hostkey_prefs);
    }

    if(session->local.kexinit) {
        LIBSSH2_FREE(session, session->local.kexinit);
    }
    if(session->local.crypt_prefs) {
        LIBSSH2_FREE(session, session->local.crypt_prefs);
    }
    if(session->local.mac_prefs) {
        LIBSSH2_FREE(session, session->local.mac_prefs);
    }
    if(session->local.comp_prefs) {
        LIBSSH2_FREE(session, session->local.comp_prefs);
    }
    if(session->local.lang_prefs) {
        LIBSSH2_FREE(session, session->local.lang_prefs);
    }

    if(session->remote.kexinit) {
        LIBSSH2_FREE(session, session->remote.kexinit);
    }
    if(session->remote.crypt_prefs) {
        LIBSSH2_FREE(session, session->remote.crypt_prefs);
    }
    if(session->remote.mac_prefs) {
        LIBSSH2_FREE(session, session->remote.mac_prefs);
    }
    if(session->remote.comp_prefs) {
        LIBSSH2_FREE(session, session->remote.comp_prefs);
    }
    if(session->remote.lang_prefs) {
        LIBSSH2_FREE(session, session->remote.lang_prefs);
    }
    if(session->server_sign_algorithms) {
        LIBSSH2_FREE(session, session->server_sign_algorithms);
    }
    if(session->sign_algo_prefs) {
        LIBSSH2_FREE(session, session->sign_algo_prefs);
    }

    
    if(session->kexinit_data) {
        LIBSSH2_FREE(session, session->kexinit_data);
    }
    if(session->startup_data) {
        LIBSSH2_FREE(session, session->startup_data);
    }
    if(session->userauth_list_data) {
        LIBSSH2_FREE(session, session->userauth_list_data);
    }
    if(session->userauth_banner) {
        LIBSSH2_FREE(session, session->userauth_banner);
    }
    if(session->userauth_pswd_data) {
        LIBSSH2_FREE(session, session->userauth_pswd_data);
    }
    if(session->userauth_pswd_newpw) {
        LIBSSH2_FREE(session, session->userauth_pswd_newpw);
    }
    if(session->userauth_host_packet) {
        LIBSSH2_FREE(session, session->userauth_host_packet);
    }
    if(session->userauth_host_method) {
        LIBSSH2_FREE(session, session->userauth_host_method);
    }
    if(session->userauth_host_data) {
        LIBSSH2_FREE(session, session->userauth_host_data);
    }
    if(session->userauth_pblc_data) {
        LIBSSH2_FREE(session, session->userauth_pblc_data);
    }
    if(session->userauth_pblc_packet) {
        LIBSSH2_FREE(session, session->userauth_pblc_packet);
    }
    if(session->userauth_pblc_method) {
        LIBSSH2_FREE(session, session->userauth_pblc_method);
    }
    if(session->userauth_kybd_data) {
        LIBSSH2_FREE(session, session->userauth_kybd_data);
    }
    if(session->userauth_kybd_packet) {
        LIBSSH2_FREE(session, session->userauth_kybd_packet);
    }
    if(session->userauth_kybd_auth_instruction) {
        LIBSSH2_FREE(session, session->userauth_kybd_auth_instruction);
    }
    if(session->open_packet) {
        LIBSSH2_FREE(session, session->open_packet);
    }
    if(session->open_data) {
        LIBSSH2_FREE(session, session->open_data);
    }
    if(session->direct_message) {
        LIBSSH2_FREE(session, session->direct_message);
    }
    if(session->fwdLstn_packet) {
        LIBSSH2_FREE(session, session->fwdLstn_packet);
    }
    if(session->pkeyInit_data) {
        LIBSSH2_FREE(session, session->pkeyInit_data);
    }
    if(session->scpRecv_command) {
        LIBSSH2_FREE(session, session->scpRecv_command);
    }
    if(session->scpSend_command) {
        LIBSSH2_FREE(session, session->scpSend_command);
    }
    if(session->sftpInit_sftp) {
        LIBSSH2_FREE(session, session->sftpInit_sftp);
    }

    
    if(session->packet.total_num) {
        LIBSSH2_FREE(session, session->packet.payload);
    }

    
    
    while((pkg = _libssh2_list_first(&session->packets)) != NULL) {
        packets_left++;
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "packet left with id %d", pkg->data[0]));
        
        _libssh2_list_remove(&pkg->node);

        
        LIBSSH2_FREE(session, pkg->data);
        LIBSSH2_FREE(session, pkg);
    }
    (void)packets_left;
    _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                   "Extra packets left %d", packets_left));

    if(session->socket_prev_blockstate) {
        
        rc = session_nonblock(session->socket_fd, 0);
        if(rc) {
            _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                           "unable to reset socket's blocking state"));
        }
    }

    if(session->server_hostkey) {
        LIBSSH2_FREE(session, session->server_hostkey);
    }

    
    if(session->err_msg &&
       ((session->err_flags & LIBSSH2_ERR_FLAG_DUP) != 0)) {
        LIBSSH2_FREE(session, (char *)session->err_msg);
    }

    LIBSSH2_FREE(session, session);

    return 0;
}


LIBSSH2_API int
libssh2_session_free(LIBSSH2_SESSION * session)
{
    int rc;

    BLOCK_ADJUST(rc, session, session_free(session));

    return rc;
}


static int
session_disconnect(LIBSSH2_SESSION *session, int reason,
                   const char *description,
                   const char *lang)
{
    unsigned char *s;
    size_t descr_len = 0, lang_len = 0;
    int rc;

    if(session->disconnect_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_TRANS,
                       "Disconnecting: reason=%d, desc=%s, lang=%s", reason,
                       description, lang));
        if(description)
            descr_len = strlen(description);

        if(lang)
            lang_len = strlen(lang);

        if(descr_len > 256)
            return _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                                  "too long description");

        if(lang_len > 256)
            return _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                                  "too long language string");

        
        session->disconnect_data_len = descr_len + lang_len + 13;

        s = session->disconnect_data;

        *(s++) = SSH_MSG_DISCONNECT;
        _libssh2_store_u32(&s, reason);
        _libssh2_store_str(&s, description, descr_len);
        
        _libssh2_store_u32(&s, (uint32_t)lang_len);

        session->disconnect_state = libssh2_NB_state_created;
    }

    rc = _libssh2_transport_send(session, session->disconnect_data,
                                 session->disconnect_data_len,
                                 (const unsigned char *)lang, lang_len);
    if(rc == LIBSSH2_ERROR_EAGAIN)
        return rc;

    session->disconnect_state = libssh2_NB_state_idle;

    return 0;
}


LIBSSH2_API int
libssh2_session_disconnect_ex(LIBSSH2_SESSION *session, int reason,
                              const char *desc, const char *lang)
{
    int rc;
    session->state &= ~LIBSSH2_STATE_INITIAL_KEX;
    session->state &= ~LIBSSH2_STATE_EXCHANGING_KEYS;
    BLOCK_ADJUST(rc, session,
                 session_disconnect(session, reason, desc, lang));

    return rc;
}


LIBSSH2_API const char *
libssh2_session_methods(LIBSSH2_SESSION * session, int method_type)
{
    
    const LIBSSH2_KEX_METHOD *method = NULL;

    switch(method_type) {
    case LIBSSH2_METHOD_KEX:
        method = session->kex;
        break;

    case LIBSSH2_METHOD_HOSTKEY:
        method = (LIBSSH2_KEX_METHOD *) session->hostkey;
        break;

    case LIBSSH2_METHOD_CRYPT_CS:
        method = (LIBSSH2_KEX_METHOD *) session->local.crypt;
        break;

    case LIBSSH2_METHOD_CRYPT_SC:
        method = (LIBSSH2_KEX_METHOD *) session->remote.crypt;
        break;

    case LIBSSH2_METHOD_MAC_CS:
        method = (LIBSSH2_KEX_METHOD *) session->local.mac;
        break;

    case LIBSSH2_METHOD_MAC_SC:
        method = (LIBSSH2_KEX_METHOD *) session->remote.mac;
        break;

    case LIBSSH2_METHOD_COMP_CS:
        method = (LIBSSH2_KEX_METHOD *) session->local.comp;
        break;

    case LIBSSH2_METHOD_COMP_SC:
        method = (LIBSSH2_KEX_METHOD *) session->remote.comp;
        break;

    case LIBSSH2_METHOD_LANG_CS:
        return "";

    case LIBSSH2_METHOD_LANG_SC:
        return "";

    default:
        _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                       "Invalid parameter specified for method_type");
        return NULL;
    }

    if(!method) {
        _libssh2_error(session, LIBSSH2_ERROR_METHOD_NONE,
                       "No method negotiated");
        return NULL;
    }

    return method->name;
}


LIBSSH2_API void **
libssh2_session_abstract(LIBSSH2_SESSION * session)
{
    return &session->abstract;
}


LIBSSH2_API int
libssh2_session_last_error(LIBSSH2_SESSION * session, char **errmsg,
                           int *errmsg_len, int want_buf)
{
    size_t msglen = 0;

    
    if(!session->err_code) {
        if(errmsg) {
            if(want_buf) {
                *errmsg = LIBSSH2_ALLOC(session, 1);
                if(*errmsg) {
                    **errmsg = 0;
                }
            }
            else {
                *errmsg = (char *) "";
            }
        }
        if(errmsg_len) {
            *errmsg_len = 0;
        }
        return 0;
    }

    if(errmsg) {
        const char *error = session->err_msg ? session->err_msg : "";

        msglen = strlen(error);

        if(want_buf) {
            
            *errmsg = LIBSSH2_ALLOC(session, msglen + 1);
            if(*errmsg) {
                memcpy(*errmsg, error, msglen);
                (*errmsg)[msglen] = 0;
            }
        }
        else
            *errmsg = (char *)error;
    }

    if(errmsg_len) {
        *errmsg_len = (int)msglen;
    }

    return session->err_code;
}


LIBSSH2_API int
libssh2_session_last_errno(LIBSSH2_SESSION * session)
{
    return session->err_code;
}


LIBSSH2_API int
libssh2_session_set_last_error(LIBSSH2_SESSION* session,
                               int errcode,
                               const char *errmsg)
{
    return _libssh2_error_flags(session, errcode, errmsg,
                                LIBSSH2_ERR_FLAG_DUP);
}


LIBSSH2_API int
libssh2_session_flag(LIBSSH2_SESSION * session, int flag, int value)
{
    switch(flag) {
    case LIBSSH2_FLAG_SIGPIPE:
        session->flag.sigpipe = value;
        break;
    case LIBSSH2_FLAG_COMPRESS:
        session->flag.compress = value;
        break;
    case LIBSSH2_FLAG_QUOTE_PATHS:
        session->flag.quote_paths = value;
        break;
    default:
        
        return LIBSSH2_ERROR_INVAL;
    }

    return LIBSSH2_ERROR_NONE;
}


int
_libssh2_session_set_blocking(LIBSSH2_SESSION *session, int blocking)
{
    int bl = session->api_block_mode;
    _libssh2_debug((session, LIBSSH2_TRACE_CONN,
                   "Setting blocking mode %s", blocking ? "ON" : "OFF"));
    session->api_block_mode = blocking;

    return bl;
}


LIBSSH2_API void
libssh2_session_set_blocking(LIBSSH2_SESSION * session, int blocking)
{
    (void)_libssh2_session_set_blocking(session, blocking);
}


LIBSSH2_API int
libssh2_session_get_blocking(LIBSSH2_SESSION * session)
{
    return session->api_block_mode;
}



LIBSSH2_API void
libssh2_session_set_timeout(LIBSSH2_SESSION * session, long timeout)
{
    session->api_timeout = timeout;
}


LIBSSH2_API long
libssh2_session_get_timeout(LIBSSH2_SESSION * session)
{
    return session->api_timeout;
}


LIBSSH2_API void
libssh2_session_set_read_timeout(LIBSSH2_SESSION * session, long timeout)
{
    if(timeout <= 0) {
        timeout = LIBSSH2_DEFAULT_READ_TIMEOUT;
    }
    session->packet_read_timeout = timeout;
}


LIBSSH2_API long
libssh2_session_get_read_timeout(LIBSSH2_SESSION * session)
{
    return session->packet_read_timeout;
}


LIBSSH2_API int
libssh2_poll_channel_read(LIBSSH2_CHANNEL *channel, int extended)
{
    LIBSSH2_SESSION *session;
    LIBSSH2_PACKET *packet;

    if(!channel)
        return LIBSSH2_ERROR_BAD_USE;

    session = channel->session;
    packet = _libssh2_list_first(&session->packets);

    while(packet) {
        if(packet->data_len < 5) {
            return _libssh2_error(session, LIBSSH2_ERROR_BUFFER_TOO_SMALL,
                                  "Packet too small");
        }

        if(channel->local.id == _libssh2_ntohu32(packet->data + 1)) {
            if(extended == 1 &&
                (packet->data[0] == SSH_MSG_CHANNEL_EXTENDED_DATA
                 || packet->data[0] == SSH_MSG_CHANNEL_DATA)) {
                return 1;
            }
            else if(extended == 0 &&
                    packet->data[0] == SSH_MSG_CHANNEL_DATA) {
                return 1;
            }
            
        }
        packet = _libssh2_list_next(&packet->node);
    }

    return 0;
}


static inline int
poll_channel_write(LIBSSH2_CHANNEL * channel)
{
    return channel->local.window_size ? 1 : 0;
}


static inline int
poll_listener_queued(LIBSSH2_LISTENER * listener)
{
    return _libssh2_list_first(&listener->queue) ? 1 : 0;
}


LIBSSH2_API int
libssh2_poll(LIBSSH2_POLLFD * fds, unsigned int nfds, long timeout)
{
    long timeout_remaining;
    unsigned int i, active_fds;
#ifdef HAVE_POLL
    LIBSSH2_SESSION *session = NULL;
#ifdef HAVE_ALLOCA
    struct pollfd *sockets = alloca(sizeof(struct pollfd) * nfds);
#else
    struct pollfd sockets[256];

    if(nfds > 256)
        
        return -1;
#endif
    
    for(i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        switch(fds[i].type) {
        case LIBSSH2_POLLFD_SOCKET:
            sockets[i].fd = fds[i].fd.socket;
            sockets[i].events = (short)fds[i].events;
            sockets[i].revents = 0;
            break;

        case LIBSSH2_POLLFD_CHANNEL:
            sockets[i].fd = fds[i].fd.channel->session->socket_fd;
            sockets[i].events = POLLIN;
            sockets[i].revents = 0;
            if(!session)
                session = fds[i].fd.channel->session;
            break;

        case LIBSSH2_POLLFD_LISTENER:
            sockets[i].fd = fds[i].fd.listener->session->socket_fd;
            sockets[i].events = POLLIN;
            sockets[i].revents = 0;
            if(!session)
                session = fds[i].fd.listener->session;
            break;

        default:
            if(session)
                _libssh2_error(session, LIBSSH2_ERROR_INVALID_POLL_TYPE,
                               "Invalid descriptor passed to libssh2_poll()");
            return -1;
        }
    }
#elif defined(HAVE_SELECT)
    LIBSSH2_SESSION *session = NULL;
    libssh2_socket_t maxfd = 0;
    fd_set rfds, wfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    for(i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        switch(fds[i].type) {
        case LIBSSH2_POLLFD_SOCKET:
            if(fds[i].events & LIBSSH2_POLLFD_POLLIN) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
                FD_SET(fds[i].fd.socket, &rfds);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
                if(fds[i].fd.socket > maxfd)
                    maxfd = fds[i].fd.socket;
            }
            if(fds[i].events & LIBSSH2_POLLFD_POLLOUT) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
                FD_SET(fds[i].fd.socket, &wfds);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
                if(fds[i].fd.socket > maxfd)
                    maxfd = fds[i].fd.socket;
            }
            break;

        case LIBSSH2_POLLFD_CHANNEL:
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
            FD_SET(fds[i].fd.channel->session->socket_fd, &rfds);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            if(fds[i].fd.channel->session->socket_fd > maxfd)
                maxfd = fds[i].fd.channel->session->socket_fd;
            if(!session)
                session = fds[i].fd.channel->session;
            break;

        case LIBSSH2_POLLFD_LISTENER:
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
            FD_SET(fds[i].fd.listener->session->socket_fd, &rfds);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            if(fds[i].fd.listener->session->socket_fd > maxfd)
                maxfd = fds[i].fd.listener->session->socket_fd;
            if(!session)
                session = fds[i].fd.listener->session;
            break;

        default:
            if(session)
                _libssh2_error(session, LIBSSH2_ERROR_INVALID_POLL_TYPE,
                               "Invalid descriptor passed to libssh2_poll()");
            return -1;
        }
    }
#else
    

    timeout = 0;
#endif 

    timeout_remaining = timeout;
    do {
#if defined(HAVE_POLL) || defined(HAVE_SELECT)
        int sysret;
#endif

        active_fds = 0;

        for(i = 0; i < nfds; i++) {
            if(fds[i].events != fds[i].revents) {
                switch(fds[i].type) {
                case LIBSSH2_POLLFD_CHANNEL:
                    if((fds[i].events & LIBSSH2_POLLFD_POLLIN) &&
                        
                        ((fds[i].revents & LIBSSH2_POLLFD_POLLIN) == 0)) {
                        
                        fds[i].revents |=
                            libssh2_poll_channel_read(fds[i].fd.channel,
                                                      0) ?
                            LIBSSH2_POLLFD_POLLIN : 0;
                    }
                    if((fds[i].events & LIBSSH2_POLLFD_POLLEXT) &&
                        
                        ((fds[i].revents & LIBSSH2_POLLFD_POLLEXT) == 0)) {
                        
                        fds[i].revents |=
                            libssh2_poll_channel_read(fds[i].fd.channel,
                                                      1) ?
                            LIBSSH2_POLLFD_POLLEXT : 0;
                    }
                    if((fds[i].events & LIBSSH2_POLLFD_POLLOUT) &&
                        
                        ((fds[i].revents & LIBSSH2_POLLFD_POLLOUT) == 0)) {
                        
                        fds[i].revents |=
                            poll_channel_write(fds[i].fd. channel) ?
                            LIBSSH2_POLLFD_POLLOUT : 0;
                    }
                    if(fds[i].fd.channel->remote.close
                        || fds[i].fd.channel->local.close) {
                        fds[i].revents |= LIBSSH2_POLLFD_CHANNEL_CLOSED;
                    }
                    if(fds[i].fd.channel->session->socket_state ==
                        LIBSSH2_SOCKET_DISCONNECTED) {
                        fds[i].revents |=
                            LIBSSH2_POLLFD_CHANNEL_CLOSED |
                            LIBSSH2_POLLFD_SESSION_CLOSED;
                    }
                    break;

                case LIBSSH2_POLLFD_LISTENER:
                    if((fds[i].events & LIBSSH2_POLLFD_POLLIN) &&
                        
                        ((fds[i].revents & LIBSSH2_POLLFD_POLLIN) == 0)) {
                        
                        fds[i].revents |=
                            poll_listener_queued(fds[i].fd. listener) ?
                            LIBSSH2_POLLFD_POLLIN : 0;
                    }
                    if(fds[i].fd.listener->session->socket_state ==
                        LIBSSH2_SOCKET_DISCONNECTED) {
                        fds[i].revents |=
                            LIBSSH2_POLLFD_LISTENER_CLOSED |
                            LIBSSH2_POLLFD_SESSION_CLOSED;
                    }
                    break;
                }
            }
            if(fds[i].revents) {
                active_fds++;
            }
        }

        if(active_fds) {
            
            timeout_remaining = 0;
        }
#ifdef HAVE_POLL

        {
            struct timeval tv_begin, tv_end;

            gettimeofday(&tv_begin, NULL);
            sysret = poll(sockets, nfds, (int)timeout_remaining);
            gettimeofday(&tv_end, NULL);
            timeout_remaining -= (tv_end.tv_sec - tv_begin.tv_sec) * 1000;
            timeout_remaining -= (tv_end.tv_usec - tv_begin.tv_usec) / 1000;
        }

        if(sysret > 0) {
            for(i = 0; i < nfds; i++) {
                switch(fds[i].type) {
                case LIBSSH2_POLLFD_SOCKET:
                    fds[i].revents = sockets[i].revents;
                    sockets[i].revents = 0; 
                    if(fds[i].revents) {
                        active_fds++;
                    }
                    break;
                case LIBSSH2_POLLFD_CHANNEL:
                    if(sockets[i].events & POLLIN) {
                        
                        while(_libssh2_transport_read(fds[i].fd.
                                                      channel->session)
                              > 0);
                    }
                    if(sockets[i].revents & POLLHUP) {
                        fds[i].revents |=
                            LIBSSH2_POLLFD_CHANNEL_CLOSED |
                            LIBSSH2_POLLFD_SESSION_CLOSED;
                    }
                    sockets[i].revents = 0;
                    break;
                case LIBSSH2_POLLFD_LISTENER:
                    if(sockets[i].events & POLLIN) {
                        
                        while(_libssh2_transport_read(fds[i].fd.
                                                      listener->session)
                              > 0);
                    }
                    if(sockets[i].revents & POLLHUP) {
                        fds[i].revents |=
                            LIBSSH2_POLLFD_LISTENER_CLOSED |
                            LIBSSH2_POLLFD_SESSION_CLOSED;
                    }
                    sockets[i].revents = 0;
                    break;
                }
            }
        }
#elif defined(HAVE_SELECT)
        tv.tv_sec = timeout_remaining / 1000;
#ifdef libssh2_usec_t
        tv.tv_usec = (libssh2_usec_t)((timeout_remaining % 1000) * 1000);
#else
        tv.tv_usec = (timeout_remaining % 1000) * 1000;
#endif

        {
            struct timeval tv_begin, tv_end;

            gettimeofday(&tv_begin, NULL);
            sysret = select((int)(maxfd + 1), &rfds, &wfds, NULL, &tv);
            gettimeofday(&tv_end, NULL);

            timeout_remaining -= (tv_end.tv_sec - tv_begin.tv_sec) * 1000;
            timeout_remaining -= (tv_end.tv_usec - tv_begin.tv_usec) / 1000;
        }

        if(sysret > 0) {
            for(i = 0; i < nfds; i++) {
                switch(fds[i].type) {
                case LIBSSH2_POLLFD_SOCKET:
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
                    if(FD_ISSET(fds[i].fd.socket, &rfds)) {
                        fds[i].revents |= LIBSSH2_POLLFD_POLLIN;
                    }
                    if(FD_ISSET(fds[i].fd.socket, &wfds)) {
                        fds[i].revents |= LIBSSH2_POLLFD_POLLOUT;
                    }
                    if(fds[i].revents) {
                        active_fds++;
                    }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
                    break;

                case LIBSSH2_POLLFD_CHANNEL:
                    if(FD_ISSET(fds[i].fd.channel->session->socket_fd,
                                &rfds)) {
                        
                        while(_libssh2_transport_read(fds[i].fd.
                                                      channel->session)
                              > 0);
                    }
                    break;

                case LIBSSH2_POLLFD_LISTENER:
                    if(FD_ISSET
                        (fds[i].fd.listener->session->socket_fd, &rfds)) {
                        
                        while(_libssh2_transport_read(fds[i].fd.
                                                      listener->session)
                              > 0);
                    }
                    break;
                }
            }
        }
#endif 
    } while((timeout_remaining > 0) && !active_fds);

    return active_fds;
}


LIBSSH2_API int
libssh2_session_block_directions(LIBSSH2_SESSION *session)
{
    return session->socket_block_directions;
}



LIBSSH2_API const char *
libssh2_session_banner_get(LIBSSH2_SESSION *session)
{
    
    if(!session)
        return NULL;

    if(!session->remote.banner)
        return NULL;

    return (const char *) session->remote.banner;
}
