

#include "libssh2_priv.h"
#include "libssh2_sftp.h"

#include "channel.h"
#include "session.h"
#include "sftp.h"

#include <assert.h>
#include <stdlib.h>  




#define SSH_FXP_INIT                            1
#define SSH_FXP_VERSION                         2
#define SSH_FXP_OPEN                            3
#define SSH_FXP_CLOSE                           4
#define SSH_FXP_READ                            5
#define SSH_FXP_WRITE                           6
#define SSH_FXP_LSTAT                           7
#define SSH_FXP_FSTAT                           8
#define SSH_FXP_SETSTAT                         9
#define SSH_FXP_FSETSTAT                        10
#define SSH_FXP_OPENDIR                         11
#define SSH_FXP_READDIR                         12
#define SSH_FXP_REMOVE                          13
#define SSH_FXP_MKDIR                           14
#define SSH_FXP_RMDIR                           15
#define SSH_FXP_REALPATH                        16
#define SSH_FXP_STAT                            17
#define SSH_FXP_RENAME                          18
#define SSH_FXP_READLINK                        19
#define SSH_FXP_SYMLINK                         20
#define SSH_FXP_STATUS                          101
#define SSH_FXP_HANDLE                          102
#define SSH_FXP_DATA                            103
#define SSH_FXP_NAME                            104
#define SSH_FXP_ATTRS                           105
#define SSH_FXP_EXTENDED                        200
#define SSH_FXP_EXTENDED_REPLY                  201


#define LIBSSH2_SFTP_ATTR_PFILETYPE_FILE        0100000

#define LIBSSH2_SFTP_ATTR_PFILETYPE_DIR         0040000

#define SSH_FXE_STATVFS_ST_RDONLY               0x00000001
#define SSH_FXE_STATVFS_ST_NOSUID               0x00000002


#define LIBSSH2_SFTP_PACKET_MAXLEN  (256 * 1024)

static int sftp_packet_ask(LIBSSH2_SFTP *sftp, unsigned char packet_type,
                           uint32_t request_id, unsigned char **data,
                           size_t *data_len);
static void sftp_packet_flush(LIBSSH2_SFTP *sftp);


static struct sftp_zombie_requests *
find_zombie_request(LIBSSH2_SFTP *sftp, uint32_t request_id)
{
    struct sftp_zombie_requests *zombie =
        _libssh2_list_first(&sftp->zombie_requests);

    while(zombie) {
        if(zombie->request_id == request_id)
            break;
        else
            zombie = _libssh2_list_next(&zombie->node);
    }

    return zombie;
}

static void
remove_zombie_request(LIBSSH2_SFTP *sftp, uint32_t request_id)
{
    LIBSSH2_SESSION *session = sftp->channel->session;

    struct sftp_zombie_requests *zombie = find_zombie_request(sftp,
                                                              request_id);
    if(zombie) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Removing request ID %u from the list of "
                       "zombie requests",
                       request_id));

        _libssh2_list_remove(&zombie->node);
        LIBSSH2_FREE(session, zombie);
    }
}

static int
add_zombie_request(LIBSSH2_SFTP *sftp, uint32_t request_id)
{
    LIBSSH2_SESSION *session = sftp->channel->session;

    struct sftp_zombie_requests *zombie;

    _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                   "Marking request ID %u as a zombie request", request_id));

    zombie = LIBSSH2_ALLOC(sftp->channel->session,
                           sizeof(struct sftp_zombie_requests));
    if(!zombie)
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "malloc fail for zombie request ID");
    else {
        zombie->request_id = request_id;
        _libssh2_list_add(&sftp->zombie_requests, &zombie->node);
        return LIBSSH2_ERROR_NONE;
    }
}


static int
sftp_packet_add(LIBSSH2_SFTP *sftp, unsigned char *data,
                size_t data_len)
{
    LIBSSH2_SESSION *session = sftp->channel->session;
    LIBSSH2_SFTP_PACKET *packet;
    uint32_t request_id;

    if(data_len < 5) {
        return LIBSSH2_ERROR_OUT_OF_BOUNDARY;
    }

    _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                   "Received packet type %u (len %lu)",
                   (unsigned int)data[0], (unsigned long)data_len));

    

    switch(data[0]) {
    case SSH_FXP_INIT:
    case SSH_FXP_VERSION:
    case SSH_FXP_OPEN:
    case SSH_FXP_CLOSE:
    case SSH_FXP_READ:
    case SSH_FXP_WRITE:
    case SSH_FXP_LSTAT:
    case SSH_FXP_FSTAT:
    case SSH_FXP_SETSTAT:
    case SSH_FXP_FSETSTAT:
    case SSH_FXP_OPENDIR:
    case SSH_FXP_READDIR:
    case SSH_FXP_REMOVE:
    case SSH_FXP_MKDIR:
    case SSH_FXP_RMDIR:
    case SSH_FXP_REALPATH:
    case SSH_FXP_STAT:
    case SSH_FXP_RENAME:
    case SSH_FXP_READLINK:
    case SSH_FXP_SYMLINK:
    case SSH_FXP_STATUS:
    case SSH_FXP_HANDLE:
    case SSH_FXP_DATA:
    case SSH_FXP_NAME:
    case SSH_FXP_ATTRS:
    case SSH_FXP_EXTENDED:
    case SSH_FXP_EXTENDED_REPLY:
        break;
    default:
        sftp->last_errno = LIBSSH2_FX_OK;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "Out of sync with the world");
    }

    request_id = _libssh2_ntohu32(&data[1]);

    _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Received packet id %d",
                   request_id));

    
    if((data[0] == SSH_FXP_STATUS || data[0] == SSH_FXP_DATA)
       && find_zombie_request(sftp, request_id)) {

        

        LIBSSH2_FREE(session, data);

        remove_zombie_request(sftp, request_id);
        return LIBSSH2_ERROR_NONE;
    }

    packet = LIBSSH2_ALLOC(session, sizeof(LIBSSH2_SFTP_PACKET));
    if(!packet) {
        return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                              "Unable to allocate datablock for SFTP packet");
    }

    packet->data = data;
    packet->data_len = data_len;
    packet->request_id = request_id;

    _libssh2_list_add(&sftp->packets, &packet->node);

    return LIBSSH2_ERROR_NONE;
}


static int
sftp_packet_read(LIBSSH2_SFTP *sftp)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    unsigned char *packet = NULL;
    ssize_t rc;
    unsigned long recv_window;
    int packet_type;
    uint32_t request_id;

    _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "recv packet"));

    switch(sftp->packet_state) {
    case libssh2_NB_state_sent: 
        sftp->packet_state = libssh2_NB_state_idle;

        packet = sftp->partial_packet;
        goto window_adjust;

    case libssh2_NB_state_sent1: 
        sftp->packet_state = libssh2_NB_state_idle;

        packet = sftp->partial_packet;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "partial read cont, len: %u", sftp->partial_len));
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "partial read cont, already recvd: %lu",
                       (unsigned long)sftp->partial_received));
        LIBSSH2_FALLTHROUGH();
    default:
        if(!packet) {
            

            
            rc = _libssh2_channel_read(channel, 0,
                                       (char *)&sftp->packet_header[
                                           sftp->packet_header_len],
                                       sizeof(sftp->packet_header) -
                                       sftp->packet_header_len);
            if(rc == LIBSSH2_ERROR_EAGAIN)
                return (int)rc;
            else if(rc < 0)
                return _libssh2_error(session, (int)rc, "channel read");

            sftp->packet_header_len += rc;

            if(sftp->packet_header_len != sizeof(sftp->packet_header))
                
                return LIBSSH2_ERROR_EAGAIN;

            
            sftp->partial_len = _libssh2_ntohu32(sftp->packet_header);
            packet_type = sftp->packet_header[4];
            request_id = _libssh2_ntohu32(sftp->packet_header + 5);

            
            if(sftp->partial_len > LIBSSH2_SFTP_PACKET_MAXLEN &&
               
               !(sftp->readdir_state != libssh2_NB_state_idle &&
                 sftp->readdir_request_id == request_id &&
                 packet_type == SSH_FXP_NAME)) {
                libssh2_channel_flush(channel);
                sftp->packet_header_len = 0;
                return _libssh2_error(session,
                                      LIBSSH2_ERROR_CHANNEL_PACKET_EXCEEDED,
                                      "SFTP packet too large");
            }

            if(sftp->partial_len < 5)
                return _libssh2_error(session,
                                      LIBSSH2_ERROR_ALLOC,
                                      "Invalid SFTP packet size");

            _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                           "Data begin - Packet Length: %lu",
                           (unsigned long)sftp->partial_len));
            packet = LIBSSH2_ALLOC(session, sftp->partial_len);
            if(!packet)
                return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                      "Unable to allocate SFTP packet");
            sftp->packet_header_len = 0;
            sftp->partial_packet = packet;
            
            sftp->partial_received = 5;
            memcpy(packet, sftp->packet_header + 4, 5);

window_adjust:
            recv_window = libssh2_channel_window_read_ex(channel, NULL, NULL);

            if(sftp->partial_len > recv_window) {
                
                rc = _libssh2_channel_receive_window_adjust(channel,
                                                            sftp->partial_len
                                                            * 2,
                                                            1, NULL);
                
                sftp->packet_state = (rc == LIBSSH2_ERROR_EAGAIN) ?
                    libssh2_NB_state_sent :
                    libssh2_NB_state_idle;

                if(rc == LIBSSH2_ERROR_EAGAIN)
                    return (int)rc;
            }
        }

        
        while(sftp->partial_len > sftp->partial_received) {
            rc = _libssh2_channel_read(channel, 0,
                                       (char *)&packet[sftp->partial_received],
                                       sftp->partial_len -
                                       sftp->partial_received);

            if(rc == LIBSSH2_ERROR_EAGAIN) {
                
                sftp->packet_state = libssh2_NB_state_sent1;
                return (int)rc;
            }
            else if(rc < 0) {
                LIBSSH2_FREE(session, packet);
                sftp->partial_packet = NULL;
                return _libssh2_error(session, (int)rc,
                                      "Error waiting for SFTP packet");
            }
            sftp->partial_received += rc;
        }

        sftp->partial_packet = NULL;

        
        packet_type = packet[0];
        rc = sftp_packet_add(sftp, packet, sftp->partial_len);
        if(rc) {
            LIBSSH2_FREE(session, packet);
            return (int)rc;
        }
        else {
            return packet_type;
        }
    }
    
}


static void sftp_packetlist_flush(LIBSSH2_SFTP_HANDLE *handle)
{
    struct sftp_pipeline_chunk *chunk;
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_SESSION *session = sftp->channel->session;

    
    chunk = _libssh2_list_first(&handle->packet_list);
    while(chunk) {
        unsigned char *data;
        size_t data_len;
        int rc;
        struct sftp_pipeline_chunk *next = _libssh2_list_next(&chunk->node);

        rc = sftp_packet_ask(sftp, SSH_FXP_STATUS,
                             chunk->request_id, &data, &data_len);
        if(rc)
            rc = sftp_packet_ask(sftp, SSH_FXP_DATA,
                                 chunk->request_id, &data, &data_len);

        if(!rc)
            
            LIBSSH2_FREE(session, data);
        else if(chunk->sent)
            
            add_zombie_request(sftp, chunk->request_id);

        _libssh2_list_remove(&chunk->node);
        LIBSSH2_FREE(session, chunk);
        chunk = next;
    }
}



static int
sftp_packet_ask(LIBSSH2_SFTP *sftp, unsigned char packet_type,
                uint32_t request_id, unsigned char **data,
                size_t *data_len)
{
    LIBSSH2_SESSION *session = sftp->channel->session;
    LIBSSH2_SFTP_PACKET *packet = _libssh2_list_first(&sftp->packets);

    if(!packet)
        return -1;

    

    while(packet) {
        if((packet->data[0] == packet_type) &&
           ((packet_type == SSH_FXP_VERSION) ||
            (packet->request_id == request_id))) {

            
            *data = packet->data;
            *data_len = packet->data_len;

            
            _libssh2_list_remove(&packet->node);
            LIBSSH2_FREE(session, packet);

            return 0;
        }
        
        packet = _libssh2_list_next(&packet->node);
    }
    return -1;
}


static int
sftp_packet_require(LIBSSH2_SFTP *sftp, unsigned char packet_type,
                    uint32_t request_id, unsigned char **data,
                    size_t *data_len, size_t required_size)
{
    LIBSSH2_SESSION *session = sftp->channel->session;
    int rc;

    if(!data || !data_len || required_size == 0) {
        return LIBSSH2_ERROR_BAD_USE;
    }

    _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Requiring packet %u id %u",
                   (unsigned int) packet_type, request_id));

    if(sftp_packet_ask(sftp, packet_type, request_id, data, data_len) == 0) {
        
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Got %u",
                       (unsigned int) packet_type));

        if(*data_len < required_size) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }

        return LIBSSH2_ERROR_NONE;
    }

    while(session->socket_state == LIBSSH2_SOCKET_CONNECTED) {
        rc = sftp_packet_read(sftp);
        if(rc < 0)
            return rc;

        
        if(!sftp_packet_ask(sftp, packet_type, request_id, data, data_len)) {
            
            _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Got %d",
                           (unsigned int) packet_type));

            if(*data_len < required_size) {
                return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
            }

            return LIBSSH2_ERROR_NONE;
        }
    }

    
    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
}


static int
sftp_packet_requirev(LIBSSH2_SFTP *sftp, int num_valid_responses,
                     const unsigned char *valid_responses,
                     uint32_t request_id, unsigned char **data,
                     size_t *data_len, size_t required_size)
{
    int i;
    int rc;

    if(!data || !data_len || required_size == 0) {
        return LIBSSH2_ERROR_BAD_USE;
    }

    
    if(sftp->requirev_start == 0)
        sftp->requirev_start = time(NULL);

    while(sftp->channel->session->socket_state == LIBSSH2_SOCKET_CONNECTED) {
        for(i = 0; i < num_valid_responses; i++) {
            if(sftp_packet_ask(sftp, valid_responses[i], request_id,
                               data, data_len) == 0) {
                
                sftp->requirev_start = 0;

                if(*data_len < required_size) {
                    return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                }

                return LIBSSH2_ERROR_NONE;
            }
        }

        rc = sftp_packet_read(sftp);
        if((rc < 0) && (rc != LIBSSH2_ERROR_EAGAIN)) {
            sftp->requirev_start = 0;
            return rc;
        }
        else if(rc <= 0) {
            
            long left =
                sftp->channel->session->packet_read_timeout -
                (long)(time(NULL) - sftp->requirev_start);

            if(left <= 0) {
                sftp->requirev_start = 0;
                return LIBSSH2_ERROR_TIMEOUT;
            }
            else if(rc == LIBSSH2_ERROR_EAGAIN) {
                return rc;
            }
        }
    }

    sftp->requirev_start = 0;

    
    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
}


static int sftp_attrsize(unsigned long flags)
{
    return 4 +                                 
           ((flags & LIBSSH2_SFTP_ATTR_SIZE) ? 8 : 0) +
           ((flags & LIBSSH2_SFTP_ATTR_UIDGID) ? 8 : 0) +
           ((flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? 4 : 0) +
           ((flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? 8 : 0);
    
}


static ssize_t
sftp_attr2bin(unsigned char *p, const LIBSSH2_SFTP_ATTRIBUTES * attrs)
{
    unsigned char *s = p;
    uint32_t flag_mask =
        LIBSSH2_SFTP_ATTR_SIZE |
        LIBSSH2_SFTP_ATTR_UIDGID |
        LIBSSH2_SFTP_ATTR_PERMISSIONS |
        LIBSSH2_SFTP_ATTR_ACMODTIME;

    

    if(!attrs) {
        _libssh2_htonu32(s, 0);
        return 4;
    }

    _libssh2_store_u32(&s, (uint32_t)(attrs->flags & flag_mask));

    if(attrs->flags & LIBSSH2_SFTP_ATTR_SIZE) {
        _libssh2_store_u64(&s, attrs->filesize);
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        _libssh2_store_u32(&s, (uint32_t)attrs->uid);
        _libssh2_store_u32(&s, (uint32_t)attrs->gid);
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        _libssh2_store_u32(&s, (uint32_t)attrs->permissions);
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        _libssh2_store_u32(&s, (uint32_t)attrs->atime);
        _libssh2_store_u32(&s, (uint32_t)attrs->mtime);
    }

    return s - p;
}


static ssize_t
sftp_bin2attr(LIBSSH2_SFTP_ATTRIBUTES *attrs, const unsigned char *p,
              size_t data_len)
{
    struct string_buf buf;
    uint32_t flags = 0;
    buf.data = (unsigned char *)p;
    buf.dataptr = buf.data;
    buf.len = data_len;

    if(_libssh2_get_u32(&buf, &flags)) {
        return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
    }
    attrs->flags = flags;

    if(attrs->flags & LIBSSH2_SFTP_ATTR_SIZE) {
        if(_libssh2_get_u64(&buf, &(attrs->filesize))) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        uint32_t uid = 0;
        uint32_t gid = 0;
        if(_libssh2_get_u32(&buf, &uid) ||
           _libssh2_get_u32(&buf, &gid)) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }
        attrs->uid = uid;
        attrs->gid = gid;
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        uint32_t permissions;
        if(_libssh2_get_u32(&buf, &permissions)) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }
        attrs->permissions = permissions;
    }

    if(attrs->flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        uint32_t atime;
        uint32_t mtime;
        if(_libssh2_get_u32(&buf, &atime) ||
           _libssh2_get_u32(&buf, &mtime)) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }
        attrs->atime = atime;
        attrs->mtime = mtime;
    }

    
    if(attrs->flags & LIBSSH2_SFTP_ATTR_EXTENDED) {
        uint32_t extended_count;
        uint32_t i;
        size_t etype_len;
        unsigned char *etype;
        size_t edata_len;
        unsigned char *edata;

        if(_libssh2_get_u32(&buf, &extended_count)) {
            return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        }

        for(i = 0; i < extended_count; ++i) {
            if(_libssh2_get_string(&buf, &etype, &etype_len) ||
               _libssh2_get_string(&buf, &edata, &edata_len)) {
                return LIBSSH2_ERROR_BUFFER_TOO_SMALL;
            }
        }
    }

    return buf.dataptr - buf.data;
}



LIBSSH2_CHANNEL_CLOSE_FUNC(libssh2_sftp_dtor);


LIBSSH2_CHANNEL_CLOSE_FUNC(libssh2_sftp_dtor)
{
    LIBSSH2_SFTP *sftp = (LIBSSH2_SFTP *) (*channel_abstract);

    (void)session_abstract;
    (void)channel;

    
    if(sftp->partial_packet) {
        LIBSSH2_FREE(session, sftp->partial_packet);
    }

    
    if(sftp->readdir_packet) {
        LIBSSH2_FREE(session, sftp->readdir_packet);
    }

    LIBSSH2_FREE(session, sftp);
}


static LIBSSH2_SFTP *sftp_init(LIBSSH2_SESSION *session)
{
    unsigned char *data;
    size_t data_len = 0;
    ssize_t rc;
    LIBSSH2_SFTP *sftp_handle;
    struct string_buf buf;
    unsigned char *endp;

    if(session->sftpInit_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Initializing SFTP subsystem"));

        

        assert(!session->sftpInit_sftp);
        session->sftpInit_sftp = NULL;
        session->sftpInit_state = libssh2_NB_state_created;
    }

    sftp_handle = session->sftpInit_sftp;

    if(session->sftpInit_state == libssh2_NB_state_created) {
        session->sftpInit_channel =
            _libssh2_channel_open(session, "session", sizeof("session") - 1,
                                  LIBSSH2_CHANNEL_WINDOW_DEFAULT,
                                  LIBSSH2_CHANNEL_PACKET_DEFAULT, NULL, 0);
        if(!session->sftpInit_channel) {
            if(libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
                _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                               "Would block starting up channel");
            }
            else {
                _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                               "Unable to startup channel");
                session->sftpInit_state = libssh2_NB_state_idle;
            }
            return NULL;
        }

        session->sftpInit_state = libssh2_NB_state_sent;
    }

    if(session->sftpInit_state == libssh2_NB_state_sent) {
        int ret = _libssh2_channel_process_startup(session->sftpInit_channel,
                                                   "subsystem",
                                                   sizeof("subsystem") - 1,
                                                   "sftp",
                                                   strlen("sftp"));
        if(ret == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block to request SFTP subsystem");
            return NULL;
        }
        else if(ret) {
            _libssh2_error(session, LIBSSH2_ERROR_CHANNEL_FAILURE,
                           "Unable to request SFTP subsystem");
            goto sftp_init_error;
        }

        session->sftpInit_state = libssh2_NB_state_sent1;
    }

    if(session->sftpInit_state == libssh2_NB_state_sent1) {
        rc = _libssh2_channel_extended_data(session->sftpInit_channel,
                                         LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block requesting handle extended data");
            return NULL;
        }

        sftp_handle =
            session->sftpInit_sftp =
            LIBSSH2_CALLOC(session, sizeof(LIBSSH2_SFTP));
        if(!sftp_handle) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate a new SFTP structure");
            goto sftp_init_error;
        }
        sftp_handle->channel = session->sftpInit_channel;
        sftp_handle->request_id = 0;

        _libssh2_htonu32(session->sftpInit_buffer, 5);
        session->sftpInit_buffer[4] = SSH_FXP_INIT;
        _libssh2_htonu32(session->sftpInit_buffer + 5, LIBSSH2_SFTP_VERSION);
        session->sftpInit_sent = 0; 

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Sending FXP_INIT packet advertising "
                       "version %d support",
                       (int) LIBSSH2_SFTP_VERSION));

        session->sftpInit_state = libssh2_NB_state_sent2;
    }

    if(session->sftpInit_state == libssh2_NB_state_sent2) {
        
        rc = _libssh2_channel_write(session->sftpInit_channel, 0,
                                    session->sftpInit_buffer +
                                    session->sftpInit_sent,
                                    9 - session->sftpInit_sent);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block sending SSH_FXP_INIT");
            return NULL;
        }
        else if(rc < 0) {
            _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                           "Unable to send SSH_FXP_INIT");
            goto sftp_init_error;
        }
        else {
            
            session->sftpInit_sent += rc;

            if(session->sftpInit_sent == 9)
                
                session->sftpInit_state = libssh2_NB_state_sent3;

            
        }
    }

    if(session->sftpInit_state == libssh2_NB_state_error_closing) {
        rc = _libssh2_channel_free(session->sftpInit_channel);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block closing channel");
            return NULL;
        }
        session->sftpInit_channel = NULL;
        if(session->sftpInit_sftp) {
            LIBSSH2_FREE(session, session->sftpInit_sftp);
            session->sftpInit_sftp = NULL;
        }
        session->sftpInit_state = libssh2_NB_state_idle;
        return NULL;
    }

    rc = sftp_packet_require(sftp_handle, SSH_FXP_VERSION,
                             0, &data, &data_len, 5);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                       "Would block receiving SSH_FXP_VERSION");
        return NULL;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                       "Invalid SSH_FXP_VERSION response");
        goto sftp_init_error;
    }
    else if(rc) {
        _libssh2_error(session, (int)rc,
                       "Timeout waiting for response from SFTP subsystem");
        goto sftp_init_error;
    }

    buf.data = data;
    buf.dataptr = buf.data + 1;
    buf.len = data_len;
    endp = &buf.data[data_len];

    if(_libssh2_get_u32(&buf, &(sftp_handle->version))) {
        LIBSSH2_FREE(session, data);
        rc = LIBSSH2_ERROR_BUFFER_TOO_SMALL;
        goto sftp_init_error;
    }

    if(sftp_handle->version > LIBSSH2_SFTP_VERSION) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Truncating remote SFTP version from %u",
                       sftp_handle->version));
        sftp_handle->version = LIBSSH2_SFTP_VERSION;
    }
    _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                   "Enabling SFTP version %u compatibility",
                   sftp_handle->version));
    while(buf.dataptr < endp) {
        unsigned char *extname, *extdata;
        size_t extname_len, extdata_len;
        uint32_t extversion = 0;

        if(_libssh2_get_string(&buf, &extname, &extname_len)) {
            LIBSSH2_FREE(session, data);
            _libssh2_error(session, LIBSSH2_ERROR_BUFFER_TOO_SMALL,
                           "Data too short when extracting extname");
            goto sftp_init_error;
        }

        if(_libssh2_get_string(&buf, &extdata, &extdata_len)) {
            LIBSSH2_FREE(session, data);
            _libssh2_error(session, LIBSSH2_ERROR_BUFFER_TOO_SMALL,
                           "Data too short when extracting extdata");
            goto sftp_init_error;
        }

        if(extdata_len > 0) {
            char *extversion_str;
            extversion_str = (char *)LIBSSH2_ALLOC(session, extdata_len + 1);
            if(!extversion_str) {
                _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                               "Unable to allocate memory for SSH_FXP_VERSION "
                               "packet");
                goto sftp_init_error;
            }
            memcpy(extversion_str, extdata, extdata_len);
            extversion_str[extdata_len] = '\0';
            extversion = (uint32_t)strtol(extversion_str, NULL, 10);
            LIBSSH2_FREE(session, extversion_str);
        }
        if(extname_len == 24
           && strncmp("posix-rename@openssh.com", (char *)extname, 24) == 0) {
            sftp_handle->posix_rename_version = extversion;
        }

    }
    LIBSSH2_FREE(session, data);

    
    sftp_handle->channel->abstract = sftp_handle;
    sftp_handle->channel->close_cb = libssh2_sftp_dtor;

    session->sftpInit_state = libssh2_NB_state_idle;

    
    session->sftpInit_sftp = NULL;
    session->sftpInit_channel = NULL;

    _libssh2_list_init(&sftp_handle->sftp_handles);

    return sftp_handle;

sftp_init_error:
    session->sftpInit_state = libssh2_NB_state_error_closing;
    return NULL;
}


LIBSSH2_API LIBSSH2_SFTP *libssh2_sftp_init(LIBSSH2_SESSION *session)
{
    LIBSSH2_SFTP *ptr;

    if(!session)
        return NULL;

    if(!(session->state & LIBSSH2_STATE_AUTHENTICATED)) {
        _libssh2_error(session, LIBSSH2_ERROR_INVAL,
                       "session not authenticated yet");
        return NULL;
    }

    BLOCK_ADJUST_ERRNO(ptr, session, sftp_init(session));
    return ptr;
}


static int
sftp_shutdown(LIBSSH2_SFTP *sftp)
{
    int rc;
    LIBSSH2_SESSION *session = sftp->channel->session;
    
    if(sftp->partial_packet) {
        LIBSSH2_FREE(session, sftp->partial_packet);
        sftp->partial_packet = NULL;
    }
    if(sftp->open_packet) {
        LIBSSH2_FREE(session, sftp->open_packet);
        sftp->open_packet = NULL;
    }
    if(sftp->readdir_packet) {
        LIBSSH2_FREE(session, sftp->readdir_packet);
        sftp->readdir_packet = NULL;
    }
    if(sftp->fstat_packet) {
        LIBSSH2_FREE(session, sftp->fstat_packet);
        sftp->fstat_packet = NULL;
    }
    if(sftp->unlink_packet) {
        LIBSSH2_FREE(session, sftp->unlink_packet);
        sftp->unlink_packet = NULL;
    }
    if(sftp->rename_packet) {
        LIBSSH2_FREE(session, sftp->rename_packet);
        sftp->rename_packet = NULL;
    }
    if(sftp->fstatvfs_packet) {
        LIBSSH2_FREE(session, sftp->fstatvfs_packet);
        sftp->fstatvfs_packet = NULL;
    }
    if(sftp->statvfs_packet) {
        LIBSSH2_FREE(session, sftp->statvfs_packet);
        sftp->statvfs_packet = NULL;
    }
    if(sftp->mkdir_packet) {
        LIBSSH2_FREE(session, sftp->mkdir_packet);
        sftp->mkdir_packet = NULL;
    }
    if(sftp->rmdir_packet) {
        LIBSSH2_FREE(session, sftp->rmdir_packet);
        sftp->rmdir_packet = NULL;
    }
    if(sftp->stat_packet) {
        LIBSSH2_FREE(session, sftp->stat_packet);
        sftp->stat_packet = NULL;
    }
    if(sftp->symlink_packet) {
        LIBSSH2_FREE(session, sftp->symlink_packet);
        sftp->symlink_packet = NULL;
    }
    if(sftp->fsync_packet) {
        LIBSSH2_FREE(session, sftp->fsync_packet);
        sftp->fsync_packet = NULL;
    }

    sftp_packet_flush(sftp);

    

    rc = _libssh2_channel_free(sftp->channel);

    return rc;
}


LIBSSH2_API int
libssh2_sftp_shutdown(LIBSSH2_SFTP *sftp)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session, sftp_shutdown(sftp));
    return rc;
}




static LIBSSH2_SFTP_HANDLE *
sftp_open(LIBSSH2_SFTP *sftp, const char *filename,
          size_t filename_len, uint32_t flags, long mode,
          int open_type, LIBSSH2_SFTP_ATTRIBUTES *attrs_in)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    LIBSSH2_SFTP_HANDLE *fp;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {
        LIBSSH2_SFTP_ATTR_PERMISSIONS, 0, 0, 0, 0, 0, 0
    };
    unsigned char *s;
    ssize_t rc;
    int open_file = (open_type == LIBSSH2_SFTP_OPENFILE) ? 1 : 0;

    if(sftp->open_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        if(attrs_in) {
            memcpy(&attrs, attrs_in, sizeof(LIBSSH2_SFTP_ATTRIBUTES));
        }

        
        sftp->open_packet_len = (uint32_t)(filename_len + 13 +
            (open_file ? (4 + sftp_attrsize(attrs.flags)) : 0));

        
        sftp->open_packet_sent = 0;
        s = sftp->open_packet = LIBSSH2_ALLOC(session, sftp->open_packet_len);
        if(!sftp->open_packet) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate memory for FXP_OPEN or "
                           "FXP_OPENDIR packet");
            return NULL;
        }
        
        attrs.permissions = mode |
            (open_file ? LIBSSH2_SFTP_ATTR_PFILETYPE_FILE :
             LIBSSH2_SFTP_ATTR_PFILETYPE_DIR);

        _libssh2_store_u32(&s, sftp->open_packet_len - 4);
        *(s++) = open_file ? SSH_FXP_OPEN : SSH_FXP_OPENDIR;
        sftp->open_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->open_request_id);
        _libssh2_store_str(&s, filename, filename_len);

        if(open_file) {
            _libssh2_store_u32(&s, flags);
            s += sftp_attr2bin(s, &attrs);
        }

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Sending %s open request",
                       open_file ? "file" : "directory"));

        sftp->open_state = libssh2_NB_state_created;
    }

    if(sftp->open_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, sftp->open_packet+
                                    sftp->open_packet_sent,
                                    sftp->open_packet_len -
                                    sftp->open_packet_sent);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block sending FXP_OPEN or "
                           "FXP_OPENDIR command");
            return NULL;
        }
        else if(rc < 0) {
            _libssh2_error(session, (int)rc, "Unable to send FXP_OPEN*");
            LIBSSH2_FREE(session, sftp->open_packet);
            sftp->open_packet = NULL;
            sftp->open_state = libssh2_NB_state_idle;
            return NULL;
        }

        
        sftp->open_packet_sent += rc;

        if(sftp->open_packet_len == sftp->open_packet_sent) {
            LIBSSH2_FREE(session, sftp->open_packet);
            sftp->open_packet = NULL;

            sftp->open_state = libssh2_NB_state_sent;
        }
    }

    if(sftp->open_state == libssh2_NB_state_sent) {
        size_t data_len = 0;
        unsigned char *data;
        static const unsigned char fopen_responses[2] =
            { SSH_FXP_HANDLE, SSH_FXP_STATUS };
        rc = sftp_packet_requirev(sftp, 2, fopen_responses,
                                  sftp->open_request_id, &data,
                                  &data_len, 1);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            _libssh2_error(session, LIBSSH2_ERROR_EAGAIN,
                           "Would block waiting for status message");
            return NULL;
        }
        else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
            if(data_len > 0) {
                LIBSSH2_FREE(session, data);
            }
            _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                           "Response too small");
            return NULL;
        }
        sftp->open_state = libssh2_NB_state_idle;
        if(rc) {
            _libssh2_error(session, (int)rc,
                           "Timeout waiting for status message");
            return NULL;
        }

        
        if(data[0] == SSH_FXP_STATUS) {
            int badness = 1;

            if(data_len < 9) {
                _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                               "Too small FXP_STATUS");
                LIBSSH2_FREE(session, data);
                return NULL;
            }

            sftp->last_errno = _libssh2_ntohu32(data + 5);

            if(LIBSSH2_FX_OK == sftp->last_errno) {
                _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                               "got HANDLE FXOK"));

                LIBSSH2_FREE(session, data);

                
                rc = sftp_packet_require(sftp, SSH_FXP_HANDLE,
                                         sftp->open_request_id, &data,
                                         &data_len, 10);
                if(rc == LIBSSH2_ERROR_EAGAIN) {
                    
                    sftp->open_state = libssh2_NB_state_sent;
                    return NULL;
                }
                else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
                    if(data_len > 0) {
                        LIBSSH2_FREE(session, data);
                    }
                    _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                   "Too small FXP_HANDLE");
                    return NULL;
                }
                else if(!rc)
                    
                    badness = 0;
            }

            if(badness) {
                _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                               "Failed opening remote file");
                _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                               "got FXP_STATUS %d",
                               sftp->last_errno));
                LIBSSH2_FREE(session, data);
                return NULL;
            }
        }

        if(data_len < 10) {
            _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                           "Too small FXP_HANDLE");
            LIBSSH2_FREE(session, data);
            return NULL;
        }

        fp = LIBSSH2_CALLOC(session, sizeof(LIBSSH2_SFTP_HANDLE));
        if(!fp) {
            _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                           "Unable to allocate new SFTP handle structure");
            LIBSSH2_FREE(session, data);
            return NULL;
        }
        fp->handle_type = open_file ? LIBSSH2_SFTP_HANDLE_FILE :
            LIBSSH2_SFTP_HANDLE_DIR;

        fp->handle_len = _libssh2_ntohu32(data + 5);
        if(fp->handle_len > SFTP_HANDLE_MAXLEN)
            
            fp->handle_len = SFTP_HANDLE_MAXLEN;

        if(fp->handle_len > (data_len - 9))
            
            fp->handle_len = data_len - 9;

        memcpy(fp->handle, data + 9, fp->handle_len);

        LIBSSH2_FREE(session, data);

        
        _libssh2_list_add(&sftp->sftp_handles, &fp->node);

        fp->sftp = sftp; 

        fp->u.file.offset = 0;
        fp->u.file.offset_sent = 0;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Open command successful"));
        return fp;
    }
    return NULL;
}


LIBSSH2_API LIBSSH2_SFTP_HANDLE *
libssh2_sftp_open_ex(LIBSSH2_SFTP *sftp, const char *filename,
                     unsigned int filename_len, unsigned long flags, long mode,
                     int open_type)
{
    LIBSSH2_SFTP_HANDLE *hnd;

    if(!sftp)
        return NULL;

    BLOCK_ADJUST_ERRNO(hnd, sftp->channel->session,
                       sftp_open(sftp, filename, filename_len, (uint32_t)flags,
                                 mode, open_type, NULL));
    return hnd;
}


LIBSSH2_API LIBSSH2_SFTP_HANDLE *
libssh2_sftp_open_ex_r(LIBSSH2_SFTP *sftp, const char *filename,
                       size_t filename_len, unsigned long flags, long mode,
                       int open_type, LIBSSH2_SFTP_ATTRIBUTES *attrs)
{
    LIBSSH2_SFTP_HANDLE *hnd;

    if(!sftp)
        return NULL;

    BLOCK_ADJUST_ERRNO(hnd, sftp->channel->session,
                       sftp_open(sftp, filename, filename_len, (uint32_t)flags,
                                 mode, open_type, attrs));
    return hnd;
}


static ssize_t sftp_read(LIBSSH2_SFTP_HANDLE * handle, char *buffer,
                         size_t buffer_size)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t count = 0;
    struct sftp_pipeline_chunk *chunk;
    struct sftp_pipeline_chunk *next;
    ssize_t rc;
    struct _libssh2_sftp_handle_file_data *filep =
        &handle->u.file;
    size_t bytes_in_buffer = 0;
    char *sliding_bufferp = buffer;

    

    switch(sftp->read_state) {
    case libssh2_NB_state_idle:
        sftp->last_errno = LIBSSH2_FX_OK;

        
        if(filep->data_left) {
            size_t copy = LIBSSH2_MIN(buffer_size, filep->data_left);

            memcpy(buffer, &filep->data[ filep->data_len - filep->data_left],
                   copy);

            filep->data_left -= copy;
            filep->offset += copy;

            if(!filep->data_left) {
                LIBSSH2_FREE(session, filep->data);
                filep->data = NULL;
            }

            return copy;
        }

        if(filep->eof) {
            return 0;
        }
        else {
            

            
            size_t already = (size_t)(filep->offset_sent - filep->offset);

            size_t max_read_ahead = buffer_size*4;
            unsigned long recv_window;

            if(max_read_ahead > LIBSSH2_CHANNEL_WINDOW_DEFAULT*4)
                max_read_ahead = LIBSSH2_CHANNEL_WINDOW_DEFAULT*4;

            
            if(max_read_ahead > already)
                count = max_read_ahead - already;

            

            recv_window = libssh2_channel_window_read_ex(sftp->channel,
                                                         NULL, NULL);
            if(max_read_ahead > recv_window) {
                

                rc = _libssh2_channel_receive_window_adjust(sftp->channel,
                                               (uint32_t)(max_read_ahead * 8),
                                               1, NULL);
                
                assert(rc != LIBSSH2_ERROR_EAGAIN || !filep->data_left);
                assert(rc != LIBSSH2_ERROR_EAGAIN || !filep->eof);
                if(rc)
                    return rc;
            }
        }

        while(count > 0) {
            unsigned char *s;

            
            uint32_t packet_len = (uint32_t)(handle->handle_len + 25);
            uint32_t request_id;

            uint32_t size = (uint32_t)count;
            if(size < buffer_size)
                size = (uint32_t)buffer_size;
            if(size > MAX_SFTP_READ_SIZE)
                size = MAX_SFTP_READ_SIZE;

            chunk = LIBSSH2_ALLOC(session, packet_len +
                                  sizeof(struct sftp_pipeline_chunk));
            if(!chunk)
                return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                      "malloc fail for FXP_WRITE");

            chunk->offset = filep->offset_sent;
            chunk->len = size;
            chunk->lefttosend = packet_len;
            chunk->sent = 0;

            s = chunk->packet;

            _libssh2_store_u32(&s, packet_len - 4);
            *s++ = SSH_FXP_READ;
            request_id = sftp->request_id++;
            chunk->request_id = request_id;
            _libssh2_store_u32(&s, request_id);
            _libssh2_store_str(&s, handle->handle, handle->handle_len);
            _libssh2_store_u64(&s, filep->offset_sent);
            filep->offset_sent += size; 
            _libssh2_store_u32(&s, size);

            
            _libssh2_list_add(&handle->packet_list, &chunk->node);
            
            count -= LIBSSH2_MIN(size, count);
            _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                           "read request id %d sent (offset: %lu, size: %lu)",
                           request_id, (unsigned long)chunk->offset,
                           (unsigned long)chunk->len));
        }
        LIBSSH2_FALLTHROUGH();
    case libssh2_NB_state_sent:

        sftp->read_state = libssh2_NB_state_idle;

        
        chunk = _libssh2_list_first(&handle->packet_list);

        while(chunk) {
            if(chunk->lefttosend) {

                rc = _libssh2_channel_write(channel, 0,
                                            &chunk->packet[chunk->sent],
                                            chunk->lefttosend);
                if(rc < 0) {
                    sftp->read_state = libssh2_NB_state_sent;
                    return rc;
                }

                
                chunk->lefttosend -= rc;
                chunk->sent += rc;

                if(chunk->lefttosend) {
                    
                    if(chunk != _libssh2_list_first(&handle->packet_list)) {
                        break;
                    }
                    else {
                        continue;
                    }
                }
            }

            
            chunk = _libssh2_list_next(&chunk->node);
        }
        LIBSSH2_FALLTHROUGH();

    case libssh2_NB_state_sent2:

        sftp->read_state = libssh2_NB_state_idle;

        
        chunk = _libssh2_list_first(&handle->packet_list);

        while(chunk) {
            unsigned char *data;
            size_t data_len = 0;
            uint32_t rc32;
            static const unsigned char read_responses[2] = {
                SSH_FXP_DATA, SSH_FXP_STATUS
            };

            if(chunk->lefttosend) {
                
                if(bytes_in_buffer > 0) {
                    return bytes_in_buffer;
                }
                else {
                    
                    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                          "sftp_read() internal error");
                }
            }

            rc = sftp_packet_requirev(sftp, 2, read_responses,
                                      chunk->request_id, &data, &data_len, 9);
            if(rc == LIBSSH2_ERROR_EAGAIN && bytes_in_buffer) {
                
                return bytes_in_buffer;
            }

            if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
                if(data_len > 0) {
                    LIBSSH2_FREE(session, data);
                }
                return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                      "Response too small");
            }
            else if(rc < 0) {
                sftp->read_state = libssh2_NB_state_sent2;
                return rc;
            }

            

            switch(data[0]) {
            case SSH_FXP_STATUS:
                

                _libssh2_list_remove(&chunk->node);
                LIBSSH2_FREE(session, chunk);

                
                sftp_packetlist_flush(handle);

                rc32 = _libssh2_ntohu32(data + 5);
                LIBSSH2_FREE(session, data);

                if(rc32 == LIBSSH2_FX_EOF) {
                    filep->eof = TRUE;
                    return bytes_in_buffer;
                }
                else {
                    sftp->last_errno = rc32;
                    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                          "SFTP READ error");
                }

            case SSH_FXP_DATA:
                if(chunk->offset != filep->offset) {
                    
                    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                          "Read Packet At Unexpected Offset");
                }

                rc32 = _libssh2_ntohu32(data + 5);
                if(rc32 > (data_len - 9))
                    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                          "SFTP Protocol badness");

                if(rc32 > chunk->len) {
                    
                    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                          "FXP_READ response too big");
                }

                if(rc32 != chunk->len) {
                    
                    filep->offset_sent -= (chunk->len - rc32);
                }

                if((bytes_in_buffer + rc32) > buffer_size) {
                    
                    filep->data_left = (bytes_in_buffer + rc32) - buffer_size;

                    
                    rc32 = (uint32_t)(buffer_size - bytes_in_buffer);

                    
                    filep->data = data;
                    filep->data_len = data_len;
                }
                else
                    filep->data_len = 0;

                
                memcpy(sliding_bufferp, data + 9, rc32);
                filep->offset += rc32;
                bytes_in_buffer += rc32;
                sliding_bufferp += rc32;

                if(filep->data_len == 0)
                    
                    LIBSSH2_FREE(session, data);

                
                next = _libssh2_list_next(&chunk->node);
                _libssh2_list_remove(&chunk->node);
                LIBSSH2_FREE(session, chunk);

                
                if(bytes_in_buffer < buffer_size) {
                    chunk = next;
                }
                else {
                    chunk = NULL;
                }

                break;
            default:
                return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                      "SFTP Protocol badness: unrecognised "
                                      "read request response");
            }
        }

        if(bytes_in_buffer > 0)
            return bytes_in_buffer;

        break;

    default:
        assert(0);  
    }

    
    return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                          "sftp_read() internal error");
}


LIBSSH2_API ssize_t
libssh2_sftp_read(LIBSSH2_SFTP_HANDLE *hnd, char *buffer,
                  size_t buffer_maxlen)
{
    ssize_t rc;
    if(!hnd)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session,
                 sftp_read(hnd, buffer, buffer_maxlen));
    return rc;
}


static ssize_t sftp_readdir(LIBSSH2_SFTP_HANDLE *handle, char *buffer,
                            size_t buffer_maxlen, char *longentry,
                            size_t longentry_maxlen,
                            LIBSSH2_SFTP_ATTRIBUTES *attrs)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    uint32_t num_names;
    
    uint32_t packet_len = (uint32_t)(handle->handle_len + 13);
    unsigned char *s, *data;
    static const unsigned char read_responses[2] = {
        SSH_FXP_NAME, SSH_FXP_STATUS };
    ssize_t retcode;

    if(sftp->readdir_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        if(handle->u.dir.names_left) {
            
            LIBSSH2_SFTP_ATTRIBUTES attrs_dummy;
            size_t real_longentry_len;
            size_t real_filename_len;
            size_t filename_len;
            size_t longentry_len;
            size_t names_packet_len = handle->u.dir.names_packet_len;
            ssize_t attr_len = 0;

            if(names_packet_len >= 4) {
                s = (unsigned char *) handle->u.dir.next_name;
                real_filename_len = _libssh2_ntohu32(s);
                s += 4;
                names_packet_len -= 4;
            }
            else {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            filename_len = real_filename_len;
            if(filename_len >= buffer_maxlen) {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            if(buffer_maxlen >= filename_len && names_packet_len >=
               filename_len) {
                memcpy(buffer, s, filename_len);
                buffer[filename_len] = '\0';           
                s += real_filename_len;
                names_packet_len -= real_filename_len;
            }
            else {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            if(names_packet_len >= 4) {
                real_longentry_len = _libssh2_ntohu32(s);
                s += 4;
                names_packet_len -= 4;
            }
            else {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            if(longentry && (longentry_maxlen > 1)) {
                longentry_len = real_longentry_len;

                if(longentry_len >= longentry_maxlen ||
                   longentry_len > names_packet_len) {
                    filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                    goto end;
                }

                memcpy(longentry, s, longentry_len);
                longentry[longentry_len] = '\0'; 
            }

            if(real_longentry_len <= names_packet_len) {
                s += real_longentry_len;
                names_packet_len -= real_longentry_len;
            }
            else {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            if(attrs)
                memset(attrs, 0, sizeof(LIBSSH2_SFTP_ATTRIBUTES));

            attr_len = sftp_bin2attr(attrs ? attrs : &attrs_dummy, s,
                                     names_packet_len);

            if(attr_len >= 0) {
                s += attr_len;
                names_packet_len -= attr_len;
            }
            else {
                filename_len = (size_t)LIBSSH2_ERROR_BUFFER_TOO_SMALL;
                goto end;
            }

            handle->u.dir.next_name = (char *) s;
            handle->u.dir.names_packet_len = names_packet_len;

            if((--handle->u.dir.names_left) == 0)
                LIBSSH2_FREE(session, handle->u.dir.names_packet);

end:
            _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                           "libssh2_sftp_readdir_ex() return %lu",
                           (unsigned long)filename_len));
            return (ssize_t)filename_len;
        }

        

        s = sftp->readdir_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->readdir_packet)
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "FXP_READDIR packet");

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_READDIR;
        sftp->readdir_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->readdir_request_id);
        _libssh2_store_str(&s, handle->handle, handle->handle_len);

        sftp->readdir_state = libssh2_NB_state_created;
    }

    if(sftp->readdir_state == libssh2_NB_state_created) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Reading entries from directory handle"));
        retcode = _libssh2_channel_write(channel, 0, sftp->readdir_packet,
                                         packet_len);
        if(retcode == LIBSSH2_ERROR_EAGAIN) {
            return retcode;
        }
        else if((ssize_t)packet_len != retcode) {
            LIBSSH2_FREE(session, sftp->readdir_packet);
            sftp->readdir_packet = NULL;
            sftp->readdir_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }

        LIBSSH2_FREE(session, sftp->readdir_packet);
        sftp->readdir_packet = NULL;

        sftp->readdir_state = libssh2_NB_state_sent;
    }

    retcode = sftp_packet_requirev(sftp, 2, read_responses,
                                   sftp->readdir_request_id, &data,
                                   &data_len, 9);
    if(retcode == LIBSSH2_ERROR_EAGAIN)
        return retcode;
    else if(retcode == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "Status message too short");
    }
    else if(retcode) {
        sftp->readdir_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)retcode,
                              "Timeout waiting for status message");
    }

    if(data[0] == SSH_FXP_STATUS) {
        unsigned int rerrno;
        rerrno = _libssh2_ntohu32(data + 5);
        LIBSSH2_FREE(session, data);
        if(rerrno == LIBSSH2_FX_EOF) {
            sftp->readdir_state = libssh2_NB_state_idle;
            return 0;
        }
        else {
            sftp->last_errno = rerrno;
            sftp->readdir_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "SFTP Protocol Error");
        }
    }

    sftp->readdir_state = libssh2_NB_state_idle;

    num_names = _libssh2_ntohu32(data + 5);
    _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "%u entries returned",
                   num_names));
    if(!num_names) {
        LIBSSH2_FREE(session, data);
        return 0;
    }

    handle->u.dir.names_left = num_names;
    handle->u.dir.names_packet = data;
    handle->u.dir.next_name = (char *) data + 9;
    handle->u.dir.names_packet_len = data_len - 9;

    
    return sftp_readdir(handle, buffer, buffer_maxlen, longentry,
                        longentry_maxlen, attrs);
}


LIBSSH2_API int
libssh2_sftp_readdir_ex(LIBSSH2_SFTP_HANDLE *hnd, char *buffer,
                        size_t buffer_maxlen, char *longentry,
                        size_t longentry_maxlen,
                        LIBSSH2_SFTP_ATTRIBUTES *attrs)
{
    ssize_t rc;
    if(!hnd)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session,
                 sftp_readdir(hnd, buffer, buffer_maxlen, longentry,
                              longentry_maxlen, attrs));
    return (int)rc;  
}



static ssize_t sftp_write(LIBSSH2_SFTP_HANDLE *handle, const char *buffer,
                          size_t count)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    uint32_t retcode;
    uint32_t packet_len;
    unsigned char *s, *data = NULL;
    ssize_t rc;
    struct sftp_pipeline_chunk *chunk;
    struct sftp_pipeline_chunk *next;
    size_t acked = 0;
    size_t org_count = count;
    size_t already;

    switch(sftp->write_state) {
    default:
    case libssh2_NB_state_idle:
        sftp->last_errno = LIBSSH2_FX_OK;

        
        already = (size_t)(handle->u.file.offset_sent -
                           handle->u.file.offset)+
            handle->u.file.acked;

        if(count >= already) {
            
            buffer += already;
            count -= already;
        }
        else
            
            count = 0;

        sftp->write_state = libssh2_NB_state_idle;
        while(count) {
            
            uint32_t size =
                (uint32_t)(LIBSSH2_MIN(MAX_SFTP_OUTGOING_SIZE, count));
            uint32_t request_id;

            
            packet_len = (uint32_t)(handle->handle_len + size + 25);

            chunk = LIBSSH2_ALLOC(session, packet_len +
                                  sizeof(struct sftp_pipeline_chunk));
            if(!chunk)
                return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                      "malloc fail for FXP_WRITE");

            chunk->len = size;
            chunk->sent = 0;
            chunk->lefttosend = packet_len;

            s = chunk->packet;
            _libssh2_store_u32(&s, packet_len - 4);

            *(s++) = SSH_FXP_WRITE;
            request_id = sftp->request_id++;
            chunk->request_id = request_id;
            _libssh2_store_u32(&s, request_id);
            _libssh2_store_str(&s, handle->handle, handle->handle_len);
            _libssh2_store_u64(&s, handle->u.file.offset_sent);
            handle->u.file.offset_sent += size; 
            _libssh2_store_str(&s, buffer, size);

            
            _libssh2_list_add(&handle->packet_list, &chunk->node);

            buffer += size;
            count -= size; 
        }

        
        chunk = _libssh2_list_first(&handle->packet_list);

        while(chunk) {
            if(chunk->lefttosend) {
                rc = _libssh2_channel_write(channel, 0,
                                            &chunk->packet[chunk->sent],
                                            chunk->lefttosend);
                if(rc < 0)
                    
                    return rc;

                
                chunk->lefttosend -= rc;
                chunk->sent += rc;

                if(chunk->lefttosend)
                    
                    break;
            }

            
            chunk = _libssh2_list_next(&chunk->node);
        }

        LIBSSH2_FALLTHROUGH();

    case libssh2_NB_state_sent:

        sftp->write_state = libssh2_NB_state_idle;
        
        chunk = _libssh2_list_first(&handle->packet_list);

        while(chunk) {
            if(chunk->lefttosend)
                
                break;

            else if(acked)
                
                break;

            
            rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                                     chunk->request_id, &data, &data_len, 9);
            if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
                if(data_len > 0) {
                    LIBSSH2_FREE(session, data);
                }
                return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                      "FXP write packet too short");
            }
            else if(rc < 0) {
                if(rc == LIBSSH2_ERROR_EAGAIN)
                    sftp->write_state = libssh2_NB_state_sent;
                return rc;
            }

            retcode = _libssh2_ntohu32(data + 5);
            LIBSSH2_FREE(session, data);

            sftp->last_errno = retcode;
            if(retcode == LIBSSH2_FX_OK) {
                acked += chunk->len; 

                
                handle->u.file.offset += chunk->len;

                next = _libssh2_list_next(&chunk->node);

                _libssh2_list_remove(&chunk->node); 
                LIBSSH2_FREE(session, chunk); 

                chunk = next;
            }
            else {
                
                sftp_packetlist_flush(handle);

                
                handle->u.file.offset -= handle->u.file.acked;

                
                handle->u.file.offset_sent = handle->u.file.offset;

                
                handle->u.file.acked = 0;

                
                return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                      "FXP write failed");
            }
        }
        break;
    }

    
    acked += handle->u.file.acked;

    if(acked) {
        ssize_t ret = LIBSSH2_MIN(acked, org_count);
        

        
        handle->u.file.acked = acked - ret;

        return ret;
    }

    else
        return 0; 
}


LIBSSH2_API ssize_t
libssh2_sftp_write(LIBSSH2_SFTP_HANDLE *hnd, const char *buffer,
                   size_t count)
{
    ssize_t rc;
    if(!hnd)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session,
                 sftp_write(hnd, buffer, count));
    return rc;

}

static int sftp_fsync(LIBSSH2_SFTP_HANDLE *handle)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    
    uint32_t packet_len = (uint32_t)(handle->handle_len + 34);
    size_t data_len = 0;
    unsigned char *packet, *s, *data = NULL;
    ssize_t rc;
    uint32_t retcode;

    if(sftp->fsync_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Issuing fsync command"));
        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_EXTENDED "
                                  "packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_EXTENDED;
        sftp->fsync_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->fsync_request_id);
        _libssh2_store_str(&s, "fsync@openssh.com", 17);
        _libssh2_store_str(&s, handle->handle, handle->handle_len);

        sftp->fsync_state = libssh2_NB_state_created;
    }
    else {
        packet = sftp->fsync_packet;
    }

    if(sftp->fsync_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, packet, packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN ||
            (0 <= rc && rc < (ssize_t)packet_len)) {
            sftp->fsync_packet = packet;
            return LIBSSH2_ERROR_EAGAIN;
        }

        LIBSSH2_FREE(session, packet);
        sftp->fsync_packet = NULL;

        if(rc < 0) {
            sftp->fsync_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }
        sftp->fsync_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                             sftp->fsync_request_id, &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return (int)rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP fsync packet too short");
    }
    else if(rc) {
        sftp->fsync_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)rc,
                              "Error waiting for FXP EXTENDED REPLY");
    }

    sftp->fsync_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    if(retcode != LIBSSH2_FX_OK) {
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "fsync failed");
    }

    return 0;
}


LIBSSH2_API int
libssh2_sftp_fsync(LIBSSH2_SFTP_HANDLE *hnd)
{
    int rc;
    if(!hnd)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session,
                 sftp_fsync(hnd));
    return rc;
}



static int sftp_fstat(LIBSSH2_SFTP_HANDLE *handle,
                      LIBSSH2_SFTP_ATTRIBUTES *attrs, int setstat)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    
    uint32_t packet_len = (uint32_t)(handle->handle_len + 13 +
                                 (setstat ? sftp_attrsize(attrs->flags) : 0));
    unsigned char *s, *data = NULL;
    static const unsigned char fstat_responses[2] =
        { SSH_FXP_ATTRS, SSH_FXP_STATUS };
    ssize_t rc;

    if(sftp->fstat_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Issuing %s command",
                       setstat ? "set-stat" : "stat"));
        s = sftp->fstat_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->fstat_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "FSTAT/FSETSTAT packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = setstat ? SSH_FXP_FSETSTAT : SSH_FXP_FSTAT;
        sftp->fstat_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->fstat_request_id);
        _libssh2_store_str(&s, handle->handle, handle->handle_len);

        if(setstat) {
            s += sftp_attr2bin(s, attrs);
        }

        sftp->fstat_state = libssh2_NB_state_created;
    }

    if(sftp->fstat_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, sftp->fstat_packet,
                                    packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return (int)rc;
        }
        else if((ssize_t)packet_len != rc) {
            LIBSSH2_FREE(session, sftp->fstat_packet);
            sftp->fstat_packet = NULL;
            sftp->fstat_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  (setstat ? "Unable to send FXP_FSETSTAT"
                                   : "Unable to send FXP_FSTAT command"));
        }
        LIBSSH2_FREE(session, sftp->fstat_packet);
        sftp->fstat_packet = NULL;

        sftp->fstat_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_requirev(sftp, 2, fstat_responses,
                              sftp->fstat_request_id, &data,
                              &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN)
        return (int)rc;
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP fstat packet too short");
    }
    else if(rc) {
        sftp->fstat_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)rc,
                              "Timeout waiting for status message");
    }

    sftp->fstat_state = libssh2_NB_state_idle;

    if(data[0] == SSH_FXP_STATUS) {
        uint32_t retcode;

        retcode = _libssh2_ntohu32(data + 5);
        LIBSSH2_FREE(session, data);
        if(retcode == LIBSSH2_FX_OK) {
            return 0;
        }
        else {
            sftp->last_errno = retcode;
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "SFTP Protocol Error");
        }
    }

    if(sftp_bin2attr(attrs, data + 5, data_len - 5) < 0) {
        LIBSSH2_FREE(session, data);
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "Attributes too short in SFTP fstat");
    }

    LIBSSH2_FREE(session, data);

    return 0;
}


LIBSSH2_API int
libssh2_sftp_fstat_ex(LIBSSH2_SFTP_HANDLE *hnd,
                      LIBSSH2_SFTP_ATTRIBUTES *attrs, int setstat)
{
    int rc;
    if(!hnd || !attrs)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session,
                 sftp_fstat(hnd, attrs, setstat));
    return rc;
}



LIBSSH2_API void
libssh2_sftp_seek64(LIBSSH2_SFTP_HANDLE *handle, libssh2_uint64_t offset)
{
    if(!handle)
        return;
    if(handle->u.file.offset == offset && handle->u.file.offset_sent == offset)
        return;

    handle->u.file.offset = handle->u.file.offset_sent = offset;
    
    sftp_packetlist_flush(handle);

    
    if(handle->u.file.data_left) {
        LIBSSH2_FREE(handle->sftp->channel->session, handle->u.file.data);
        handle->u.file.data_left = handle->u.file.data_len = 0;
        handle->u.file.data = NULL;
    }

    
    handle->u.file.eof = FALSE;
}


LIBSSH2_API void
libssh2_sftp_seek(LIBSSH2_SFTP_HANDLE *handle, size_t offset)
{
    libssh2_sftp_seek64(handle, (libssh2_uint64_t)offset);
}


LIBSSH2_API size_t
libssh2_sftp_tell(LIBSSH2_SFTP_HANDLE *handle)
{
    if(!handle)
        return 0; 

    
    return (size_t)(handle->u.file.offset);
}


LIBSSH2_API libssh2_uint64_t
libssh2_sftp_tell64(LIBSSH2_SFTP_HANDLE *handle)
{
    if(!handle)
        return 0; 

    return handle->u.file.offset;
}


static void sftp_packet_flush(LIBSSH2_SFTP *sftp)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    LIBSSH2_SFTP_PACKET *packet = _libssh2_list_first(&sftp->packets);
    struct sftp_zombie_requests *zombie =
        _libssh2_list_first(&sftp->zombie_requests);

    while(packet) {
        LIBSSH2_SFTP_PACKET *next;

        
        next =  _libssh2_list_next(&packet->node);
        _libssh2_list_remove(&packet->node);
        LIBSSH2_FREE(session, packet->data);
        LIBSSH2_FREE(session, packet);

        packet = next;
    }

    while(zombie) {
        
        struct sftp_zombie_requests *next = _libssh2_list_next(&zombie->node);
        
        _libssh2_list_remove(&zombie->node);
        
        LIBSSH2_FREE(session, zombie);
        zombie = next;
    }

}


static int
sftp_close_handle(LIBSSH2_SFTP_HANDLE *handle)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    
    uint32_t packet_len = (uint32_t)(handle->handle_len + 13);
    unsigned char *s, *data = NULL;
    int rc = 0;

    if(handle->close_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Closing handle"));
        s = handle->close_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!handle->close_packet) {
            handle->close_state = libssh2_NB_state_idle;
            rc = _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                "Unable to allocate memory for FXP_CLOSE "
                                "packet");
        }
        else {

            _libssh2_store_u32(&s, packet_len - 4);
            *(s++) = SSH_FXP_CLOSE;
            handle->close_request_id = sftp->request_id++;
            _libssh2_store_u32(&s, handle->close_request_id);
            _libssh2_store_str(&s, handle->handle, handle->handle_len);
            handle->close_state = libssh2_NB_state_created;
        }
    }

    if(handle->close_state == libssh2_NB_state_created) {
        ssize_t nwritten;
        nwritten = _libssh2_channel_write(channel, 0, handle->close_packet,
                                          packet_len);
        if(nwritten == LIBSSH2_ERROR_EAGAIN) {
            return (int)nwritten;
        }
        else if((ssize_t)packet_len != nwritten) {
            handle->close_state = libssh2_NB_state_idle;
            rc = _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                "Unable to send FXP_CLOSE command");
        }
        else
            handle->close_state = libssh2_NB_state_sent;

        LIBSSH2_FREE(session, handle->close_packet);
        handle->close_packet = NULL;
    }

    if(handle->close_state == libssh2_NB_state_sent) {
        rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                                 handle->close_request_id, &data,
                                 &data_len, 9);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return rc;
        }
        else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
            if(data_len > 0) {
                LIBSSH2_FREE(session, data);
            }
            data = NULL;
            _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                           "Packet too short in FXP_CLOSE command");
        }
        else if(rc) {
            _libssh2_error(session, rc,
                           "Error waiting for status message");
        }

        handle->close_state = libssh2_NB_state_sent1;
    }

    if(!data) {
        
        assert(rc);

    }
    else {
        uint32_t retcode = _libssh2_ntohu32(data + 5);
        LIBSSH2_FREE(session, data);

        if(retcode != LIBSSH2_FX_OK) {
            sftp->last_errno = retcode;
            handle->close_state = libssh2_NB_state_idle;
            rc = _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                "SFTP Protocol Error");
        }
    }

    
    _libssh2_list_remove(&handle->node);

    if(handle->handle_type == LIBSSH2_SFTP_HANDLE_DIR) {
        if(handle->u.dir.names_left)
            LIBSSH2_FREE(session, handle->u.dir.names_packet);
    }
    else if(handle->handle_type == LIBSSH2_SFTP_HANDLE_FILE) {
        if(handle->u.file.data)
            LIBSSH2_FREE(session, handle->u.file.data);
    }

    sftp_packetlist_flush(handle);
    sftp->read_state = libssh2_NB_state_idle;

    handle->close_state = libssh2_NB_state_idle;

    LIBSSH2_FREE(session, handle);

    return rc;
}


LIBSSH2_API int
libssh2_sftp_close_handle(LIBSSH2_SFTP_HANDLE *hnd)
{
    int rc;
    if(!hnd)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, hnd->sftp->channel->session, sftp_close_handle(hnd));
    return rc;
}


static int sftp_unlink(LIBSSH2_SFTP *sftp, const char *filename,
                       size_t filename_len)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    uint32_t retcode;
    
    uint32_t packet_len = (uint32_t)(filename_len + 13);
    unsigned char *s, *data = NULL;
    int rc;

    if(sftp->unlink_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Unlinking %s", filename));
        s = sftp->unlink_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->unlink_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_REMOVE "
                                  "packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_REMOVE;
        sftp->unlink_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->unlink_request_id);
        _libssh2_store_str(&s, filename, filename_len);
        sftp->unlink_state = libssh2_NB_state_created;
    }

    if(sftp->unlink_state == libssh2_NB_state_created) {
        ssize_t nwritten;
        nwritten = _libssh2_channel_write(channel, 0, sftp->unlink_packet,
                                          packet_len);
        if(nwritten == LIBSSH2_ERROR_EAGAIN) {
            return (int)nwritten;
        }
        else if((ssize_t)packet_len != nwritten) {
            LIBSSH2_FREE(session, sftp->unlink_packet);
            sftp->unlink_packet = NULL;
            sftp->unlink_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send FXP_REMOVE command");
        }
        LIBSSH2_FREE(session, sftp->unlink_packet);
        sftp->unlink_packet = NULL;

        sftp->unlink_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                             sftp->unlink_request_id, &data,
                             &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP unlink packet too short");
    }
    else if(rc) {
        sftp->unlink_state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc,
                              "Error waiting for FXP STATUS");
    }

    sftp->unlink_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    if(retcode == LIBSSH2_FX_OK) {
        return 0;
    }
    else {
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error");
    }
}


LIBSSH2_API int
libssh2_sftp_unlink_ex(LIBSSH2_SFTP *sftp, const char *filename,
                       unsigned int filename_len)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_unlink(sftp, filename, filename_len));
    return rc;
}


static int sftp_rename(LIBSSH2_SFTP *sftp, const char *source_filename,
                       unsigned int source_filename_len,
                       const char *dest_filename,
                       unsigned int dest_filename_len, long flags)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    int retcode;
    uint32_t packet_len =
        source_filename_len + dest_filename_len + 17 +
        (sftp->version >= 5 ? 4 : 0);
    
    unsigned char *data = NULL;
    ssize_t rc;

    if(sftp->rename_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        if(sftp->version < 2) {
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "Server does not support RENAME");
        }

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Renaming %s to %s",
                       source_filename, dest_filename));
        sftp->rename_s = sftp->rename_packet =
            LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->rename_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_RENAME "
                                  "packet");
        }

        _libssh2_store_u32(&sftp->rename_s, packet_len - 4);
        *(sftp->rename_s++) = SSH_FXP_RENAME;
        sftp->rename_request_id = sftp->request_id++;
        _libssh2_store_u32(&sftp->rename_s, sftp->rename_request_id);
        _libssh2_store_str(&sftp->rename_s, source_filename,
                           source_filename_len);
        _libssh2_store_str(&sftp->rename_s, dest_filename, dest_filename_len);

        if(sftp->version >= 5)
            _libssh2_store_u32(&sftp->rename_s, (uint32_t)flags);

        sftp->rename_state = libssh2_NB_state_created;
    }

    if(sftp->rename_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, sftp->rename_packet,
                                    sftp->rename_s - sftp->rename_packet);
        if(rc == LIBSSH2_ERROR_EAGAIN) {
            return (int)rc;
        }
        else if((ssize_t)packet_len != rc) {
            LIBSSH2_FREE(session, sftp->rename_packet);
            sftp->rename_packet = NULL;
            sftp->rename_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send FXP_RENAME command");
        }
        LIBSSH2_FREE(session, sftp->rename_packet);
        sftp->rename_packet = NULL;

        sftp->rename_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                             sftp->rename_request_id, &data,
                             &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return (int)rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP rename packet too short");
    }
    else if(rc) {
        sftp->rename_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)rc,
                              "Error waiting for FXP STATUS");
    }

    sftp->rename_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    sftp->last_errno = retcode;

    
    switch(retcode) {
    case LIBSSH2_FX_OK:
        retcode = LIBSSH2_ERROR_NONE;
        break;

    case LIBSSH2_FX_FILE_ALREADY_EXISTS:
        retcode = _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                 "File already exists and "
                                 "SSH_FXP_RENAME_OVERWRITE not specified");
        break;

    case LIBSSH2_FX_OP_UNSUPPORTED:
        retcode = _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                 "Operation Not Supported");
        break;

    default:
        retcode = _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                 "SFTP Protocol Error");
        break;
    }

    return retcode;
}


LIBSSH2_API int
libssh2_sftp_rename_ex(LIBSSH2_SFTP *sftp, const char *source_filename,
                       unsigned int source_filename_len,
                       const char *dest_filename,
                       unsigned int dest_filename_len, long flags)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_rename(sftp, source_filename, source_filename_len,
                             dest_filename, dest_filename_len, flags));
    return rc;
}

static int
sftp_posix_rename(LIBSSH2_SFTP *sftp, const char *source_filename,
                  size_t source_filename_len,
                  const char *dest_filename,
                  size_t dest_filename_len)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    uint32_t packet_len;
    size_t data_len = 0;
    unsigned char *packet, *s, *data = NULL;
    ssize_t rc;
    uint32_t retcode;

    if(sftp->posix_rename_version != 1) {
        return _libssh2_error(session, LIBSSH2_FX_OP_UNSUPPORTED,
                              "Server does not support"
                              "posix-rename@openssh.com");
    }

    if(source_filename_len > UINT32_MAX ||
       dest_filename_len > UINT32_MAX ||
       45 + source_filename_len + dest_filename_len > UINT32_MAX) {
        return _libssh2_error(session, LIBSSH2_ERROR_OUT_OF_BOUNDARY,
                              "Input too large"
                              "posix-rename@openssh.com");
    }

    packet_len = (uint32_t)(45 + source_filename_len + dest_filename_len);

    

    if(sftp->posix_rename_state == libssh2_NB_state_idle) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Issuing posix_rename command"));
        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_EXTENDED "
                                  "packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_EXTENDED;
        sftp->posix_rename_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->posix_rename_request_id);
        _libssh2_store_str(&s, "posix-rename@openssh.com", 24);
        _libssh2_store_str(&s, source_filename, source_filename_len);
        _libssh2_store_str(&s, dest_filename, dest_filename_len);

        sftp->posix_rename_state = libssh2_NB_state_created;
    }
    else {
        packet = sftp->posix_rename_packet;
    }

    if(sftp->posix_rename_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, packet, packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN ||
            (0 <= rc && rc < (ssize_t)packet_len)) {
            sftp->posix_rename_packet = packet;
            return LIBSSH2_ERROR_EAGAIN;
        }

        LIBSSH2_FREE(session, packet);
        sftp->posix_rename_packet = NULL;

        if(rc < 0) {
            sftp->posix_rename_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }
        sftp->posix_rename_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                             sftp->posix_rename_request_id,
                             &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return (int)rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP posix_rename packet too short");
    }
    else if(rc) {
        sftp->posix_rename_state = libssh2_NB_state_idle;
        return (int)_libssh2_error(session, (int)rc,
                                   "Error waiting for FXP EXTENDED REPLY");
    }

    sftp->posix_rename_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    if(retcode != LIBSSH2_FX_OK) {
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "posix_rename failed");
    }

    return 0;
}


LIBSSH2_API int
libssh2_sftp_posix_rename_ex(LIBSSH2_SFTP *sftp, const char *source_filename,
                             size_t source_filename_len,
                             const char *dest_filename,
                             size_t dest_filename_len)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_posix_rename(sftp, source_filename, source_filename_len,
                                   dest_filename, dest_filename_len));
    return rc;
}


static int sftp_fstatvfs(LIBSSH2_SFTP_HANDLE *handle, LIBSSH2_SFTP_STATVFS *st)
{
    LIBSSH2_SFTP *sftp = handle->sftp;
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    
    
    uint32_t packet_len = (uint32_t)(handle->handle_len + 20 + 17);
    unsigned char *packet, *s, *data = NULL;
    ssize_t rc;
    unsigned int flag;
    static const unsigned char responses[2] =
        { SSH_FXP_EXTENDED_REPLY, SSH_FXP_STATUS };

    if(sftp->fstatvfs_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Getting file system statistics"));
        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_EXTENDED "
                                  "packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_EXTENDED;
        sftp->fstatvfs_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->fstatvfs_request_id);
        _libssh2_store_str(&s, "fstatvfs@openssh.com", 20);
        _libssh2_store_str(&s, handle->handle, handle->handle_len);

        sftp->fstatvfs_state = libssh2_NB_state_created;
    }
    else {
        packet = sftp->fstatvfs_packet;
    }

    if(sftp->fstatvfs_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, packet, packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN ||
            (0 <= rc && rc < (ssize_t)packet_len)) {
            sftp->fstatvfs_packet = packet;
            return LIBSSH2_ERROR_EAGAIN;
        }

        LIBSSH2_FREE(session, packet);
        sftp->fstatvfs_packet = NULL;

        if(rc < 0) {
            sftp->fstatvfs_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }
        sftp->fstatvfs_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_requirev(sftp, 2, responses, sftp->fstatvfs_request_id,
                              &data, &data_len, 9);

    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return (int)rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP rename packet too short");
    }
    else if(rc) {
        sftp->fstatvfs_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)rc,
                              "Error waiting for FXP EXTENDED REPLY");
    }

    if(data[0] == SSH_FXP_STATUS) {
        uint32_t retcode = _libssh2_ntohu32(data + 5);
        sftp->fstatvfs_state = libssh2_NB_state_idle;
        LIBSSH2_FREE(session, data);
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error");
    }

    if(data_len < 93) {
        LIBSSH2_FREE(session, data);
        sftp->fstatvfs_state = libssh2_NB_state_idle;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error: short response");
    }

    sftp->fstatvfs_state = libssh2_NB_state_idle;

    st->f_bsize = _libssh2_ntohu64(data + 5);
    st->f_frsize = _libssh2_ntohu64(data + 13);
    st->f_blocks = _libssh2_ntohu64(data + 21);
    st->f_bfree = _libssh2_ntohu64(data + 29);
    st->f_bavail = _libssh2_ntohu64(data + 37);
    st->f_files = _libssh2_ntohu64(data + 45);
    st->f_ffree = _libssh2_ntohu64(data + 53);
    st->f_favail = _libssh2_ntohu64(data + 61);
    st->f_fsid = _libssh2_ntohu64(data + 69);
    flag = (unsigned int)_libssh2_ntohu64(data + 77);
    st->f_namemax = _libssh2_ntohu64(data + 85);

    st->f_flag = (flag & SSH_FXE_STATVFS_ST_RDONLY)
        ? LIBSSH2_SFTP_ST_RDONLY : 0;
    st->f_flag |= (flag & SSH_FXE_STATVFS_ST_NOSUID)
        ? LIBSSH2_SFTP_ST_NOSUID : 0;

    LIBSSH2_FREE(session, data);
    return 0;
}


LIBSSH2_API int
libssh2_sftp_fstatvfs(LIBSSH2_SFTP_HANDLE *handle, LIBSSH2_SFTP_STATVFS *st)
{
    int rc;
    if(!handle || !st)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, handle->sftp->channel->session,
                 sftp_fstatvfs(handle, st));
    return rc;
}


static int sftp_statvfs(LIBSSH2_SFTP *sftp, const char *path,
                        unsigned int path_len, LIBSSH2_SFTP_STATVFS *st)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    
    
    uint32_t packet_len = path_len + 19 + 17;
    unsigned char *packet, *s, *data = NULL;
    ssize_t rc;
    unsigned int flag;
    static const unsigned char responses[2] =
        { SSH_FXP_EXTENDED_REPLY, SSH_FXP_STATUS };

    if(sftp->statvfs_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Getting file system statistics of %s", path));
        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_EXTENDED "
                                  "packet");
        }

        _libssh2_store_u32(&s, packet_len - 4);
        *(s++) = SSH_FXP_EXTENDED;
        sftp->statvfs_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->statvfs_request_id);
        _libssh2_store_str(&s, "statvfs@openssh.com", 19);
        _libssh2_store_str(&s, path, path_len);

        sftp->statvfs_state = libssh2_NB_state_created;
    }
    else {
        packet = sftp->statvfs_packet;
    }

    if(sftp->statvfs_state == libssh2_NB_state_created) {
        rc = _libssh2_channel_write(channel, 0, packet, packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN ||
            (0 <= rc && rc < (ssize_t)packet_len)) {
            sftp->statvfs_packet = packet;
            return LIBSSH2_ERROR_EAGAIN;
        }

        LIBSSH2_FREE(session, packet);
        sftp->statvfs_packet = NULL;

        if(rc < 0) {
            sftp->statvfs_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }
        sftp->statvfs_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_requirev(sftp, 2, responses, sftp->statvfs_request_id,
                              &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return (int)rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP fstat packet too short");
    }
    else if(rc) {
        sftp->statvfs_state = libssh2_NB_state_idle;
        return _libssh2_error(session, (int)rc,
                              "Error waiting for FXP EXTENDED REPLY");
    }

    if(data[0] == SSH_FXP_STATUS) {
        uint32_t retcode = _libssh2_ntohu32(data + 5);
        sftp->statvfs_state = libssh2_NB_state_idle;
        LIBSSH2_FREE(session, data);
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error");
    }

    if(data_len < 93) {
        LIBSSH2_FREE(session, data);
        sftp->statvfs_state = libssh2_NB_state_idle;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error: short response");
    }

    sftp->statvfs_state = libssh2_NB_state_idle;

    st->f_bsize = _libssh2_ntohu64(data + 5);
    st->f_frsize = _libssh2_ntohu64(data + 13);
    st->f_blocks = _libssh2_ntohu64(data + 21);
    st->f_bfree = _libssh2_ntohu64(data + 29);
    st->f_bavail = _libssh2_ntohu64(data + 37);
    st->f_files = _libssh2_ntohu64(data + 45);
    st->f_ffree = _libssh2_ntohu64(data + 53);
    st->f_favail = _libssh2_ntohu64(data + 61);
    st->f_fsid = _libssh2_ntohu64(data + 69);
    flag = (unsigned int)_libssh2_ntohu64(data + 77);
    st->f_namemax = _libssh2_ntohu64(data + 85);

    st->f_flag = (flag & SSH_FXE_STATVFS_ST_RDONLY)
        ? LIBSSH2_SFTP_ST_RDONLY : 0;
    st->f_flag |= (flag & SSH_FXE_STATVFS_ST_NOSUID)
        ? LIBSSH2_SFTP_ST_NOSUID : 0;

    LIBSSH2_FREE(session, data);
    return 0;
}


LIBSSH2_API int
libssh2_sftp_statvfs(LIBSSH2_SFTP *sftp, const char *path,
                     size_t path_len, LIBSSH2_SFTP_STATVFS *st)
{
    int rc;
    if(!sftp || !st)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_statvfs(sftp, path, (unsigned int)path_len, st));
    return rc;
}



static int sftp_mkdir(LIBSSH2_SFTP *sftp, const char *path,
                      unsigned int path_len, long mode)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {
        0, 0, 0, 0, 0, 0, 0
    };
    size_t data_len = 0;
    uint32_t retcode;
    ssize_t packet_len;
    unsigned char *packet, *s, *data = NULL;
    int rc;

    if(mode != LIBSSH2_SFTP_DEFAULT_MODE) {
        
        attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
        attrs.permissions = mode | LIBSSH2_SFTP_ATTR_PFILETYPE_DIR;
    }

    
    packet_len = path_len + 13 + sftp_attrsize(attrs.flags);

    if(sftp->mkdir_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP,
                       "Creating directory %s with mode 0%lo", path, mode));
        s = packet = LIBSSH2_ALLOC(session, packet_len);
        if(!packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_MKDIR "
                                  "packet");
        }

        _libssh2_store_u32(&s, (uint32_t)(packet_len - 4));
        *(s++) = SSH_FXP_MKDIR;
        sftp->mkdir_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->mkdir_request_id);
        _libssh2_store_str(&s, path, path_len);

        s += sftp_attr2bin(s, &attrs);

        sftp->mkdir_state = libssh2_NB_state_created;
    }
    else {
        packet = sftp->mkdir_packet;
    }

    if(sftp->mkdir_state == libssh2_NB_state_created) {
        ssize_t nwritten;
        nwritten = _libssh2_channel_write(channel, 0, packet, packet_len);
        if(nwritten == LIBSSH2_ERROR_EAGAIN) {
            sftp->mkdir_packet = packet;
            return (int)nwritten;
        }
        if(packet_len != nwritten) {
            LIBSSH2_FREE(session, packet);
            sftp->mkdir_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "_libssh2_channel_write() failed");
        }
        LIBSSH2_FREE(session, packet);
        sftp->mkdir_state = libssh2_NB_state_sent;
        sftp->mkdir_packet = NULL;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS, sftp->mkdir_request_id,
                             &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP mkdir packet too short");
    }
    else if(rc) {
        sftp->mkdir_state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc,
                              "Error waiting for FXP STATUS");
    }

    sftp->mkdir_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    if(retcode == LIBSSH2_FX_OK) {
        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "OK"));
        return 0;
    }
    else {
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error");
    }
}


LIBSSH2_API int
libssh2_sftp_mkdir_ex(LIBSSH2_SFTP *sftp, const char *path,
                      unsigned int path_len, long mode)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_mkdir(sftp, path, path_len, mode));
    return rc;
}


static int sftp_rmdir(LIBSSH2_SFTP *sftp, const char *path,
                      unsigned int path_len)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    uint32_t retcode;
    
    ssize_t packet_len = path_len + 13;
    unsigned char *s, *data = NULL;
    int rc;

    if(sftp->rmdir_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "Removing directory: %s",
                       path));
        s = sftp->rmdir_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->rmdir_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_RMDIR "
                                  "packet");
        }

        _libssh2_store_u32(&s, (uint32_t)(packet_len - 4));
        *(s++) = SSH_FXP_RMDIR;
        sftp->rmdir_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->rmdir_request_id);
        _libssh2_store_str(&s, path, path_len);

        sftp->rmdir_state = libssh2_NB_state_created;
    }

    if(sftp->rmdir_state == libssh2_NB_state_created) {
        ssize_t nwritten;
        nwritten = _libssh2_channel_write(channel, 0, sftp->rmdir_packet,
                                          packet_len);
        if(nwritten == LIBSSH2_ERROR_EAGAIN) {
            return (int)nwritten;
        }
        else if(packet_len != nwritten) {
            LIBSSH2_FREE(session, sftp->rmdir_packet);
            sftp->rmdir_packet = NULL;
            sftp->rmdir_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send FXP_RMDIR command");
        }
        LIBSSH2_FREE(session, sftp->rmdir_packet);
        sftp->rmdir_packet = NULL;

        sftp->rmdir_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_require(sftp, SSH_FXP_STATUS,
                             sftp->rmdir_request_id, &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN) {
        return rc;
    }
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP rmdir packet too short");
    }
    else if(rc) {
        sftp->rmdir_state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc,
                              "Error waiting for FXP STATUS");
    }

    sftp->rmdir_state = libssh2_NB_state_idle;

    retcode = _libssh2_ntohu32(data + 5);
    LIBSSH2_FREE(session, data);

    if(retcode == LIBSSH2_FX_OK) {
        return 0;
    }
    else {
        sftp->last_errno = retcode;
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP Protocol Error");
    }
}


LIBSSH2_API int
libssh2_sftp_rmdir_ex(LIBSSH2_SFTP *sftp, const char *path,
                      unsigned int path_len)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_rmdir(sftp, path, path_len));
    return rc;
}


static int sftp_stat(LIBSSH2_SFTP *sftp, const char *path,
                     unsigned int path_len, int stat_type,
                     LIBSSH2_SFTP_ATTRIBUTES * attrs)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0;
    
    ssize_t packet_len =
        path_len + 13 +
        ((stat_type ==
          LIBSSH2_SFTP_SETSTAT) ? sftp_attrsize(attrs->flags) : 0);
    unsigned char *s, *data = NULL;
    static const unsigned char stat_responses[2] =
        { SSH_FXP_ATTRS, SSH_FXP_STATUS };
    int rc;

    if(sftp->stat_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "%s %s",
                       (stat_type == LIBSSH2_SFTP_SETSTAT) ? "Set-statting" :
                       (stat_type ==
                        LIBSSH2_SFTP_LSTAT ? "LStatting" : "Statting"), path));
        s = sftp->stat_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->stat_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for FXP_*STAT "
                                  "packet");
        }

        _libssh2_store_u32(&s, (uint32_t)(packet_len - 4));

        switch(stat_type) {
        case LIBSSH2_SFTP_SETSTAT:
            *(s++) = SSH_FXP_SETSTAT;
            break;

        case LIBSSH2_SFTP_LSTAT:
            *(s++) = SSH_FXP_LSTAT;
            break;

        case LIBSSH2_SFTP_STAT:
        default:
            *(s++) = SSH_FXP_STAT;
        }
        sftp->stat_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->stat_request_id);
        _libssh2_store_str(&s, path, path_len);

        if(stat_type == LIBSSH2_SFTP_SETSTAT)
            s += sftp_attr2bin(s, attrs);

        sftp->stat_state = libssh2_NB_state_created;
    }

    if(sftp->stat_state == libssh2_NB_state_created) {
        ssize_t nwritten;
        nwritten = _libssh2_channel_write(channel, 0,
                                          sftp->stat_packet, packet_len);
        if(nwritten == LIBSSH2_ERROR_EAGAIN) {
            return (int)nwritten;
        }
        else if(packet_len != nwritten) {
            LIBSSH2_FREE(session, sftp->stat_packet);
            sftp->stat_packet = NULL;
            sftp->stat_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send STAT/LSTAT/SETSTAT command");
        }
        LIBSSH2_FREE(session, sftp->stat_packet);
        sftp->stat_packet = NULL;

        sftp->stat_state = libssh2_NB_state_sent;
    }

    rc = sftp_packet_requirev(sftp, 2, stat_responses,
                              sftp->stat_request_id, &data, &data_len, 9);
    if(rc == LIBSSH2_ERROR_EAGAIN)
        return rc;
    else if(rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP stat packet too short");
    }
    else if(rc) {
        sftp->stat_state = libssh2_NB_state_idle;
        return _libssh2_error(session, rc,
                              "Timeout waiting for status message");
    }

    sftp->stat_state = libssh2_NB_state_idle;

    if(data[0] == SSH_FXP_STATUS) {
        uint32_t retcode;

        retcode = _libssh2_ntohu32(data + 5);
        LIBSSH2_FREE(session, data);
        if(retcode == LIBSSH2_FX_OK) {
            memset(attrs, 0, sizeof(LIBSSH2_SFTP_ATTRIBUTES));
            return 0;
        }
        else {
            sftp->last_errno = retcode;
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "SFTP Protocol Error");
        }
    }

    memset(attrs, 0, sizeof(LIBSSH2_SFTP_ATTRIBUTES));
    if(sftp_bin2attr(attrs, data + 5, data_len - 5) < 0) {
        LIBSSH2_FREE(session, data);
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "Attributes too short in SFTP fstat");
    }

    LIBSSH2_FREE(session, data);

    return 0;
}


LIBSSH2_API int
libssh2_sftp_stat_ex(LIBSSH2_SFTP *sftp, const char *path,
                     unsigned int path_len, int stat_type,
                     LIBSSH2_SFTP_ATTRIBUTES *attrs)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_stat(sftp, path, path_len, stat_type, attrs));
    return rc;
}


static int sftp_symlink(LIBSSH2_SFTP *sftp, const char *path,
                        unsigned int path_len, char *target,
                        unsigned int target_len, int link_type)
{
    LIBSSH2_CHANNEL *channel = sftp->channel;
    LIBSSH2_SESSION *session = channel->session;
    size_t data_len = 0, link_len;
    
    ssize_t packet_len =
        path_len + 13 +
        ((link_type == LIBSSH2_SFTP_SYMLINK) ? (4 + target_len) : 0);
    unsigned char *s, *data = NULL;
    static const unsigned char link_responses[2] =
        { SSH_FXP_NAME, SSH_FXP_STATUS };
    int retcode;

    if(sftp->symlink_state == libssh2_NB_state_idle) {
        sftp->last_errno = LIBSSH2_FX_OK;

        if((sftp->version < 3) && (link_type != LIBSSH2_SFTP_REALPATH)) {
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "Server does not support SYMLINK or"
                                  " READLINK");
        }

        s = sftp->symlink_packet = LIBSSH2_ALLOC(session, packet_len);
        if(!sftp->symlink_packet) {
            return _libssh2_error(session, LIBSSH2_ERROR_ALLOC,
                                  "Unable to allocate memory for "
                                  "SYMLINK/READLINK/REALPATH packet");
        }

        _libssh2_debug((session, LIBSSH2_TRACE_SFTP, "%s %s on %s",
                       (link_type ==
                        LIBSSH2_SFTP_SYMLINK) ? "Creating" : "Reading",
                       (link_type ==
                        LIBSSH2_SFTP_REALPATH) ? "realpath" : "symlink",
                       path));

        _libssh2_store_u32(&s, (uint32_t)(packet_len - 4));

        switch(link_type) {
        case LIBSSH2_SFTP_REALPATH:
            *(s++) = SSH_FXP_REALPATH;
            break;

        case LIBSSH2_SFTP_SYMLINK:
            *(s++) = SSH_FXP_SYMLINK;
            break;

        case LIBSSH2_SFTP_READLINK:
        default:
            *(s++) = SSH_FXP_READLINK;
        }
        sftp->symlink_request_id = sftp->request_id++;
        _libssh2_store_u32(&s, sftp->symlink_request_id);
        _libssh2_store_str(&s, path, path_len);

        if(link_type == LIBSSH2_SFTP_SYMLINK)
            _libssh2_store_str(&s, target, target_len);

        sftp->symlink_state = libssh2_NB_state_created;
    }

    if(sftp->symlink_state == libssh2_NB_state_created) {
        ssize_t rc = _libssh2_channel_write(channel, 0, sftp->symlink_packet,
                                            packet_len);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return (int)rc;
        else if(packet_len != rc) {
            LIBSSH2_FREE(session, sftp->symlink_packet);
            sftp->symlink_packet = NULL;
            sftp->symlink_state = libssh2_NB_state_idle;
            return _libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "Unable to send SYMLINK/READLINK command");
        }
        LIBSSH2_FREE(session, sftp->symlink_packet);
        sftp->symlink_packet = NULL;

        sftp->symlink_state = libssh2_NB_state_sent;
    }

    retcode = sftp_packet_requirev(sftp, 2, link_responses,
                                   sftp->symlink_request_id, &data,
                                   &data_len, 9);
    if(retcode == LIBSSH2_ERROR_EAGAIN)
        return retcode;
    else if(retcode == LIBSSH2_ERROR_BUFFER_TOO_SMALL) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP symlink packet too short");
    }
    else if(retcode) {
        sftp->symlink_state = libssh2_NB_state_idle;
        return _libssh2_error(session, retcode,
                              "Error waiting for status message");
    }

    sftp->symlink_state = libssh2_NB_state_idle;

    if(data[0] == SSH_FXP_STATUS) {
        retcode = _libssh2_ntohu32(data + 5);
        LIBSSH2_FREE(session, data);
        if(retcode == LIBSSH2_FX_OK)
            return LIBSSH2_ERROR_NONE;
        else {
            sftp->last_errno = retcode;
            return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                                  "SFTP Protocol Error");
        }
    }

    if(_libssh2_ntohu32(data + 5) < 1) {
        LIBSSH2_FREE(session, data);
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "Invalid READLINK/REALPATH response, "
                              "no name entries");
    }

    if(data_len < 13) {
        if(data_len > 0) {
            LIBSSH2_FREE(session, data);
        }
        return _libssh2_error(session, LIBSSH2_ERROR_SFTP_PROTOCOL,
                              "SFTP stat packet too short");
    }

    
    link_len = _libssh2_ntohu32(data + 9);
    if(link_len < target_len) {
        memcpy(target, data + 13, link_len);
        target[link_len] = 0;
        retcode = (int)link_len;
    }
    else
        retcode = LIBSSH2_ERROR_BUFFER_TOO_SMALL;
    LIBSSH2_FREE(session, data);

    return retcode;
}


LIBSSH2_API int
libssh2_sftp_symlink_ex(LIBSSH2_SFTP *sftp, const char *path,
                        unsigned int path_len, char *target,
                        unsigned int target_len, int link_type)
{
    int rc;
    if(!sftp)
        return LIBSSH2_ERROR_BAD_USE;
    BLOCK_ADJUST(rc, sftp->channel->session,
                 sftp_symlink(sftp, path, path_len, target, target_len,
                              link_type));
    return rc;
}


LIBSSH2_API unsigned long
libssh2_sftp_last_error(LIBSSH2_SFTP *sftp)
{
    if(!sftp)
        return 0;

    return sftp->last_errno;
}


LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_sftp_get_channel(LIBSSH2_SFTP *sftp)
{
    if(!sftp)
        return NULL;

    return sftp->channel;
}
