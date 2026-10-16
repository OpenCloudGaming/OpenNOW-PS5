#include "catalog_search.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
int main() {
    opennow::CatalogSearch input;
    const char* rows[]={"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM","_- ."};
    unsigned index=0;
    for(unsigned r=0;r<5;++r) {
        const unsigned length=std::strlen(rows[r]);
        for(unsigned col=0;col<length;++col,++index) {
            assert(opennow::CatalogSearch::keys[index]==rows[r][col]);
            assert(opennow::CatalogSearch::row(index)==r);
            assert(opennow::CatalogSearch::column(index)==col);
            input.selected=index;input.move(-1,0);
            assert(input.selected==index-col+(col?col-1:length-1));
            input.selected=index;input.move(1,0);
            assert(input.selected==index-col+(col+1)%length);
            for(int y:{-1,1}) {
                const unsigned target=(r+5+y)%5, targetLength=std::strlen(rows[target]);
                input.selected=index;input.move(0,y);
                assert(opennow::CatalogSearch::row(input.selected)==target);
                assert(opennow::CatalogSearch::column(input.selected)==(col<targetLength?col:targetLength-1));
            }
        }
    }
    assert(index==opennow::CatalogSearch::count);
    for(unsigned i=0;i<opennow::CatalogSearch::count;++i){input.selected=i;input.append();}
    assert(!std::strcmp(input.text,"1234567890QWERTYUIOPASDFGHJKLZXCVBNM_- ."));
    input.text[0]=0;input.selected=0;
    for(unsigned i=0;i<200;++i)input.append();
    assert(std::strlen(input.text)==127);
    input.erase();assert(std::strlen(input.text)==126);
}
