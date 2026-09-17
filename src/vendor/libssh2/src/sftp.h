#ifndef LIBSSH2_SFTP_PRIV_H
#define LIBSSH2_SFTP_PRIV_H



#define MAX_SFTP_OUTGOING_SIZE 30000


#define MAX_SFTP_READ_SIZE 30000

struct sftp_pipeline_chunk {
    struct list_node node;
    libssh2_uint64_t offset; 
    size_t len; 
    size_t sent;
    ssize_t lefttosend; 
    uint32_t request_id;
    unsigned char packet[1]; 
};

struct sftp_zombie_requests {
    struct list_node node;
    uint32_t request_id;
};

struct _LIBSSH2_SFTP_PACKET
{
    struct list_node node;   
    uint32_t request_id;
    unsigned char *data;
    size_t data_len;              
};

typedef struct _LIBSSH2_SFTP_PACKET LIBSSH2_SFTP_PACKET;


#define SFTP_HANDLE_MAXLEN 4092 

struct _LIBSSH2_SFTP_HANDLE
{
    struct list_node node;

    LIBSSH2_SFTP *sftp;

    char handle[SFTP_HANDLE_MAXLEN];
    size_t handle_len;

    enum {
        LIBSSH2_SFTP_HANDLE_FILE,
        LIBSSH2_SFTP_HANDLE_DIR
    } handle_type;

    union _libssh2_sftp_handle_data
    {
        struct _libssh2_sftp_handle_file_data
        {
            libssh2_uint64_t offset;
            libssh2_uint64_t offset_sent;
            size_t acked; 

            
            unsigned char *data;
            size_t data_len;
            size_t data_left;

            char eof; 
        } file;
        struct _libssh2_sftp_handle_dir_data
        {
            uint32_t names_left;
            void *names_packet;
            char *next_name;
            size_t names_packet_len;
        } dir;
    } u;

    
    libssh2_nonblocking_states close_state;
    uint32_t close_request_id;
    unsigned char *close_packet;

    
    struct list_head packet_list;

};

struct _LIBSSH2_SFTP
{
    LIBSSH2_CHANNEL *channel;

    uint32_t request_id, version, posix_rename_version;

    struct list_head packets;

    
    struct list_head zombie_requests;

    
    struct list_head sftp_handles;

    uint32_t last_errno;

    
    unsigned char packet_header[9];
    
    size_t packet_header_len;           
    unsigned char *partial_packet;      
    uint32_t partial_len;               
    size_t partial_received;            

    
    time_t requirev_start;

    
    libssh2_nonblocking_states open_state;
    unsigned char *open_packet;
    uint32_t open_packet_len; 
    size_t open_packet_sent;
    uint32_t open_request_id;

    
    libssh2_nonblocking_states read_state;

    
    libssh2_nonblocking_states packet_state;

    
    libssh2_nonblocking_states write_state;

    
    libssh2_nonblocking_states fsync_state;
    unsigned char *fsync_packet;
    uint32_t fsync_request_id;

    
    libssh2_nonblocking_states readdir_state;
    unsigned char *readdir_packet;
    uint32_t readdir_request_id;

    
    libssh2_nonblocking_states fstat_state;
    unsigned char *fstat_packet;
    uint32_t fstat_request_id;

    
    libssh2_nonblocking_states unlink_state;
    unsigned char *unlink_packet;
    uint32_t unlink_request_id;

    
    libssh2_nonblocking_states rename_state;
    unsigned char *rename_packet;
    unsigned char *rename_s;
    uint32_t rename_request_id;

    
    libssh2_nonblocking_states posix_rename_state;
    unsigned char *posix_rename_packet;
    uint32_t posix_rename_request_id;

    
    libssh2_nonblocking_states fstatvfs_state;
    unsigned char *fstatvfs_packet;
    uint32_t fstatvfs_request_id;

    
    libssh2_nonblocking_states statvfs_state;
    unsigned char *statvfs_packet;
    uint32_t statvfs_request_id;

    
    libssh2_nonblocking_states mkdir_state;
    unsigned char *mkdir_packet;
    uint32_t mkdir_request_id;

    
    libssh2_nonblocking_states rmdir_state;
    unsigned char *rmdir_packet;
    uint32_t rmdir_request_id;

    
    libssh2_nonblocking_states stat_state;
    unsigned char *stat_packet;
    uint32_t stat_request_id;

    
    libssh2_nonblocking_states symlink_state;
    unsigned char *symlink_packet;
    uint32_t symlink_request_id;
};

#endif 
