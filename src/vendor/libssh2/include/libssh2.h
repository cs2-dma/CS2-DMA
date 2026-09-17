

#ifndef LIBSSH2_H
#define LIBSSH2_H 1

#define LIBSSH2_COPYRIGHT "The libssh2 project and its contributors."


#define LIBSSH2_VERSION "1.11.1"


#define LIBSSH2_VERSION_MAJOR 1
#define LIBSSH2_VERSION_MINOR 11
#define LIBSSH2_VERSION_PATCH 1


#define LIBSSH2_VERSION_NUM 0x010b01


#define LIBSSH2_TIMESTAMP "Wed Oct 16 08:03:21 UTC 2024"

#ifndef RC_INVOKED

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
# include <basetsd.h>
# include <winsock2.h>
#endif

#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>


#ifndef LIBSSH2_API
# ifdef _WIN32
#  if defined(LIBSSH2_EXPORTS) || defined(_WINDLL)
#   ifdef LIBSSH2_LIBRARY
#    define LIBSSH2_API __declspec(dllexport)
#   else
#    define LIBSSH2_API __declspec(dllimport)
#   endif 
#  else
#   define LIBSSH2_API
#  endif
# else 
#  define LIBSSH2_API
# endif 
#endif 

#ifdef HAVE_SYS_UIO_H
# include <sys/uio.h>
#endif

#ifdef _MSC_VER
typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef unsigned __int64 libssh2_uint64_t;
typedef __int64 libssh2_int64_t;
#if (!defined(HAVE_SSIZE_T) && !defined(ssize_t))
typedef SSIZE_T ssize_t;
#define HAVE_SSIZE_T
#endif
#else
#include <stdint.h>
typedef unsigned long long libssh2_uint64_t;
typedef long long libssh2_int64_t;
#endif

#ifdef _WIN32
typedef SOCKET libssh2_socket_t;
#define LIBSSH2_INVALID_SOCKET INVALID_SOCKET
#define LIBSSH2_SOCKET_CLOSE(s) closesocket(s)
#else 
typedef int libssh2_socket_t;
#define LIBSSH2_INVALID_SOCKET -1
#define LIBSSH2_SOCKET_CLOSE(s) close(s)
#endif 


#if !defined(LIBSSH2_DISABLE_DEPRECATION) && !defined(LIBSSH2_LIBRARY)
#  if defined(_MSC_VER)
#    if _MSC_VER >= 1400
#      define LIBSSH2_DEPRECATED(version, message) \
         __declspec(deprecated("since libssh2 " # version ". " message))
#    elif _MSC_VER >= 1310
#      define LIBSSH2_DEPRECATED(version, message) \
         __declspec(deprecated)
#   endif
#  elif defined(__GNUC__) && !defined(__INTEL_COMPILER)
#    if (defined(__clang__) && __clang_major__ >= 3) || \
        (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5))
#      define LIBSSH2_DEPRECATED(version, message) \
         __attribute__((deprecated("since libssh2 " # version ". " message)))
#    elif __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 0)
#      define LIBSSH2_DEPRECATED(version, message) \
         __attribute__((deprecated))
#    endif
#  elif defined(__SUNPRO_C) && __SUNPRO_C >= 0x5130
#    define LIBSSH2_DEPRECATED(version, message) \
       __attribute__((deprecated))
#  endif
#endif

#ifndef LIBSSH2_DEPRECATED
#define LIBSSH2_DEPRECATED(version, message)
#endif



#if defined(_MSC_VER) && !defined(_WIN32_WCE)
#  if (_MSC_VER >= 900) && (_INTEGRAL_MAX_BITS >= 64)
#    define LIBSSH2_USE_WIN32_LARGE_FILES
#  else
#    define LIBSSH2_USE_WIN32_SMALL_FILES
#  endif
#endif

#if defined(__MINGW32__) && !defined(LIBSSH2_USE_WIN32_LARGE_FILES)
#  define LIBSSH2_USE_WIN32_LARGE_FILES
#endif

#if defined(__WATCOMC__) && !defined(LIBSSH2_USE_WIN32_LARGE_FILES)
#  define LIBSSH2_USE_WIN32_LARGE_FILES
#endif

#if defined(__POCC__)
#  undef LIBSSH2_USE_WIN32_LARGE_FILES
#endif

#if defined(_WIN32) && !defined(LIBSSH2_USE_WIN32_LARGE_FILES) && \
    !defined(LIBSSH2_USE_WIN32_SMALL_FILES)
#  define LIBSSH2_USE_WIN32_SMALL_FILES
#endif



#ifdef LIBSSH2_USE_WIN32_LARGE_FILES
#  include <io.h>
#  define LIBSSH2_STRUCT_STAT_SIZE_FORMAT    "%I64d"
typedef struct _stati64 libssh2_struct_stat;
typedef __int64 libssh2_struct_stat_size;
#endif



#ifdef LIBSSH2_USE_WIN32_SMALL_FILES
#  ifndef _WIN32_WCE
#    define LIBSSH2_STRUCT_STAT_SIZE_FORMAT    "%d"
typedef struct _stat libssh2_struct_stat;
typedef off_t libssh2_struct_stat_size;
#  endif
#endif

#ifndef LIBSSH2_STRUCT_STAT_SIZE_FORMAT
#  ifdef __VMS

#    if __USE_OFF64_T || __USING_STD_STAT
#      define LIBSSH2_STRUCT_STAT_SIZE_FORMAT      "%Ld"
#    else
#      define LIBSSH2_STRUCT_STAT_SIZE_FORMAT      "%d"
#    endif
#  else
#    define LIBSSH2_STRUCT_STAT_SIZE_FORMAT      "%zd"
#  endif
typedef struct stat libssh2_struct_stat;
typedef off_t libssh2_struct_stat_size;
#endif


#define LIBSSH2_SSH_BANNER                  "SSH-2.0-libssh2_" LIBSSH2_VERSION

#define LIBSSH2_SSH_DEFAULT_BANNER            LIBSSH2_SSH_BANNER
#define LIBSSH2_SSH_DEFAULT_BANNER_WITH_CRLF  LIBSSH2_SSH_DEFAULT_BANNER "\r\n"


#define LIBSSH2_TERM_WIDTH      80
#define LIBSSH2_TERM_HEIGHT     24
#define LIBSSH2_TERM_WIDTH_PX   0
#define LIBSSH2_TERM_HEIGHT_PX  0


#define LIBSSH2_SOCKET_POLL_UDELAY      250000

#define LIBSSH2_SOCKET_POLL_MAXLOOPS    120


#define LIBSSH2_PACKET_MAXCOMP      32000


#define LIBSSH2_PACKET_MAXDECOMP    40000


#define LIBSSH2_PACKET_MAXPAYLOAD   40000


#define LIBSSH2_ALLOC_FUNC(name)   void *name(size_t count, void **abstract)
#define LIBSSH2_REALLOC_FUNC(name) void *name(void *ptr, size_t count, \
                                              void **abstract)
#define LIBSSH2_FREE_FUNC(name)    void name(void *ptr, void **abstract)

typedef struct _LIBSSH2_USERAUTH_KBDINT_PROMPT
{
    unsigned char *text;
    size_t length;
    unsigned char echo;
} LIBSSH2_USERAUTH_KBDINT_PROMPT;

typedef struct _LIBSSH2_USERAUTH_KBDINT_RESPONSE
{
    char *text;
    unsigned int length;  
} LIBSSH2_USERAUTH_KBDINT_RESPONSE;

typedef struct _LIBSSH2_SK_SIG_INFO {
    uint8_t flags;
    uint32_t counter;
    unsigned char *sig_r;
    size_t sig_r_len;
    unsigned char *sig_s;
    size_t sig_s_len;
} LIBSSH2_SK_SIG_INFO;


#define LIBSSH2_USERAUTH_PUBLICKEY_SIGN_FUNC(name) \
    int name(LIBSSH2_SESSION *session, unsigned char **sig, size_t *sig_len, \
             const unsigned char *data, size_t data_len, void **abstract)



#define LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC(name_) \
    void name_(const char *name, int name_len, const char *instruction, \
               int instruction_len, int num_prompts, \
               const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts, \
               LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abstract)


#define LIBSSH2_USERAUTH_SK_SIGN_FUNC(name) \
    int name(LIBSSH2_SESSION *session, LIBSSH2_SK_SIG_INFO *sig_info, \
             const unsigned char *data, size_t data_len, \
             int algorithm, uint8_t flags, \
             const char *application, const unsigned char *key_handle, \
             size_t handle_len, \
             void **abstract)


#define LIBSSH2_SK_PRESENCE_REQUIRED     0x01
#define LIBSSH2_SK_VERIFICATION_REQUIRED 0x04




#define LIBSSH2_IGNORE_FUNC(name) \
    void name(LIBSSH2_SESSION *session, const char *message, int message_len, \
              void **abstract)

#define LIBSSH2_DEBUG_FUNC(name) \
    void name(LIBSSH2_SESSION *session, int always_display, \
              const char *message, int message_len, \
              const char *language, int language_len, \
              void **abstract)

#define LIBSSH2_DISCONNECT_FUNC(name) \
    void name(LIBSSH2_SESSION *session, int reason, \
              const char *message, int message_len, \
              const char *language, int language_len, \
              void **abstract)

#define LIBSSH2_PASSWD_CHANGEREQ_FUNC(name) \
    void name(LIBSSH2_SESSION *session, char **newpw, int *newpw_len, \
              void **abstract)

#define LIBSSH2_MACERROR_FUNC(name) \
    int name(LIBSSH2_SESSION *session, const char *packet, int packet_len, \
             void **abstract)

#define LIBSSH2_X11_OPEN_FUNC(name) \
    void name(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel, \
              const char *shost, int sport, void **abstract)

#define LIBSSH2_AUTHAGENT_FUNC(name) \
    void name(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel, \
              void **abstract)

#define LIBSSH2_ADD_IDENTITIES_FUNC(name) \
    void name(LIBSSH2_SESSION *session, void *buffer, \
              const char *agent_path, void **abstract)

#define LIBSSH2_AUTHAGENT_SIGN_FUNC(name) \
    int name(LIBSSH2_SESSION* session, \
             unsigned char *blob, unsigned int blen, \
             const unsigned char *data, unsigned int dlen, \
             unsigned char **signature, unsigned int *sigLen, \
             const char *agentPath, \
             void **abstract)

#define LIBSSH2_CHANNEL_CLOSE_FUNC(name) \
    void name(LIBSSH2_SESSION *session, void **session_abstract, \
              LIBSSH2_CHANNEL *channel, void **channel_abstract)


#define LIBSSH2_RECV_FUNC(name)                                         \
    ssize_t name(libssh2_socket_t socket,                               \
                 void *buffer, size_t length,                           \
                 int flags, void **abstract)
#define LIBSSH2_SEND_FUNC(name)                                         \
    ssize_t name(libssh2_socket_t socket,                               \
                 const void *buffer, size_t length,                     \
                 int flags, void **abstract)


#define LIBSSH2_CALLBACK_IGNORE               0
#define LIBSSH2_CALLBACK_DEBUG                1
#define LIBSSH2_CALLBACK_DISCONNECT           2
#define LIBSSH2_CALLBACK_MACERROR             3
#define LIBSSH2_CALLBACK_X11                  4
#define LIBSSH2_CALLBACK_SEND                 5
#define LIBSSH2_CALLBACK_RECV                 6
#define LIBSSH2_CALLBACK_AUTHAGENT            7
#define LIBSSH2_CALLBACK_AUTHAGENT_IDENTITIES 8
#define LIBSSH2_CALLBACK_AUTHAGENT_SIGN       9


#define LIBSSH2_METHOD_KEX          0
#define LIBSSH2_METHOD_HOSTKEY      1
#define LIBSSH2_METHOD_CRYPT_CS     2
#define LIBSSH2_METHOD_CRYPT_SC     3
#define LIBSSH2_METHOD_MAC_CS       4
#define LIBSSH2_METHOD_MAC_SC       5
#define LIBSSH2_METHOD_COMP_CS      6
#define LIBSSH2_METHOD_COMP_SC      7
#define LIBSSH2_METHOD_LANG_CS      8
#define LIBSSH2_METHOD_LANG_SC      9
#define LIBSSH2_METHOD_SIGN_ALGO    10


#define LIBSSH2_FLAG_SIGPIPE        1
#define LIBSSH2_FLAG_COMPRESS       2
#define LIBSSH2_FLAG_QUOTE_PATHS    3

typedef struct _LIBSSH2_SESSION                     LIBSSH2_SESSION;
typedef struct _LIBSSH2_CHANNEL                     LIBSSH2_CHANNEL;
typedef struct _LIBSSH2_LISTENER                    LIBSSH2_LISTENER;
typedef struct _LIBSSH2_KNOWNHOSTS                  LIBSSH2_KNOWNHOSTS;
typedef struct _LIBSSH2_AGENT                       LIBSSH2_AGENT;


typedef struct _LIBSSH2_PRIVKEY_SK {
    int algorithm;
    uint8_t flags;
    const char *application;
    const unsigned char *key_handle;
    size_t handle_len;
    LIBSSH2_USERAUTH_SK_SIGN_FUNC((*sign_callback));
    void **orig_abstract;
} LIBSSH2_PRIVKEY_SK;

int
libssh2_sign_sk(LIBSSH2_SESSION *session,
                unsigned char **sig,
                size_t *sig_len,
                const unsigned char *data,
                size_t data_len,
                void **abstract);

typedef struct _LIBSSH2_POLLFD {
    unsigned char type; 

    union {
        libssh2_socket_t socket; 
        LIBSSH2_CHANNEL *channel; 
        LIBSSH2_LISTENER *listener; 
    } fd;

    unsigned long events; 
    unsigned long revents; 
} LIBSSH2_POLLFD;


#define LIBSSH2_POLLFD_SOCKET       1
#define LIBSSH2_POLLFD_CHANNEL      2
#define LIBSSH2_POLLFD_LISTENER     3



#define LIBSSH2_POLLFD_POLLIN           0x0001 
#define LIBSSH2_POLLFD_POLLPRI          0x0002 
#define LIBSSH2_POLLFD_POLLEXT          0x0002 
#define LIBSSH2_POLLFD_POLLOUT          0x0004 

#define LIBSSH2_POLLFD_POLLERR          0x0008 
#define LIBSSH2_POLLFD_POLLHUP          0x0010 
#define LIBSSH2_POLLFD_SESSION_CLOSED   0x0010 
#define LIBSSH2_POLLFD_POLLNVAL         0x0020 
#define LIBSSH2_POLLFD_POLLEX           0x0040 
#define LIBSSH2_POLLFD_CHANNEL_CLOSED   0x0080 
#define LIBSSH2_POLLFD_LISTENER_CLOSED  0x0080 

#define HAVE_LIBSSH2_SESSION_BLOCK_DIRECTION

#define LIBSSH2_SESSION_BLOCK_INBOUND                  0x0001
#define LIBSSH2_SESSION_BLOCK_OUTBOUND                 0x0002


#define LIBSSH2_HOSTKEY_HASH_MD5                            1
#define LIBSSH2_HOSTKEY_HASH_SHA1                           2
#define LIBSSH2_HOSTKEY_HASH_SHA256                         3


#define LIBSSH2_HOSTKEY_TYPE_UNKNOWN            0
#define LIBSSH2_HOSTKEY_TYPE_RSA                1
#define LIBSSH2_HOSTKEY_TYPE_DSS                2  
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_256          3
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_384          4
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_521          5
#define LIBSSH2_HOSTKEY_TYPE_ED25519            6


#define SSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT          1
#define SSH_DISCONNECT_PROTOCOL_ERROR                       2
#define SSH_DISCONNECT_KEY_EXCHANGE_FAILED                  3
#define SSH_DISCONNECT_RESERVED                             4
#define SSH_DISCONNECT_MAC_ERROR                            5
#define SSH_DISCONNECT_COMPRESSION_ERROR                    6
#define SSH_DISCONNECT_SERVICE_NOT_AVAILABLE                7
#define SSH_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED       8
#define SSH_DISCONNECT_HOST_KEY_NOT_VERIFIABLE              9
#define SSH_DISCONNECT_CONNECTION_LOST                      10
#define SSH_DISCONNECT_BY_APPLICATION                       11
#define SSH_DISCONNECT_TOO_MANY_CONNECTIONS                 12
#define SSH_DISCONNECT_AUTH_CANCELLED_BY_USER               13
#define SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE       14
#define SSH_DISCONNECT_ILLEGAL_USER_NAME                    15


#define LIBSSH2_ERROR_NONE                      0


#define LIBSSH2_ERROR_SOCKET_NONE               -1

#define LIBSSH2_ERROR_BANNER_RECV               -2
#define LIBSSH2_ERROR_BANNER_SEND               -3
#define LIBSSH2_ERROR_INVALID_MAC               -4
#define LIBSSH2_ERROR_KEX_FAILURE               -5
#define LIBSSH2_ERROR_ALLOC                     -6
#define LIBSSH2_ERROR_SOCKET_SEND               -7
#define LIBSSH2_ERROR_KEY_EXCHANGE_FAILURE      -8
#define LIBSSH2_ERROR_TIMEOUT                   -9
#define LIBSSH2_ERROR_HOSTKEY_INIT              -10
#define LIBSSH2_ERROR_HOSTKEY_SIGN              -11
#define LIBSSH2_ERROR_DECRYPT                   -12
#define LIBSSH2_ERROR_SOCKET_DISCONNECT         -13
#define LIBSSH2_ERROR_PROTO                     -14
#define LIBSSH2_ERROR_PASSWORD_EXPIRED          -15
#define LIBSSH2_ERROR_FILE                      -16
#define LIBSSH2_ERROR_METHOD_NONE               -17
#define LIBSSH2_ERROR_AUTHENTICATION_FAILED     -18
#define LIBSSH2_ERROR_PUBLICKEY_UNRECOGNIZED    \
    LIBSSH2_ERROR_AUTHENTICATION_FAILED
#define LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED      -19
#define LIBSSH2_ERROR_CHANNEL_OUTOFORDER        -20
#define LIBSSH2_ERROR_CHANNEL_FAILURE           -21
#define LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED    -22
#define LIBSSH2_ERROR_CHANNEL_UNKNOWN           -23
#define LIBSSH2_ERROR_CHANNEL_WINDOW_EXCEEDED   -24
#define LIBSSH2_ERROR_CHANNEL_PACKET_EXCEEDED   -25
#define LIBSSH2_ERROR_CHANNEL_CLOSED            -26
#define LIBSSH2_ERROR_CHANNEL_EOF_SENT          -27
#define LIBSSH2_ERROR_SCP_PROTOCOL              -28
#define LIBSSH2_ERROR_ZLIB                      -29
#define LIBSSH2_ERROR_SOCKET_TIMEOUT            -30
#define LIBSSH2_ERROR_SFTP_PROTOCOL             -31
#define LIBSSH2_ERROR_REQUEST_DENIED            -32
#define LIBSSH2_ERROR_METHOD_NOT_SUPPORTED      -33
#define LIBSSH2_ERROR_INVAL                     -34
#define LIBSSH2_ERROR_INVALID_POLL_TYPE         -35
#define LIBSSH2_ERROR_PUBLICKEY_PROTOCOL        -36
#define LIBSSH2_ERROR_EAGAIN                    -37
#define LIBSSH2_ERROR_BUFFER_TOO_SMALL          -38
#define LIBSSH2_ERROR_BAD_USE                   -39
#define LIBSSH2_ERROR_COMPRESS                  -40
#define LIBSSH2_ERROR_OUT_OF_BOUNDARY           -41
#define LIBSSH2_ERROR_AGENT_PROTOCOL            -42
#define LIBSSH2_ERROR_SOCKET_RECV               -43
#define LIBSSH2_ERROR_ENCRYPT                   -44
#define LIBSSH2_ERROR_BAD_SOCKET                -45
#define LIBSSH2_ERROR_KNOWN_HOSTS               -46
#define LIBSSH2_ERROR_CHANNEL_WINDOW_FULL       -47
#define LIBSSH2_ERROR_KEYFILE_AUTH_FAILED       -48
#define LIBSSH2_ERROR_RANDGEN                   -49
#define LIBSSH2_ERROR_MISSING_USERAUTH_BANNER   -50
#define LIBSSH2_ERROR_ALGO_UNSUPPORTED          -51
#define LIBSSH2_ERROR_MAC_FAILURE               -52
#define LIBSSH2_ERROR_HASH_INIT                 -53
#define LIBSSH2_ERROR_HASH_CALC                 -54


#define LIBSSH2_ERROR_BANNER_NONE LIBSSH2_ERROR_BANNER_RECV


#define LIBSSH2_INIT_NO_CRYPTO        0x0001


LIBSSH2_API int libssh2_init(int flags);


LIBSSH2_API void libssh2_exit(void);


LIBSSH2_API void libssh2_free(LIBSSH2_SESSION *session, void *ptr);


LIBSSH2_API int libssh2_session_supported_algs(LIBSSH2_SESSION* session,
                                               int method_type,
                                               const char ***algs);


LIBSSH2_API LIBSSH2_SESSION *
libssh2_session_init_ex(LIBSSH2_ALLOC_FUNC((*my_alloc)),
                        LIBSSH2_FREE_FUNC((*my_free)),
                        LIBSSH2_REALLOC_FUNC((*my_realloc)), void *abstract);
#define libssh2_session_init() libssh2_session_init_ex(NULL, NULL, NULL, NULL)

LIBSSH2_API void **libssh2_session_abstract(LIBSSH2_SESSION *session);

typedef void (libssh2_cb_generic)(void);

LIBSSH2_API libssh2_cb_generic *
libssh2_session_callback_set2(LIBSSH2_SESSION *session, int cbtype,
                              libssh2_cb_generic *callback);

LIBSSH2_DEPRECATED(1.11.1, "Use libssh2_session_callback_set2()")
LIBSSH2_API void *libssh2_session_callback_set(LIBSSH2_SESSION *session,
                                               int cbtype, void *callback);
LIBSSH2_API int libssh2_session_banner_set(LIBSSH2_SESSION *session,
                                           const char *banner);
#ifndef LIBSSH2_NO_DEPRECATED
LIBSSH2_DEPRECATED(1.4.0, "Use libssh2_session_banner_set()")
LIBSSH2_API int libssh2_banner_set(LIBSSH2_SESSION *session,
                                   const char *banner);

LIBSSH2_DEPRECATED(1.2.8, "Use libssh2_session_handshake()")
LIBSSH2_API int libssh2_session_startup(LIBSSH2_SESSION *session, int sock);
#endif
LIBSSH2_API int libssh2_session_handshake(LIBSSH2_SESSION *session,
                                          libssh2_socket_t sock);
LIBSSH2_API int libssh2_session_disconnect_ex(LIBSSH2_SESSION *session,
                                              int reason,
                                              const char *description,
                                              const char *lang);
#define libssh2_session_disconnect(session, description) \
    libssh2_session_disconnect_ex((session), SSH_DISCONNECT_BY_APPLICATION, \
                                  (description), "")

LIBSSH2_API int libssh2_session_free(LIBSSH2_SESSION *session);

LIBSSH2_API const char *libssh2_hostkey_hash(LIBSSH2_SESSION *session,
                                             int hash_type);

LIBSSH2_API const char *libssh2_session_hostkey(LIBSSH2_SESSION *session,
                                                size_t *len, int *type);

LIBSSH2_API int libssh2_session_method_pref(LIBSSH2_SESSION *session,
                                            int method_type,
                                            const char *prefs);
LIBSSH2_API const char *libssh2_session_methods(LIBSSH2_SESSION *session,
                                                int method_type);
LIBSSH2_API int libssh2_session_last_error(LIBSSH2_SESSION *session,
                                           char **errmsg,
                                           int *errmsg_len, int want_buf);
LIBSSH2_API int libssh2_session_last_errno(LIBSSH2_SESSION *session);
LIBSSH2_API int libssh2_session_set_last_error(LIBSSH2_SESSION* session,
                                               int errcode,
                                               const char *errmsg);
LIBSSH2_API int libssh2_session_block_directions(LIBSSH2_SESSION *session);

LIBSSH2_API int libssh2_session_flag(LIBSSH2_SESSION *session, int flag,
                                     int value);
LIBSSH2_API const char *libssh2_session_banner_get(LIBSSH2_SESSION *session);


LIBSSH2_API char *libssh2_userauth_list(LIBSSH2_SESSION *session,
                                        const char *username,
                                        unsigned int username_len);
LIBSSH2_API int libssh2_userauth_banner(LIBSSH2_SESSION *session,
                                        char **banner);
LIBSSH2_API int libssh2_userauth_authenticated(LIBSSH2_SESSION *session);

LIBSSH2_API int
libssh2_userauth_password_ex(LIBSSH2_SESSION *session,
                             const char *username,
                             unsigned int username_len,
                             const char *password,
                             unsigned int password_len,
                             LIBSSH2_PASSWD_CHANGEREQ_FUNC
                                 ((*passwd_change_cb)));

#define libssh2_userauth_password(session, username, password) \
    libssh2_userauth_password_ex((session), (username), \
                                 (unsigned int)strlen(username), \
                                 (password), (unsigned int)strlen(password), \
                                 NULL)

LIBSSH2_API int
libssh2_userauth_publickey_fromfile_ex(LIBSSH2_SESSION *session,
                                       const char *username,
                                       unsigned int username_len,
                                       const char *publickey,
                                       const char *privatekey,
                                       const char *passphrase);

#define libssh2_userauth_publickey_fromfile(session, username, publickey,  \
                                            privatekey, passphrase)        \
    libssh2_userauth_publickey_fromfile_ex((session), (username),          \
                                           (unsigned int)strlen(username), \
                                           (publickey),                    \
                                           (privatekey), (passphrase))

LIBSSH2_API int
libssh2_userauth_publickey(LIBSSH2_SESSION *session,
                           const char *username,
                           const unsigned char *pubkeydata,
                           size_t pubkeydata_len,
                           LIBSSH2_USERAUTH_PUBLICKEY_SIGN_FUNC
                               ((*sign_callback)),
                           void **abstract);

LIBSSH2_API int
libssh2_userauth_hostbased_fromfile_ex(LIBSSH2_SESSION *session,
                                       const char *username,
                                       unsigned int username_len,
                                       const char *publickey,
                                       const char *privatekey,
                                       const char *passphrase,
                                       const char *hostname,
                                       unsigned int hostname_len,
                                       const char *local_username,
                                       unsigned int local_username_len);

#define libssh2_userauth_hostbased_fromfile(session, username, publickey,     \
                                            privatekey, passphrase, hostname) \
    libssh2_userauth_hostbased_fromfile_ex((session), (username),             \
                                           (unsigned int)strlen(username),    \
                                           (publickey),                       \
                                           (privatekey), (passphrase),        \
                                           (hostname),                        \
                                           (unsigned int)strlen(hostname),    \
                                           (username),                        \
                                           (unsigned int)strlen(username))

LIBSSH2_API int
libssh2_userauth_publickey_frommemory(LIBSSH2_SESSION *session,
                                      const char *username,
                                      size_t username_len,
                                      const char *publickeyfiledata,
                                      size_t publickeyfiledata_len,
                                      const char *privatekeyfiledata,
                                      size_t privatekeyfiledata_len,
                                      const char *passphrase);


LIBSSH2_API int
libssh2_userauth_keyboard_interactive_ex(LIBSSH2_SESSION* session,
                                         const char *username,
                                         unsigned int username_len,
                                         LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC
                                             ((*response_callback)));

#define libssh2_userauth_keyboard_interactive(session, username,             \
                                              response_callback)             \
    libssh2_userauth_keyboard_interactive_ex((session), (username),          \
                                             (unsigned int)strlen(username), \
                                             (response_callback))

LIBSSH2_API int
libssh2_userauth_publickey_sk(LIBSSH2_SESSION *session,
                              const char *username,
                              size_t username_len,
                              const unsigned char *pubkeydata,
                              size_t pubkeydata_len,
                              const char *privatekeydata,
                              size_t privatekeydata_len,
                              const char *passphrase,
                              LIBSSH2_USERAUTH_SK_SIGN_FUNC
                                  ((*sign_callback)),
                              void **abstract);

LIBSSH2_API int libssh2_poll(LIBSSH2_POLLFD *fds, unsigned int nfds,
                             long timeout);


#define LIBSSH2_CHANNEL_WINDOW_DEFAULT  (2*1024*1024)
#define LIBSSH2_CHANNEL_PACKET_DEFAULT  32768
#define LIBSSH2_CHANNEL_MINADJUST       1024


#define LIBSSH2_CHANNEL_EXTENDED_DATA_NORMAL        0
#define LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE        1
#define LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE         2

#define SSH_EXTENDED_DATA_STDERR 1


#define LIBSSH2CHANNEL_EAGAIN LIBSSH2_ERROR_EAGAIN

LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_open_ex(LIBSSH2_SESSION *session, const char *channel_type,
                        unsigned int channel_type_len,
                        unsigned int window_size, unsigned int packet_size,
                        const char *message, unsigned int message_len);

#define libssh2_channel_open_session(session) \
    libssh2_channel_open_ex((session), "session", sizeof("session") - 1, \
                            LIBSSH2_CHANNEL_WINDOW_DEFAULT, \
                            LIBSSH2_CHANNEL_PACKET_DEFAULT, NULL, 0)

LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_direct_tcpip_ex(LIBSSH2_SESSION *session, const char *host,
                                int port, const char *shost, int sport);
#define libssh2_channel_direct_tcpip(session, host, port) \
    libssh2_channel_direct_tcpip_ex((session), (host), (port), "127.0.0.1", 22)

LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_direct_streamlocal_ex(LIBSSH2_SESSION * session,
                                      const char *socket_path,
                                      const char *shost, int sport);

LIBSSH2_API LIBSSH2_LISTENER *
libssh2_channel_forward_listen_ex(LIBSSH2_SESSION *session, const char *host,
                                  int port, int *bound_port,
                                  int queue_maxsize);
#define libssh2_channel_forward_listen(session, port) \
    libssh2_channel_forward_listen_ex((session), NULL, (port), NULL, 16)

LIBSSH2_API int libssh2_channel_forward_cancel(LIBSSH2_LISTENER *listener);

LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_channel_forward_accept(LIBSSH2_LISTENER *listener);

LIBSSH2_API int libssh2_channel_setenv_ex(LIBSSH2_CHANNEL *channel,
                                          const char *varname,
                                          unsigned int varname_len,
                                          const char *value,
                                          unsigned int value_len);

#define libssh2_channel_setenv(channel, varname, value)                 \
    libssh2_channel_setenv_ex((channel), (varname),                     \
                              (unsigned int)strlen(varname), (value),   \
                              (unsigned int)strlen(value))

LIBSSH2_API int libssh2_channel_request_auth_agent(LIBSSH2_CHANNEL *channel);

LIBSSH2_API int libssh2_channel_request_pty_ex(LIBSSH2_CHANNEL *channel,
                                               const char *term,
                                               unsigned int term_len,
                                               const char *modes,
                                               unsigned int modes_len,
                                               int width, int height,
                                               int width_px, int height_px);
#define libssh2_channel_request_pty(channel, term)                      \
    libssh2_channel_request_pty_ex((channel), (term),                   \
                                   (unsigned int)strlen(term),          \
                                   NULL, 0,                             \
                                   LIBSSH2_TERM_WIDTH,                  \
                                   LIBSSH2_TERM_HEIGHT,                 \
                                   LIBSSH2_TERM_WIDTH_PX,               \
                                   LIBSSH2_TERM_HEIGHT_PX)

LIBSSH2_API int libssh2_channel_request_pty_size_ex(LIBSSH2_CHANNEL *channel,
                                                    int width, int height,
                                                    int width_px,
                                                    int height_px);
#define libssh2_channel_request_pty_size(channel, width, height) \
    libssh2_channel_request_pty_size_ex((channel), (width), (height), 0, 0)

LIBSSH2_API int libssh2_channel_x11_req_ex(LIBSSH2_CHANNEL *channel,
                                           int single_connection,
                                           const char *auth_proto,
                                           const char *auth_cookie,
                                           int screen_number);
#define libssh2_channel_x11_req(channel, screen_number) \
    libssh2_channel_x11_req_ex((channel), 0, NULL, NULL, (screen_number))

LIBSSH2_API int libssh2_channel_signal_ex(LIBSSH2_CHANNEL *channel,
                                          const char *signame,
                                          size_t signame_len);
#define libssh2_channel_signal(channel, signame) \
    libssh2_channel_signal_ex((channel), signame, strlen(signame))

LIBSSH2_API int libssh2_channel_process_startup(LIBSSH2_CHANNEL *channel,
                                                const char *request,
                                                unsigned int request_len,
                                                const char *message,
                                                unsigned int message_len);
#define libssh2_channel_shell(channel) \
    libssh2_channel_process_startup((channel), "shell", sizeof("shell") - 1, \
                                    NULL, 0)
#define libssh2_channel_exec(channel, command) \
    libssh2_channel_process_startup((channel), "exec", sizeof("exec") - 1, \
                                    (command), (unsigned int)strlen(command))
#define libssh2_channel_subsystem(channel, subsystem) \
    libssh2_channel_process_startup((channel), "subsystem", \
                                    sizeof("subsystem") - 1, (subsystem), \
                                    (unsigned int)strlen(subsystem))

LIBSSH2_API ssize_t libssh2_channel_read_ex(LIBSSH2_CHANNEL *channel,
                                            int stream_id, char *buf,
                                            size_t buflen);
#define libssh2_channel_read(channel, buf, buflen) \
    libssh2_channel_read_ex((channel), 0, \
                            (buf), (buflen))
#define libssh2_channel_read_stderr(channel, buf, buflen) \
    libssh2_channel_read_ex((channel), SSH_EXTENDED_DATA_STDERR, \
                            (buf), (buflen))

LIBSSH2_API int libssh2_poll_channel_read(LIBSSH2_CHANNEL *channel,
                                          int extended);

LIBSSH2_API unsigned long
libssh2_channel_window_read_ex(LIBSSH2_CHANNEL *channel,
                               unsigned long *read_avail,
                               unsigned long *window_size_initial);
#define libssh2_channel_window_read(channel) \
    libssh2_channel_window_read_ex((channel), NULL, NULL)

#ifndef LIBSSH2_NO_DEPRECATED
LIBSSH2_DEPRECATED(1.1.0, "Use libssh2_channel_receive_window_adjust2()")
LIBSSH2_API unsigned long
libssh2_channel_receive_window_adjust(LIBSSH2_CHANNEL *channel,
                                      unsigned long adjustment,
                                      unsigned char force);
#endif
LIBSSH2_API int
libssh2_channel_receive_window_adjust2(LIBSSH2_CHANNEL *channel,
                                       unsigned long adjustment,
                                       unsigned char force,
                                       unsigned int *storewindow);

LIBSSH2_API ssize_t libssh2_channel_write_ex(LIBSSH2_CHANNEL *channel,
                                             int stream_id, const char *buf,
                                             size_t buflen);

#define libssh2_channel_write(channel, buf, buflen) \
    libssh2_channel_write_ex((channel), 0, \
                             (buf), (buflen))
#define libssh2_channel_write_stderr(channel, buf, buflen) \
    libssh2_channel_write_ex((channel), SSH_EXTENDED_DATA_STDERR, \
                             (buf), (buflen))

LIBSSH2_API unsigned long
libssh2_channel_window_write_ex(LIBSSH2_CHANNEL *channel,
                                unsigned long *window_size_initial);
#define libssh2_channel_window_write(channel) \
    libssh2_channel_window_write_ex((channel), NULL)

LIBSSH2_API void libssh2_session_set_blocking(LIBSSH2_SESSION* session,
                                              int blocking);
LIBSSH2_API int libssh2_session_get_blocking(LIBSSH2_SESSION* session);

LIBSSH2_API void libssh2_channel_set_blocking(LIBSSH2_CHANNEL *channel,
                                              int blocking);

LIBSSH2_API void libssh2_session_set_timeout(LIBSSH2_SESSION* session,
                                             long timeout);
LIBSSH2_API long libssh2_session_get_timeout(LIBSSH2_SESSION* session);

LIBSSH2_API void libssh2_session_set_read_timeout(LIBSSH2_SESSION* session,
                                                  long timeout);
LIBSSH2_API long libssh2_session_get_read_timeout(LIBSSH2_SESSION* session);

#ifndef LIBSSH2_NO_DEPRECATED
LIBSSH2_DEPRECATED(1.1.0, "libssh2_channel_handle_extended_data2()")
LIBSSH2_API void libssh2_channel_handle_extended_data(LIBSSH2_CHANNEL *channel,
                                                      int ignore_mode);
#endif
LIBSSH2_API int libssh2_channel_handle_extended_data2(LIBSSH2_CHANNEL *channel,
                                                      int ignore_mode);

#ifndef LIBSSH2_NO_DEPRECATED


#define libssh2_channel_ignore_extended_data(channel, ignore)                 \
    libssh2_channel_handle_extended_data((channel), (ignore) ?                \
                                       LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE : \
                                       LIBSSH2_CHANNEL_EXTENDED_DATA_NORMAL)
#endif

#define LIBSSH2_CHANNEL_FLUSH_EXTENDED_DATA     -1
#define LIBSSH2_CHANNEL_FLUSH_ALL               -2
LIBSSH2_API int libssh2_channel_flush_ex(LIBSSH2_CHANNEL *channel,
                                         int streamid);
#define libssh2_channel_flush(channel) libssh2_channel_flush_ex((channel), 0)
#define libssh2_channel_flush_stderr(channel) \
    libssh2_channel_flush_ex((channel), SSH_EXTENDED_DATA_STDERR)

LIBSSH2_API int libssh2_channel_get_exit_status(LIBSSH2_CHANNEL* channel);
LIBSSH2_API int libssh2_channel_get_exit_signal(LIBSSH2_CHANNEL* channel,
                                                char **exitsignal,
                                                size_t *exitsignal_len,
                                                char **errmsg,
                                                size_t *errmsg_len,
                                                char **langtag,
                                                size_t *langtag_len);
LIBSSH2_API int libssh2_channel_send_eof(LIBSSH2_CHANNEL *channel);
LIBSSH2_API int libssh2_channel_eof(LIBSSH2_CHANNEL *channel);
LIBSSH2_API int libssh2_channel_wait_eof(LIBSSH2_CHANNEL *channel);
LIBSSH2_API int libssh2_channel_close(LIBSSH2_CHANNEL *channel);
LIBSSH2_API int libssh2_channel_wait_closed(LIBSSH2_CHANNEL *channel);
LIBSSH2_API int libssh2_channel_free(LIBSSH2_CHANNEL *channel);

#ifndef LIBSSH2_NO_DEPRECATED
LIBSSH2_DEPRECATED(1.7.0, "Use libssh2_scp_recv2()")
LIBSSH2_API LIBSSH2_CHANNEL *libssh2_scp_recv(LIBSSH2_SESSION *session,
                                              const char *path,
                                              struct stat *sb);
#endif

LIBSSH2_API LIBSSH2_CHANNEL *libssh2_scp_recv2(LIBSSH2_SESSION *session,
                                               const char *path,
                                               libssh2_struct_stat *sb);
LIBSSH2_API LIBSSH2_CHANNEL *libssh2_scp_send_ex(LIBSSH2_SESSION *session,
                                                 const char *path, int mode,
                                                 size_t size, long mtime,
                                                 long atime);
LIBSSH2_API LIBSSH2_CHANNEL *
libssh2_scp_send64(LIBSSH2_SESSION *session, const char *path, int mode,
                   libssh2_int64_t size, time_t mtime, time_t atime);

#define libssh2_scp_send(session, path, mode, size) \
    libssh2_scp_send_ex((session), (path), (mode), (size), 0, 0)


LIBSSH2_API int libssh2_base64_decode(LIBSSH2_SESSION *session, char **dest,
                                      unsigned int *dest_len,
                                      const char *src, unsigned int src_len);

LIBSSH2_API
const char *libssh2_version(int req_version_num);

typedef enum {
    libssh2_no_crypto = 0,
    libssh2_openssl,
    libssh2_gcrypt,
    libssh2_mbedtls,
    libssh2_wincng,
    libssh2_os400qc3
} libssh2_crypto_engine_t;

LIBSSH2_API
libssh2_crypto_engine_t libssh2_crypto_engine(void);

#define HAVE_LIBSSH2_KNOWNHOST_API 0x010101 
#define HAVE_LIBSSH2_VERSION_API   0x010100 
#define HAVE_LIBSSH2_CRYPTOENGINE_API 0x011100 

struct libssh2_knownhost {
    unsigned int magic;  
    void *node; 
    char *name; 
    char *key;  
    int typemask;
};


LIBSSH2_API LIBSSH2_KNOWNHOSTS *
libssh2_knownhost_init(LIBSSH2_SESSION *session);




#define LIBSSH2_KNOWNHOST_TYPE_MASK    0xffff
#define LIBSSH2_KNOWNHOST_TYPE_PLAIN   1
#define LIBSSH2_KNOWNHOST_TYPE_SHA1    2 
#define LIBSSH2_KNOWNHOST_TYPE_CUSTOM  3


#define LIBSSH2_KNOWNHOST_KEYENC_MASK     (3<<16)
#define LIBSSH2_KNOWNHOST_KEYENC_RAW      (1<<16)
#define LIBSSH2_KNOWNHOST_KEYENC_BASE64   (2<<16)


#define LIBSSH2_KNOWNHOST_KEY_MASK         (15<<18)
#define LIBSSH2_KNOWNHOST_KEY_SHIFT        18
#define LIBSSH2_KNOWNHOST_KEY_RSA1         (1<<18)
#define LIBSSH2_KNOWNHOST_KEY_SSHRSA       (2<<18)
#define LIBSSH2_KNOWNHOST_KEY_SSHDSS       (3<<18)  
#define LIBSSH2_KNOWNHOST_KEY_ECDSA_256    (4<<18)
#define LIBSSH2_KNOWNHOST_KEY_ECDSA_384    (5<<18)
#define LIBSSH2_KNOWNHOST_KEY_ECDSA_521    (6<<18)
#define LIBSSH2_KNOWNHOST_KEY_ED25519      (7<<18)
#define LIBSSH2_KNOWNHOST_KEY_UNKNOWN      (15<<18)

LIBSSH2_API int
libssh2_knownhost_add(LIBSSH2_KNOWNHOSTS *hosts,
                      const char *host,
                      const char *salt,
                      const char *key, size_t keylen, int typemask,
                      struct libssh2_knownhost **store);



LIBSSH2_API int
libssh2_knownhost_addc(LIBSSH2_KNOWNHOSTS *hosts,
                       const char *host,
                       const char *salt,
                       const char *key, size_t keylen,
                       const char *comment, size_t commentlen, int typemask,
                       struct libssh2_knownhost **store);



#define LIBSSH2_KNOWNHOST_CHECK_MATCH    0
#define LIBSSH2_KNOWNHOST_CHECK_MISMATCH 1
#define LIBSSH2_KNOWNHOST_CHECK_NOTFOUND 2
#define LIBSSH2_KNOWNHOST_CHECK_FAILURE  3

LIBSSH2_API int
libssh2_knownhost_check(LIBSSH2_KNOWNHOSTS *hosts,
                        const char *host, const char *key, size_t keylen,
                        int typemask,
                        struct libssh2_knownhost **knownhost);


LIBSSH2_API int
libssh2_knownhost_checkp(LIBSSH2_KNOWNHOSTS *hosts,
                         const char *host, int port,
                         const char *key, size_t keylen,
                         int typemask,
                         struct libssh2_knownhost **knownhost);


LIBSSH2_API int
libssh2_knownhost_del(LIBSSH2_KNOWNHOSTS *hosts,
                      struct libssh2_knownhost *entry);


LIBSSH2_API void
libssh2_knownhost_free(LIBSSH2_KNOWNHOSTS *hosts);


LIBSSH2_API int
libssh2_knownhost_readline(LIBSSH2_KNOWNHOSTS *hosts,
                           const char *line, size_t len, int type);



#define LIBSSH2_KNOWNHOST_FILE_OPENSSH 1

LIBSSH2_API int
libssh2_knownhost_readfile(LIBSSH2_KNOWNHOSTS *hosts,
                           const char *filename, int type);


LIBSSH2_API int
libssh2_knownhost_writeline(LIBSSH2_KNOWNHOSTS *hosts,
                            struct libssh2_knownhost *known,
                            char *buffer, size_t buflen,
                            size_t *outlen, 
                            int type);



LIBSSH2_API int
libssh2_knownhost_writefile(LIBSSH2_KNOWNHOSTS *hosts,
                            const char *filename, int type);


LIBSSH2_API int
libssh2_knownhost_get(LIBSSH2_KNOWNHOSTS *hosts,
                      struct libssh2_knownhost **store,
                      struct libssh2_knownhost *prev);

#define HAVE_LIBSSH2_AGENT_API 0x010202 

struct libssh2_agent_publickey {
    unsigned int magic;              
    void *node;     
    unsigned char *blob;           
    size_t blob_len;               
    char *comment;                 
};


LIBSSH2_API LIBSSH2_AGENT *
libssh2_agent_init(LIBSSH2_SESSION *session);


LIBSSH2_API int
libssh2_agent_connect(LIBSSH2_AGENT *agent);


LIBSSH2_API int
libssh2_agent_list_identities(LIBSSH2_AGENT *agent);


LIBSSH2_API int
libssh2_agent_get_identity(LIBSSH2_AGENT *agent,
                           struct libssh2_agent_publickey **store,
                           struct libssh2_agent_publickey *prev);


LIBSSH2_API int
libssh2_agent_userauth(LIBSSH2_AGENT *agent,
                       const char *username,
                       struct libssh2_agent_publickey *identity);


LIBSSH2_API int
libssh2_agent_sign(LIBSSH2_AGENT *agent,
                   struct libssh2_agent_publickey *identity,
                   unsigned char **sig,
                   size_t *s_len,
                   const unsigned char *data,
                   size_t d_len,
                   const char *method,
                   unsigned int method_len);


LIBSSH2_API int
libssh2_agent_disconnect(LIBSSH2_AGENT *agent);


LIBSSH2_API void
libssh2_agent_free(LIBSSH2_AGENT *agent);


LIBSSH2_API void
libssh2_agent_set_identity_path(LIBSSH2_AGENT *agent,
                                const char *path);


LIBSSH2_API const char *
libssh2_agent_get_identity_path(LIBSSH2_AGENT *agent);


LIBSSH2_API void libssh2_keepalive_config(LIBSSH2_SESSION *session,
                                          int want_reply,
                                          unsigned int interval);


LIBSSH2_API int libssh2_keepalive_send(LIBSSH2_SESSION *session,
                                       int *seconds_to_next);


LIBSSH2_API int libssh2_trace(LIBSSH2_SESSION *session, int bitmask);
#define LIBSSH2_TRACE_TRANS      (1<<1)
#define LIBSSH2_TRACE_KEX        (1<<2)
#define LIBSSH2_TRACE_AUTH       (1<<3)
#define LIBSSH2_TRACE_CONN       (1<<4)
#define LIBSSH2_TRACE_SCP        (1<<5)
#define LIBSSH2_TRACE_SFTP       (1<<6)
#define LIBSSH2_TRACE_ERROR      (1<<7)
#define LIBSSH2_TRACE_PUBLICKEY  (1<<8)
#define LIBSSH2_TRACE_SOCKET     (1<<9)

typedef void (*libssh2_trace_handler_func)(LIBSSH2_SESSION*,
                                           void *,
                                           const char *,
                                           size_t);
LIBSSH2_API int libssh2_trace_sethandler(LIBSSH2_SESSION *session,
                                         void *context,
                                         libssh2_trace_handler_func callback);

#ifdef __cplusplus
} 
#endif

#endif 

#endif 
