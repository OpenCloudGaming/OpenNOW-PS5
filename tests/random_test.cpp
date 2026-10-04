#include "random.hpp"
#include <cassert>
#include <cstring>
#include <array>
static unsigned calls;
static unsigned failAt,shortAt;
extern "C" int sysctlbyname(const char* name,void* data,std::size_t* size,const void* input,std::size_t length) {
    assert(std::strcmp(name,"kern.rng_pseudo")==0 && input==nullptr && length==0 && *size==64);
    ++calls;
    if (calls==failAt) return -1;
    if (calls==shortAt) {*size=0;return 0;}
    std::memset(data,static_cast<int>(calls),*size);return 0;
}
int main() {
    std::array<unsigned char,130> output{};
    assert(opennow::randomBytes(output.data(),output.size()) && calls==3);
    assert(output[0]==1 && output[63]==1 && output[64]==2 && output[129]==3);
    calls=0;failAt=2;
    assert(!opennow::randomBytes(output.data(),output.size()));
    for (auto b:output) assert(b==0);
    calls=0;failAt=0;shortAt=1;output.fill(42);
    assert(!opennow::randomBytes(output.data(),16));
    for (unsigned i=0;i<16;++i) assert(output[i]==0);
    assert(output[16]==42);
    assert(!opennow::randomBytes(nullptr,1));assert(opennow::randomBytes(nullptr,0));
}
