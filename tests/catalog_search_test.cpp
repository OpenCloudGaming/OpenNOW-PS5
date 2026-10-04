#include "catalog_search.hpp"
#include <cassert>
#include <cstring>
int main() {
    opennow::CatalogSearch input;
    input.move(-1,-1);assert(input.selected==39);
    input.move(1,1);assert(input.selected==0);
    input.append();assert(!std::strcmp(input.text,"A"));
    input.selected=36;input.append();assert(!std::strcmp(input.text,"A "));
    input.erase();input.erase();input.erase();assert(!*input.text);
    for(unsigned i=0;i<200;++i)input.append();
    assert(std::strlen(input.text)==127);
    input.erase();assert(std::strlen(input.text)==126);
}
