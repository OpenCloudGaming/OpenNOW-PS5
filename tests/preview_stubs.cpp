#include <cstddef>
extern "C" {
#include "platform/ps5-pad.h"
int sceUserServiceGetInitialUser(int*) {return -1;}
int scePadOpen(int,int,int,void*) {return -1;}
int scePadReadState(int,PS5_PadData*) {return -1;}
}
