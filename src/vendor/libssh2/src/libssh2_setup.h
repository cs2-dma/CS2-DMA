

#ifndef LIBSSH2_SETUP_H
#define LIBSSH2_SETUP_H




#ifdef __MINGW32__
#include <_mingw.h>
#endif


#if defined(HAVE_CONFIG_H) || defined(__OS400__) || defined(__VMS)

#include "libssh2_config.h"


#elif defined(_WIN32)

#define HAVE_SELECT
#define HAVE_SNPRINTF

#ifdef __MINGW32__
# define HAVE_UNISTD_H
# define HAVE_INTTYPES_H
# define HAVE_SYS_TIME_H
# define HAVE_GETTIMEOFDAY
# define HAVE_STRTOLL
#elif defined(_MSC_VER)
# if _MSC_VER >= 1800
#  define HAVE_INTTYPES_H
#  define HAVE_STRTOLL
# else
#  define HAVE_STRTOI64
# endif
# if _MSC_VER < 1900
#  undef HAVE_SNPRINTF
# endif
#endif

#endif 



#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NONLS
#define NONLS
#endif

#ifdef __MINGW32__
# ifdef __MINGW64_VERSION_MAJOR

#  ifndef _FILE_OFFSET_BITS
#  define _FILE_OFFSET_BITS 64
#  endif
# endif
#elif defined(_MSC_VER)
# ifndef _CRT_SECURE_NO_WARNINGS
# define _CRT_SECURE_NO_WARNINGS  
# endif
# if !defined(LIBSSH2_LIBRARY) || defined(LIBSSH2_TESTS)
   
#  ifndef _CRT_NONSTDC_NO_DEPRECATE
#  define _CRT_NONSTDC_NO_DEPRECATE  
#  endif
#  ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#  define _WINSOCK_DEPRECATED_NO_WARNINGS  
#  endif
   
#  if _MSC_VER < 1900
#   undef HAVE_SNPRINTF
#   define HAVE_SNPRINTF
#   define snprintf _snprintf
#  endif
# endif
# if _MSC_VER < 1500
#  define vsnprintf _vsnprintf
# endif
# if _MSC_VER < 1900
#  define strdup _strdup

#  pragma warning(disable:4127)
# endif
#endif

#endif 

#endif 
