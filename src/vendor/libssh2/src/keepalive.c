

#include "libssh2_priv.h"
#include "transport.h" 



LIBSSH2_API void
libssh2_keepalive_config(LIBSSH2_SESSION *session,
                         int want_reply,
                         unsigned int interval)
{
    if(interval == 1)
        session->keepalive_interval = 2;
    else
        session->keepalive_interval = interval;
    session->keepalive_want_reply = want_reply ? 1 : 0;
}

LIBSSH2_API int
libssh2_keepalive_send(LIBSSH2_SESSION *session,
                       int *seconds_to_next)
{
    time_t now;

    if(!session->keepalive_interval) {
        if(seconds_to_next)
            *seconds_to_next = 0;
        return 0;
    }

    now = time(NULL);

    if(session->keepalive_last_sent + session->keepalive_interval <= now) {
        
        unsigned char keepalive_data[]
            = "\x50\x00\x00\x00\x15keepalive@libssh2.orgW";
        size_t len = sizeof(keepalive_data) - 1;
        int rc;

        keepalive_data[len - 1] =
            (unsigned char)session->keepalive_want_reply;

        rc = _libssh2_transport_send(session, keepalive_data, len, NULL, 0);
        
        if(rc && rc != LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                           "Unable to send keepalive message");
            return rc;
        }

        session->keepalive_last_sent = now;
        if(seconds_to_next)
            *seconds_to_next = session->keepalive_interval;
    }
    else if(seconds_to_next) {
        *seconds_to_next = (int) (session->keepalive_last_sent - now)
            + session->keepalive_interval;
    }

    return 0;
}
