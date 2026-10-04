#include "catalog_search.hpp"
#include <cassert>
#include <cstring>
int main() {
    opennow::CatalogSearch input;
    input.move(-1,0);assert(input.selected==32);
    input.move(1,0);assert(input.selected==0);
    input.move(0,-1);assert(input.selected==39);
    input.move(0,1);assert(input.selected==31);
    input.move(0,1);assert(input.selected==34);
    input.move(0,1);assert(input.selected==37);
    input.move(0,1);assert(input.selected==39);
    input.selected=0;input.append();assert(!std::strcmp(input.text,"A"));
    input.selected=26;input.append();assert(!std::strcmp(input.text,"A_"));
    input.selected=28;input.append();assert(!std::strcmp(input.text,"A_ "));
    input.text[0]=0;
    for(unsigned i=30;i<40;++i){input.selected=i;input.append();}
    assert(!std::strcmp(input.text,"1234567890"));
    input.text[0]=0;input.selected=0;
    for(unsigned i=0;i<200;++i)input.append();
    assert(std::strlen(input.text)==127);
    input.erase();assert(std::strlen(input.text)==126);
}
