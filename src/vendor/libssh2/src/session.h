#ifndef LIBSSH2_SESSION_H
#define LIBSSH2_SESSION_H



#define BLOCK_ADJUST(rc, sess, x) \
    do { \
        time_t entry_time = time(NULL); \
        do { \
            rc = x; \
             \
            if((rc != LIBSSH2_ERROR_EAGAIN) || !sess->api_block_mode) \
                break; \
            rc = _libssh2_wait_socket(sess, entry_time);  \
        } while(!rc);   \
    } while(0)


#define BLOCK_ADJUST_ERRNO(ptr, sess, x) \
    do { \
        time_t entry_time = time(NULL); \
        int rc; \
        do { \
            ptr = x; \
            if(!sess->api_block_mode || \
               (ptr != NULL) || \
               (libssh2_session_last_errno(sess) != LIBSSH2_ERROR_EAGAIN)) \
                break; \
            rc = _libssh2_wait_socket(sess, entry_time); \
        } while(!rc); \
    } while(0)


int _libssh2_wait_socket(LIBSSH2_SESSION *session, time_t entry_time);


int _libssh2_session_set_blocking(LIBSSH2_SESSION * session, int blocking);

#endif 
