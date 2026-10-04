#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#if CONFIG_USE_LWIP
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#elif defined(__SWITCH__)
#include <unistd.h>
#include <netdb.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

#include "ports.h"
#include "utils.h"
#include <sys/select.h>
extern int ps5_socket_close(int);
extern int ps5_socket_select(int,fd_set*,fd_set*,fd_set*,struct timeval*);
extern int ps5_socket_fcntl(int,int,...);
extern int ps5_socket_ioctl(int,unsigned long,...);
#define close ps5_socket_close
#define select ps5_socket_select
#define fcntl ps5_socket_fcntl
#define ioctl ps5_socket_ioctl

int ports_get_host_addr(Address* addr, const char* iface_prefix) {
  (void)iface_prefix;
  struct sockaddr_in remote = {0}, local = {0};
  remote.sin_family=AF_INET; remote.sin_len=sizeof(remote);remote.sin_port=htons(53);
  /* UDP connect selects the route without sending a datagram. */
  remote.sin_addr.s_addr=htonl(0x01010101);
  int fd=socket(AF_INET,SOCK_DGRAM,0);
  if(fd<0)return -1;
  socklen_t size=sizeof(local);
  int ok=connect(fd,(struct sockaddr*)&remote,sizeof(remote))==0 && getsockname(fd,(struct sockaddr*)&local,&size)==0;
  close(fd);
  if(!ok)return -1;
  addr_set_family(addr,AF_INET);addr->sin=local;addr->sin.sin_port=0;return 1;
}

int ports_resolve_addr(const char* host, Address* addr) {
  char addr_string[ADDRSTRLEN];
  int ret = -1;
  struct addrinfo hints, *res, *p;
  int status;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if ((status = getaddrinfo(host, NULL, &hints, &res)) != 0) {
    LOGE("getaddrinfo error: %d\n", status);
    return ret;
  }

  // TODO: Support for IPv6
  addr_set_family(addr, AF_INET);
  for (p = res; p != NULL; p = p->ai_next) {
    if (p->ai_family == addr->family) {
      switch (addr->family) {
        case AF_INET6:
          memcpy(&addr->sin6, p->ai_addr, sizeof(struct sockaddr_in6));
          break;
        case AF_INET:
        default:
          memcpy(&addr->sin, p->ai_addr, sizeof(struct sockaddr_in));
          break;
      }
      ret = 0;
    }
  }

  addr_to_string(addr, addr_string, sizeof(addr_string));
  LOGI("Resolved %s -> %s", host, addr_string);
  freeaddrinfo(res);
  return ret;
}

uint32_t ports_get_epoch_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint32_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

uint32_t ports_get_monotonic_time(void) {
#if CONFIG_USE_LWIP
  return sys_now();
#else
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    return 0;
  return (uint32_t)time.tv_sec * 1000 + (uint32_t)(time.tv_nsec / 1000000);
#endif
}

void ports_sleep_ms(int ms) {
#if CONFIG_USE_LWIP
  sys_msleep(ms);
#else
  usleep(ms * 1000);
#endif
}
