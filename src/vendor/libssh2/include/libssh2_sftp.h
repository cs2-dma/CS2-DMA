

#ifndef LIBSSH2_SFTP_H
#define LIBSSH2_SFTP_H 1

#include "libssh2.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif


#define LIBSSH2_SFTP_VERSION        3

typedef struct _LIBSSH2_SFTP                LIBSSH2_SFTP;
typedef struct _LIBSSH2_SFTP_HANDLE         LIBSSH2_SFTP_HANDLE;
typedef struct _LIBSSH2_SFTP_ATTRIBUTES     LIBSSH2_SFTP_ATTRIBUTES;
typedef struct _LIBSSH2_SFTP_STATVFS        LIBSSH2_SFTP_STATVFS;


#define LIBSSH2_SFTP_OPENFILE           0
#define LIBSSH2_SFTP_OPENDIR            1


#define LIBSSH2_SFTP_RENAME_OVERWRITE   0x00000001
#define LIBSSH2_SFTP_RENAME_ATOMIC      0x00000002
#define LIBSSH2_SFTP_RENAME_NATIVE      0x00000004


#define LIBSSH2_SFTP_STAT               0
#define LIBSSH2_SFTP_LSTAT              1
#define LIBSSH2_SFTP_SETSTAT            2


#define LIBSSH2_SFTP_SYMLINK            0
#define LIBSSH2_SFTP_READLINK           1
#define LIBSSH2_SFTP_REALPATH           2


#define LIBSSH2_SFTP_DEFAULT_MODE      -1


#define LIBSSH2_SFTP_ATTR_SIZE              0x00000001
#define LIBSSH2_SFTP_ATTR_UIDGID            0x00000002
#define LIBSSH2_SFTP_ATTR_PERMISSIONS       0x00000004
#define LIBSSH2_SFTP_ATTR_ACMODTIME         0x00000008
#define LIBSSH2_SFTP_ATTR_EXTENDED          0x80000000


#define LIBSSH2_SFTP_ST_RDONLY              0x00000001
#define LIBSSH2_SFTP_ST_NOSUID              0x00000002

struct _LIBSSH2_SFTP_ATTRIBUTES {
    
    unsigned long flags;

    libssh2_uint64_t filesize;
    unsigned long uid, gid;
    unsigned long permissions;
    unsigned long atime, mtime;
};

struct _LIBSSH2_SFTP_STATVFS {
    libssh2_uint64_t  f_bsize;    
    libssh2_uint64_t  f_frsize;   
    libssh2_uint64_t  f_blocks;   
    libssh2_uint64_t  f_bfree;    
    libssh2_uint64_t  f_bavail;   
    libssh2_uint64_t  f_files;    
    libssh2_uint64_t  f_ffree;    
    libssh2_uint64_t  f_favail;   
    libssh2_uint64_t  f_fsid;     
    libssh2_uint64_t  f_flag;     
    libssh2_uint64_t  f_namemax;  
};


#define LIBSSH2_SFTP_TYPE_REGULAR           1
#define LIBSSH2_SFTP_TYPE_DIRECTORY         2
#define LIBSSH2_SFTP_TYPE_SYMLINK           3
#define LIBSSH2_SFTP_TYPE_SPECIAL           4
#define LIBSSH2_SFTP_TYPE_UNKNOWN           5
#define LIBSSH2_SFTP_TYPE_SOCKET            6
#define LIBSSH2_SFTP_TYPE_CHAR_DEVICE       7
#define LIBSSH2_SFTP_TYPE_BLOCK_DEVICE      8
#define LIBSSH2_SFTP_TYPE_FIFO              9



#define LIBSSH2_SFTP_S_IFMT         0170000     
#define LIBSSH2_SFTP_S_IFIFO        0010000     
#define LIBSSH2_SFTP_S_IFCHR        0020000     
#define LIBSSH2_SFTP_S_IFDIR        0040000     
#define LIBSSH2_SFTP_S_IFBLK        0060000     
#define LIBSSH2_SFTP_S_IFREG        0100000     
#define LIBSSH2_SFTP_S_IFLNK        0120000     
#define LIBSSH2_SFTP_S_IFSOCK       0140000     



#define LIBSSH2_SFTP_S_IRWXU        0000700     
#define LIBSSH2_SFTP_S_IRUSR        0000400     
#define LIBSSH2_SFTP_S_IWUSR        0000200     
#define LIBSSH2_SFTP_S_IXUSR        0000100     

#define LIBSSH2_SFTP_S_IRWXG        0000070     
#define LIBSSH2_SFTP_S_IRGRP        0000040     
#define LIBSSH2_SFTP_S_IWGRP        0000020     
#define LIBSSH2_SFTP_S_IXGRP        0000010     

#define LIBSSH2_SFTP_S_IRWXO        0000007     
#define LIBSSH2_SFTP_S_IROTH        0000004     
#define LIBSSH2_SFTP_S_IWOTH        0000002     
#define LIBSSH2_SFTP_S_IXOTH        0000001     


#define LIBSSH2_SFTP_S_ISLNK(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFLNK)
#define LIBSSH2_SFTP_S_ISREG(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFREG)
#define LIBSSH2_SFTP_S_ISDIR(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR)
#define LIBSSH2_SFTP_S_ISCHR(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFCHR)
#define LIBSSH2_SFTP_S_ISBLK(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFBLK)
#define LIBSSH2_SFTP_S_ISFIFO(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFIFO)
#define LIBSSH2_SFTP_S_ISSOCK(m) \
    (((m) & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFSOCK)


#define LIBSSH2_FXF_READ                        0x00000001
#define LIBSSH2_FXF_WRITE                       0x00000002
#define LIBSSH2_FXF_APPEND                      0x00000004
#define LIBSSH2_FXF_CREAT                       0x00000008
#define LIBSSH2_FXF_TRUNC                       0x00000010
#define LIBSSH2_FXF_EXCL                        0x00000020


#define LIBSSH2_FX_OK                       0UL
#define LIBSSH2_FX_EOF                      1UL
#define LIBSSH2_FX_NO_SUCH_FILE             2UL
#define LIBSSH2_FX_PERMISSION_DENIED        3UL
#define LIBSSH2_FX_FAILURE                  4UL
#define LIBSSH2_FX_BAD_MESSAGE              5UL
#define LIBSSH2_FX_NO_CONNECTION            6UL
#define LIBSSH2_FX_CONNECTION_LOST          7UL
#define LIBSSH2_FX_OP_UNSUPPORTED           8UL
#define LIBSSH2_FX_INVALID_HANDLE           9UL
#define LIBSSH2_FX_NO_SUCH_PATH             10UL
#define LIBSSH2_FX_FILE_ALREADY_EXISTS      11UL
#define LIBSSH2_FX_WRITE_PROTECT            12UL
#define LIBSSH2_FX_NO_MEDIA                 13UL
#define LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM   14UL
#define LIBSSH2_FX_QUOTA_EXCEEDED           15UL
#define LIBSSH2_FX_UNKNOWN_PRINCIPLE        16UL 
#define LIBSSH2_FX_UNKNOWN_PRINCIPAL        16UL
#define LIBSSH2_FX_LOCK_CONFlICT            17UL 
#define LIBSSH2_FX_LOCK_CONFLICT            17UL
#define LIBSSH2_FX_DIR_NOT_EMPTY            18UL
#define LIBSSH2_FX_NOT_A_DIRECTORY          19UL
#define LIBSSH2_FX_INVALID_FILENAME         20UL
#define LIBSSH2_FX_LINK_LOOP                21UL


#define LIBSSH2SFTP_EAGAIN LIBSSH2_ERROR_EAGAIN


LIBSSH2_API LIBSSH2_SFTP *libssh2_sftp_init(LIBSSH2_SESSION *session);
LIBSSH2_API int libssh2_sftp_shutdown(LIBSSH2_SFTP *sftp);
LIBSSH2_API unsigned long libssh2_sftp_last_error(LIBSSH2_SFTP *sftp);
LIBSSH2_API LIBSSH2_CHANNEL *libssh2_sftp_get_channel(LIBSSH2_SFTP *sftp);


LIBSSH2_API LIBSSH2_SFTP_HANDLE *
libssh2_sftp_open_ex(LIBSSH2_SFTP *sftp,
                     const char *filename,
                     unsigned int filename_len,
                     unsigned long flags,
                     long mode, int open_type);
#define libssh2_sftp_open(sftp, filename, flags, mode) \
    libssh2_sftp_open_ex((sftp), \
                         (filename), (unsigned int)strlen(filename), \
                         (flags), (mode), LIBSSH2_SFTP_OPENFILE)
#define libssh2_sftp_opendir(sftp, path) \
    libssh2_sftp_open_ex((sftp), \
                         (path), (unsigned int)strlen(path), \
                         0, 0, LIBSSH2_SFTP_OPENDIR)
LIBSSH2_API LIBSSH2_SFTP_HANDLE *
libssh2_sftp_open_ex_r(LIBSSH2_SFTP *sftp,
                       const char *filename,
                       size_t filename_len,
                       unsigned long flags,
                       long mode, int open_type,
                       LIBSSH2_SFTP_ATTRIBUTES *attrs);
#define libssh2_sftp_open_r(sftp, filename, flags, mode, attrs) \
    libssh2_sftp_open_ex_r((sftp), (filename), strlen(filename), \
                           (flags), (mode), LIBSSH2_SFTP_OPENFILE, \
                           (attrs))

LIBSSH2_API ssize_t libssh2_sftp_read(LIBSSH2_SFTP_HANDLE *handle,
                                      char *buffer, size_t buffer_maxlen);

LIBSSH2_API int libssh2_sftp_readdir_ex(LIBSSH2_SFTP_HANDLE *handle, \
                                        char *buffer, size_t buffer_maxlen,
                                        char *longentry,
                                        size_t longentry_maxlen,
                                        LIBSSH2_SFTP_ATTRIBUTES *attrs);
#define libssh2_sftp_readdir(handle, buffer, buffer_maxlen, attrs) \
    libssh2_sftp_readdir_ex((handle), (buffer), (buffer_maxlen), NULL, 0, \
                            (attrs))

LIBSSH2_API ssize_t libssh2_sftp_write(LIBSSH2_SFTP_HANDLE *handle,
                                       const char *buffer, size_t count);
LIBSSH2_API int libssh2_sftp_fsync(LIBSSH2_SFTP_HANDLE *handle);

LIBSSH2_API int libssh2_sftp_close_handle(LIBSSH2_SFTP_HANDLE *handle);
#define libssh2_sftp_close(handle) libssh2_sftp_close_handle(handle)
#define libssh2_sftp_closedir(handle) libssh2_sftp_close_handle(handle)

LIBSSH2_API void libssh2_sftp_seek(LIBSSH2_SFTP_HANDLE *handle, size_t offset);
LIBSSH2_API void libssh2_sftp_seek64(LIBSSH2_SFTP_HANDLE *handle,
                                     libssh2_uint64_t offset);
#define libssh2_sftp_rewind(handle) libssh2_sftp_seek64((handle), 0)

LIBSSH2_API size_t libssh2_sftp_tell(LIBSSH2_SFTP_HANDLE *handle);
LIBSSH2_API libssh2_uint64_t libssh2_sftp_tell64(LIBSSH2_SFTP_HANDLE *handle);

LIBSSH2_API int libssh2_sftp_fstat_ex(LIBSSH2_SFTP_HANDLE *handle,
                                      LIBSSH2_SFTP_ATTRIBUTES *attrs,
                                      int setstat);
#define libssh2_sftp_fstat(handle, attrs) \
    libssh2_sftp_fstat_ex((handle), (attrs), 0)
#define libssh2_sftp_fsetstat(handle, attrs) \
    libssh2_sftp_fstat_ex((handle), (attrs), 1)


LIBSSH2_API int libssh2_sftp_rename_ex(LIBSSH2_SFTP *sftp,
                                       const char *source_filename,
                                       unsigned int srouce_filename_len,
                                       const char *dest_filename,
                                       unsigned int dest_filename_len,
                                       long flags);
#define libssh2_sftp_rename(sftp, sourcefile, destfile) \
    libssh2_sftp_rename_ex((sftp), \
                           (sourcefile), (unsigned int)strlen(sourcefile), \
                           (destfile), (unsigned int)strlen(destfile), \
                           LIBSSH2_SFTP_RENAME_OVERWRITE | \
                           LIBSSH2_SFTP_RENAME_ATOMIC | \
                           LIBSSH2_SFTP_RENAME_NATIVE)

LIBSSH2_API int libssh2_sftp_posix_rename_ex(LIBSSH2_SFTP *sftp,
                                             const char *source_filename,
                                             size_t srouce_filename_len,
                                             const char *dest_filename,
                                             size_t dest_filename_len);
#define libssh2_sftp_posix_rename(sftp, sourcefile, destfile) \
    libssh2_sftp_posix_rename_ex((sftp), (sourcefile), strlen(sourcefile), \
                                 (destfile), strlen(destfile))

LIBSSH2_API int libssh2_sftp_unlink_ex(LIBSSH2_SFTP *sftp,
                                       const char *filename,
                                       unsigned int filename_len);
#define libssh2_sftp_unlink(sftp, filename) \
    libssh2_sftp_unlink_ex((sftp), (filename), (unsigned int)strlen(filename))

LIBSSH2_API int libssh2_sftp_fstatvfs(LIBSSH2_SFTP_HANDLE *handle,
                                      LIBSSH2_SFTP_STATVFS *st);

LIBSSH2_API int libssh2_sftp_statvfs(LIBSSH2_SFTP *sftp,
                                     const char *path,
                                     size_t path_len,
                                     LIBSSH2_SFTP_STATVFS *st);

LIBSSH2_API int libssh2_sftp_mkdir_ex(LIBSSH2_SFTP *sftp,
                                      const char *path,
                                      unsigned int path_len, long mode);
#define libssh2_sftp_mkdir(sftp, path, mode) \
    libssh2_sftp_mkdir_ex((sftp), (path), (unsigned int)strlen(path), (mode))

LIBSSH2_API int libssh2_sftp_rmdir_ex(LIBSSH2_SFTP *sftp,
                                      const char *path,
                                      unsigned int path_len);
#define libssh2_sftp_rmdir(sftp, path) \
    libssh2_sftp_rmdir_ex((sftp), (path), (unsigned int)strlen(path))

LIBSSH2_API int libssh2_sftp_stat_ex(LIBSSH2_SFTP *sftp,
                                     const char *path,
                                     unsigned int path_len,
                                     int stat_type,
                                     LIBSSH2_SFTP_ATTRIBUTES *attrs);
#define libssh2_sftp_stat(sftp, path, attrs) \
    libssh2_sftp_stat_ex((sftp), (path), (unsigned int)strlen(path), \
                         LIBSSH2_SFTP_STAT, (attrs))
#define libssh2_sftp_lstat(sftp, path, attrs) \
    libssh2_sftp_stat_ex((sftp), (path), (unsigned int)strlen(path), \
                         LIBSSH2_SFTP_LSTAT, (attrs))
#define libssh2_sftp_setstat(sftp, path, attrs) \
    libssh2_sftp_stat_ex((sftp), (path), (unsigned int)strlen(path), \
                         LIBSSH2_SFTP_SETSTAT, (attrs))

LIBSSH2_API int libssh2_sftp_symlink_ex(LIBSSH2_SFTP *sftp,
                                        const char *path,
                                        unsigned int path_len,
                                        char *target,
                                        unsigned int target_len,
                                        int link_type);
#define libssh2_sftp_symlink(sftp, orig, linkpath) \
    libssh2_sftp_symlink_ex((sftp), \
                            (orig), (unsigned int)strlen(orig), \
                            (linkpath), (unsigned int)strlen(linkpath), \
                            LIBSSH2_SFTP_SYMLINK)
#define libssh2_sftp_readlink(sftp, path, target, maxlen) \
    libssh2_sftp_symlink_ex((sftp), \
                            (path), (unsigned int)strlen(path), \
                            (target), (maxlen), \
                            LIBSSH2_SFTP_READLINK)
#define libssh2_sftp_realpath(sftp, path, target, maxlen) \
    libssh2_sftp_symlink_ex((sftp), \
                            (path), (unsigned int)strlen(path), \
                            (target), (maxlen), \
                            LIBSSH2_SFTP_REALPATH)

#ifdef __cplusplus
} 
#endif

#endif 
