#pragma once

#include <stdint.h>

/* ldns/config.h.  Generated from config.h.in by configure.  */
/* ldns/config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to 1 if you have the <arpa/inet.h> header file. */
#ifndef _WIN32
#define HAVE_ARPA_INET_H 1
#endif // _WIN32

/* Whether the C compiler accepts the "format" attribute */
#define HAVE_ATTR_FORMAT 1

/* Whether the C compiler accepts the "unused" attribute */
#define HAVE_ATTR_UNUSED 1

/* Define to 1 if you have the `b32_ntop' function. */
/* #undef HAVE_B32_NTOP */

/* Define to 1 if you have the `b32_pton' function. */
/* #undef HAVE_B32_PTON */

/* Define to 1 if you have the `b64_ntop' function. */
/* #undef HAVE_B64_NTOP */

/* Define to 1 if you have the `b64_pton' function. */
/* #undef HAVE_B64_PTON */

/* Define to 1 if you have the `bzero' function. */
#define HAVE_BZERO 1

/* Define to 1 if you have the `calloc' function. */
#define HAVE_CALLOC 1

/* Define to 1 if you have the `ctime_r' function. */
#define HAVE_CTIME_R 1

/* Is a CAFILE given at configure time */
#define HAVE_DANE_CA_FILE 0

/* Is a CAPATH given at configure time */
#define HAVE_DANE_CA_PATH 0

/* Define to 1 if you have the declaration of `NID_ED25519', and to 0 if you
   don't. */
#define HAVE_DECL_NID_ED25519 0

/* Define to 1 if you have the declaration of `NID_ED448', and to 0 if you
   don't. */
#define HAVE_DECL_NID_ED448 0

/* Define to 1 if you have the declaration of `NID_secp384r1', and to 0 if you
   don't. */
#define HAVE_DECL_NID_SECP384R1 1

/* Define to 1 if you have the declaration of `NID_X9_62_prime256v1', and to 0
   if you don't. */
#define HAVE_DECL_NID_X9_62_PRIME256V1 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the `DSA_get0_key' function. */
/* #undef HAVE_DSA_GET0_KEY */

/* Define to 1 if you have the `DSA_get0_pqg' function. */
/* #undef HAVE_DSA_GET0_PQG */

/* Define to 1 if you have the `DSA_SIG_get0' function. */
/* #undef HAVE_DSA_SIG_GET0 */

/* Define to 1 if you have the `DSA_SIG_set0' function. */
/* #undef HAVE_DSA_SIG_SET0 */

/* Define to 1 if you have the `ECDSA_SIG_get0' function. */
/* #undef HAVE_ECDSA_SIG_GET0 */

/* Define to 1 if you have the `endprotoent' function. */
#if !defined(_WIN32) && !defined(__ANDROID__)
#define HAVE_ENDPROTOENT 1
#endif // !defined(_WIN32) && !defined(__ANDROID__)

/* Define to 1 if you have the `endservent' function. */
#ifndef _WIN32
#define HAVE_ENDSERVENT 1
#endif // _WIN32

/* Define to 1 if you have the `ENGINE_load_cryptodev' function. */
#define HAVE_ENGINE_LOAD_CRYPTODEV 1

/* Define to 1 if you have the `ERR_load_crypto_strings' function. */
#define HAVE_ERR_LOAD_CRYPTO_STRINGS 1

/* Define to 1 if you have the `EVP_dss1' function. */
#define HAVE_EVP_DSS1 1

/* Define to 1 if you have the `EVP_MD_CTX_new' function. */
/* #undef HAVE_EVP_MD_CTX_NEW */

/* Define to 1 if you have the `EVP_PKEY_base_id' function. */
#define HAVE_EVP_PKEY_BASE_ID 1

/* Define to 1 if you have the `EVP_PKEY_keygen' function. */
#define HAVE_EVP_PKEY_KEYGEN 1

/* Define to 1 if you have the `EVP_sha256' function. */
#define HAVE_EVP_SHA256 1

/* Define to 1 if you have the `EVP_sha384' function. */
#define HAVE_EVP_SHA384 1

/* Define to 1 if you have the `EVP_sha512' function. */
#define HAVE_EVP_SHA512 1

/* Define to 1 if you have the `fcntl' function. */
#ifndef _WIN32
#define HAVE_FCNTL 1
#endif

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Whether getaddrinfo is available */
#define HAVE_GETADDRINFO 1

/* Define to 1 if you have the <getopt.h> header file. */
#define HAVE_GETOPT_H 1

/* Define to 1 if you have the `gmtime_r' function. */
#ifndef _WIN32
#define HAVE_GMTIME_R 1
#endif // _WIN32

/* If you have HMAC_Update */
#define HAVE_HMAC_UPDATE 1

/* Define to 1 if you have the `inet_aton' function. */
#define HAVE_INET_ATON 1

/* Define to 1 if you have the `inet_ntop' function. */
#define HAVE_INET_NTOP 1

/* Define to 1 if you have the `inet_pton' function. */
#define HAVE_INET_PTON 1

/* define if you have inttypes.h */
#define HAVE_INTTYPES_H 1

/* if the function 'ioctlsocket' is available */
#ifdef _WIN32
#define HAVE_IOCTLSOCKET 1
#endif

/* Define to 1 if you have the `isascii' function. */
#define HAVE_ISASCII 1

/* Define to 1 if you have the `isblank' function. */
#define HAVE_ISBLANK 1

/* Define to 1 if you have the `pcap' library (-lpcap). */
/* #undef HAVE_LIBPCAP */

/* Define if we have LibreSSL */
/* #undef HAVE_LIBRESSL */

/* Define to 1 if you have the `localtime_r' function. */
#ifndef _WIN32
#define HAVE_LOCALTIME_R 1
#endif

/* Define to 1 if your system has a GNU libc compatible `malloc' function, and
   to 0 otherwise. */
#define HAVE_MALLOC 1

/* Define to 1 if you have the `memmove' function. */
#define HAVE_MEMMOVE 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the <netdb.h> header file. */
#ifndef _WIN32
#define HAVE_NETDB_H 1
#endif

/* Define to 1 if you have the <netinet/if_ether.h> header file. */
/* #undef HAVE_NETINET_IF_ETHER_H */

/* Define to 1 if you have the <netinet/igmp.h> header file. */
/* #undef HAVE_NETINET_IGMP_H */

/* Define to 1 if you have the <netinet/in.h> header file. */
#ifndef _WIN32
#define HAVE_NETINET_IN_H 1
#endif

/* Define to 1 if you have the <netinet/in_systm.h> header file. */
/* #undef HAVE_NETINET_IN_SYSTM_H */

/* Define to 1 if you have the <netinet/ip6.h> header file. */
/* #undef HAVE_NETINET_IP6_H */

/* Define to 1 if you have the <netinet/ip_compat.h> header file. */
/* #undef HAVE_NETINET_IP_COMPAT_H */

/* Define to 1 if you have the <netinet/ip.h> header file. */
/* #undef HAVE_NETINET_IP_H */

/* Define to 1 if you have the <netinet/udp.h> header file. */
/* #undef HAVE_NETINET_UDP_H */

/* Define to 1 if you have the <net/ethernet.h> header file. */
/* #undef HAVE_NET_ETHERNET_H */

/* Define to 1 if you have the <net/if.h> header file. */
/* #undef HAVE_NET_IF_H */

/* Define to 1 if you have the <openssl/err.h> header file. */
#define HAVE_OPENSSL_ERR_H 1

/* Define to 1 if you have the `OPENSSL_init_crypto' function. */
/* #undef HAVE_OPENSSL_INIT_CRYPTO */

/* Define to 1 if you have the `OPENSSL_init_ssl' function. */
/* #undef HAVE_OPENSSL_INIT_SSL */

/* Define to 1 if you have the <openssl/rand.h> header file. */
#define HAVE_OPENSSL_RAND_H 1

/* Define to 1 if you have the <openssl/ssl.h> header file. */
#define HAVE_OPENSSL_SSL_H 1

/* Define to 1 if you have the <pcap.h> header file. */
/* #undef HAVE_PCAP_H */

/* This platform supports poll(7). */
#ifndef _WIN32
#define HAVE_POLL 1
#endif

/* If available, contains the Python version number currently in use. */
/* #undef HAVE_PYTHON */

/* Define to 1 if you have the `random' function. */
#ifndef _WIN32
#define HAVE_RANDOM 1
#endif

/* Define to 1 if your system has a GNU libc compatible `realloc' function,
   and to 0 otherwise. */
#define HAVE_REALLOC 1

/* Define to 1 if you have the `sleep' function. */
#ifndef _WIN32
#define HAVE_SLEEP 1
#endif

/* Define to 1 if you have the `snprintf' function. */
#define HAVE_SNPRINTF 1

/* Define if you have the SSL libraries installed. */
//#define HAVE_SSL /**/

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if stdbool.h conforms to C99. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcpy' function. */
#ifdef __APPLE__
#define HAVE_STRLCPY 1
#endif // __APPLE__

/* Define to 1 if you have the `strtoul' function. */
#define HAVE_STRTOUL 1

/* Define if you have SWIG libraries and header files. */
/* #undef HAVE_SWIG */

/* Define to 1 if you have the <sys/mount.h> header file. */
#ifndef _WIN32
#define HAVE_SYS_MOUNT_H 1
#endif

/* Define to 1 if you have the <sys/param.h> header file. */
#ifndef _WIN32
#define HAVE_SYS_PARAM_H 1
#endif

/* define if you have sys/socket.h */
#ifndef _WIN32
#define HAVE_SYS_SOCKET_H 1
#endif // _WIN32

/* Define to 1 if you have the <sys/stat.h> header file. */
#ifndef _WIN32
#define HAVE_SYS_STAT_H 1
#endif

/* define if you have sys/types.h */
#ifndef _WIN32
#define HAVE_SYS_TYPES_H 1
#endif

/* Define to 1 if you have the `timegm' function. */
#ifndef _WIN32
#define HAVE_TIMEGM 1
#endif

/* Define to 1 if you have the <time.h> header file. */
#ifndef _WIN32
#define HAVE_TIME_H 1
#endif

/* define if you have unistd.h */
#ifndef _WIN32
#define HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the <winsock2.h> header file. */
#ifdef _WIN32
#define HAVE_WINSOCK2_H
#endif // _WIN32

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* Define to 1 if you have the <ws2tcpip.h> header file. */
#ifdef _WIN32
#define HAVE_WS2TCPIP_H 1
#endif

/* Define to 1 if the system has the type `_Bool'. */
#define HAVE__BOOL 1

/* Is a CAFILE given at configure time */
/* #undef LDNS_DANE_CA_FILE */

/* Is a CAPATH given at configure time */
/* #undef LDNS_DANE_CA_PATH */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Name of package */
/* #undef PACKAGE */

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "libdns@nlnetlabs.nl"

/* Define to the full name of this package. */
#define PACKAGE_NAME "ldns"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "ldns 1.7.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libdns"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.7.1"

/* Define this to enable RR type AMTRELAY. */
/* #undef RRTYPE_AMTRELAY */

/* Define this to enable RR type AVC. */
/* #undef RRTYPE_AVC */

/* Define this to enable RR type DOA. */
/* #undef RRTYPE_DOA */

/* Define this to enable RR type NINFO. */
/* #undef RRTYPE_NINFO */

/* Define this to enable RR type OPENPGPKEY. */
#define RRTYPE_OPENPGPKEY /**/

/* Define this to enable RR type RKEY. */
/* #undef RRTYPE_RKEY */

/* Define this to enable RR type TA. */
/* #undef RRTYPE_TA */

/* The size of `time_t', as computed by sizeof. */
#define SIZEOF_TIME_T 8

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define this to enable messages to stderr. */
/* #undef STDERR_MSGS */

/* System configuration dir */
#define SYSCONFDIR sysconfdir

/* Define this to enable DANE support. */
//#define USE_DANE 1

/* Define this to enable DANE-TA usage type support. */
/* #undef USE_DANE_TA_USAGE */

/* Define this to enable DANE verify support. */
//#define USE_DANE_VERIFY 1

/* Define this to enable DSA support. */
//#define USE_DSA 1

/* Define this to enable ECDSA support. */
//#define USE_ECDSA 1

/* Define this to enable ED25519 support. */
/* #undef USE_ED25519 */

/* Define this to enable ED448 support. */
/* #undef USE_ED448 */

/* Define this to enable GOST support. */
//#define USE_GOST 1

/* Define this to enable SHA256 and SHA512 support. */
#define USE_SHA2 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* Whether the windows socket API is used */
#ifdef _WIN32
#define USE_WINSOCK 1
#endif // _WIN32

/* Version number of package */
/* #undef VERSION */

/* the version of the windows API enabled */
#define WINVER 0x0502

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Enable for compile on Minix */
/* #undef _NETBSD_SOURCE */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* in_addr_t */
#define in_addr_t uint32_t

/* in_port_t */
#define in_port_t uint16_t

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to `short' if <sys/types.h> does not define. */
/* #undef int16_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef int32_t */

/* Define to `long long' if <sys/types.h> does not define. */
/* #undef int64_t */

/* Define to `char' if <sys/types.h> does not define. */
/* #undef int8_t */

/* Define to `size_t' if <sys/types.h> does not define. */
/* #undef intptr_t */

/* Define to rpl_malloc if the replacement function should be used. */
/* #undef malloc */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to rpl_realloc if the replacement function should be used. */
/* #undef realloc */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to 'int' if not defined */
/* #undef socklen_t */

/* Fallback member name for socket family in struct sockaddr_storage */
/* #undef ss_family */

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

/* Define to `unsigned short' if <sys/types.h> does not define. */
/* #undef uint16_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef uint32_t */

/* Define to `unsigned long long' if <sys/types.h> does not define. */
/* #undef uint64_t */

/* Define to `unsigned char' if <sys/types.h> does not define. */
/* #undef uint8_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */


#include <stdio.h>
#include <string.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <assert.h>

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif

#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif

#ifndef BYTE_ORDER
#ifdef WORDS_BIGENDIAN
#define BYTE_ORDER BIG_ENDIAN
#else
#define BYTE_ORDER LITTLE_ENDIAN
#endif /* WORDS_BIGENDIAN */
#endif /* BYTE_ORDER */

#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif


/* detect if we need to cast to unsigned int for FD_SET to avoid warnings */
#ifdef HAVE_WINSOCK2_H
#define FD_SET_T (u_int)
#else
#define FD_SET_T
#endif


#ifdef __cplusplus
extern "C" {
#endif

int ldns_b64_ntop(uint8_t const *src, size_t srclength,
                  char *target, size_t targsize);
/**
 * calculates the size needed to store the result of b64_ntop
 */
/*@unused@*/
static inline size_t ldns_b64_ntop_calculate_size(size_t srcsize) {
    return ((((srcsize + 2) / 3) * 4) + 1);
}
int ldns_b64_pton(char const *src, uint8_t *target, size_t targsize);
/**
 * calculates the size needed to store the result of ldns_b64_pton
 */
/*@unused@*/
static inline size_t ldns_b64_pton_calculate_size(size_t srcsize) {
    return (((((srcsize + 3) / 4) * 3)) + 1);
}

/**
 * Given in dnssec_zone.c, also used in dnssec_sign.c:w

 */
int ldns_dname_compare_v(const void *a, const void *b);

#ifndef HAVE_SLEEP
/* use windows sleep, in millisecs, instead */
#define sleep(x) Sleep((x)*1000)
#endif

#ifndef HAVE_RANDOM
#define srandom(x) srand(x)
#define random(x) rand(x)
#endif

#ifdef _WIN32
#include <event2/util.h>
#define strcasecmp evutil_ascii_strcasecmp
#define strncasecmp evutil_ascii_strncasecmp
#define gettimeofday evutil_gettimeofday

// renaming both declaration and definition of {gm,local}time_r
#define gmtime_r ldns_gmtime_r
#define localtime_r ldns_localtime_r
#endif

#ifndef HAVE_TIMEGM
#include <time.h>
time_t timegm (struct tm *tm);
#endif /* !TIMEGM */
#ifndef HAVE_GMTIME_R
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#endif
#ifndef HAVE_LOCALTIME_R
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif
#ifndef HAVE_ISBLANK
int isblank(int c);
#endif /* !HAVE_ISBLANK */
#ifndef HAVE_ISASCII
int isascii(int c);
#endif /* !HAVE_ISASCII */
#ifndef HAVE_SNPRINTF
#include <stdarg.h>
int snprintf (char *str, size_t count, const char *fmt, ...);
int vsnprintf (char *str, size_t count, const char *fmt, va_list arg);
#endif /* HAVE_SNPRINTF */
#ifndef HAVE_INET_PTON
int inet_pton(int af, const char* src, void* dst);
#endif /* HAVE_INET_PTON */
#ifndef HAVE_INET_NTOP
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif
#ifndef HAVE_INET_ATON
int inet_aton(const char *cp, struct in_addr *addr);
#endif
#ifndef HAVE_MEMMOVE
void *memmove(void *dest, const void *src, size_t n);
#endif
#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif

#ifdef USE_WINSOCK
#define SOCK_INVALID INVALID_SOCKET
#define close_socket(_s) do { if (_s != SOCK_INVALID) {closesocket(_s); _s = -1;} } while(0)
#else
#define SOCK_INVALID -1
#define close_socket(_s) do { if (_s != SOCK_INVALID) {close(_s); _s = -1;} } while(0)
#endif

#ifndef HAVE_GETADDRINFO
#include "compat/fake-rfc2553.h"
#endif
#ifndef HAVE_STRTOUL
#define strtoul (unsigned long)strtol
#endif

#ifdef _WIN32
#define LDNS_ETIMEDOUT WSAETIMEDOUT
#else
#define LDNS_ETIMEDOUT ETIMEDOUT
#endif

#ifdef __cplusplus
}
#endif
