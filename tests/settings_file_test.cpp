// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_file.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace opennow;
namespace {
void writeRaw(const std::string& path,const void* data,std::size_t size) {
    FILE* file=std::fopen(path.c_str(),"wb");
    assert(file);
    assert(std::fwrite(data,1,size,file)==size);
    std::fclose(file);
}
}

int main() {
    char root[]="/tmp/opennow-settings-XXXXXX";
    assert(mkdtemp(root));
    const std::string path=std::string(root)+"/settings.bin";
    settingsFile::Saved saved;
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::missing&&!saved.hasProfile);
    assert(settingsFile::save(path.c_str(),{true,StreamProfile::native_hdr120})==0);
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::loaded);
    assert(saved.hasProfile&&saved.profile==StreamProfile::native_hdr120);
    assert(settingsFile::save(path.c_str(),{true,StreamProfile::compatibility})==0);
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::loaded&&saved.profile==StreamProfile::compatibility);
    FILE* temporary=std::fopen((path+".tmp").c_str(),"rb");
    assert(!temporary);

    settingsFile::Record record{{'O','N','S','T'},1,static_cast<std::int32_t>(StreamProfile::smooth),0};
    record.checksum=settingsFile::checksum(record);
    writeRaw(path,&record,sizeof(record));
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::loaded&&saved.profile==StreamProfile::smooth);
    auto corrupt=record;corrupt.checksum^=1;
    writeRaw(path,&corrupt,sizeof(corrupt));
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt&&!saved.hasProfile);
    corrupt=record;corrupt.magic[0]='X';corrupt.checksum=settingsFile::checksum(corrupt);
    writeRaw(path,&corrupt,sizeof(corrupt));
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);
    corrupt=record;corrupt.version=2;corrupt.checksum=settingsFile::checksum(corrupt);
    writeRaw(path,&corrupt,sizeof(corrupt));
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);
    for(std::int32_t profile:{-1,static_cast<std::int32_t>(StreamProfile::count),1000}) {
        corrupt=record;corrupt.profile=profile;corrupt.checksum=settingsFile::checksum(corrupt);
        writeRaw(path,&corrupt,sizeof(corrupt));
        assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);
    }
    writeRaw(path,&record,sizeof(record)-1);
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);
    unsigned char longer[sizeof(record)+1]{};
    std::memcpy(longer,&record,sizeof(record));
    writeRaw(path,longer,sizeof(longer));
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::corrupt);

    assert(settingsFile::save(path.c_str(),{false,StreamProfile::quality})==EINVAL);
    const std::string missingDirectory=std::string(root)+"/missing/settings.bin";
    assert(settingsFile::save(missingDirectory.c_str(),{true,StreamProfile::quality})==ENOENT);
    assert(settingsFile::load(missingDirectory.c_str(),saved)==settingsFile::Status::missing);

    assert(settingsFile::save(path.c_str(),{true,StreamProfile::quality})==0);
    assert(settingsFile::clear(path.c_str())==0);
    assert(settingsFile::load(path.c_str(),saved)==settingsFile::Status::missing);
    assert(settingsFile::clear(path.c_str())==0);
    const std::string account=std::string(root)+"/account.bin";
    writeRaw(account,"token",5);
    assert(settingsFile::save(path.c_str(),{true,StreamProfile::smooth})==0);
    assert(settingsFile::clear(path.c_str())==0);
    FILE* kept=std::fopen(account.c_str(),"rb");
    assert(kept);
    std::fclose(kept);
    std::remove(account.c_str());
    std::remove(root);
    std::puts("Persistent settings round-trip, corruption and save-error regressions passed");
}
