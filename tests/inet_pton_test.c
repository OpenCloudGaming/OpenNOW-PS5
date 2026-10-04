#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
extern int opennow_inet_pton(int,const char*,void*);
int main(void) {
 const char *cases[]={"login.nvidia.com","localhost","","192.168.1.1","127.0.0.1","256.0.0.1","::","::1","2001:db8::1234","::ffff:192.0.2.1","1:2:3:4:5:6:7:8","1:2:3:4:5:6:7:8:9","1::2::3","12345::","g::","[::1]"};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) for(int v=0;v<2;++v) {
  int family=v?AF_INET6:AF_INET;unsigned char expected[16],actual[16];memset(expected,0xa5,16);memset(actual,0xa5,16);
  int a=opennow_inet_pton(family,cases[i],actual),e=inet_pton(family,cases[i],expected);if(a!=e) printf("Mismatch %s family=%d ours=%d system=%d\n",cases[i],family,a,e);
  assert(a==e);
  assert(memcmp(expected,actual,16)==0);
 }
 unsigned char out[16];assert(opennow_inet_pton(AF_INET,"001.2.3.4",out)==0);assert(opennow_inet_pton(AF_UNIX,"anything",out)==-1);assert(errno==EAFNOSUPPORT);
 puts("IP literal classification regressions passed");
}
