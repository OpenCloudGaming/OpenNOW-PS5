// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/forwarder.hpp"
#include "app_storage.h"
#include "vendor/cJSON.h"
#include "../vendor/ps5-forwarder-format/include/psfwd.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs=std::filesystem;
using namespace opennow;
using namespace opennow::forwarder;
namespace {
bool failDownload=false;
unsigned downloads=0;
std::vector<Entry> entries;
bool download(void*,const char*,unsigned char* data,std::size_t size,std::size_t& used,const std::atomic_bool& stop) noexcept {
    ++downloads;
    if(failDownload||stop.load()||size<4)return false;
    data[0]=0xd8;used=4;return true;
}
bool collect(void*,const Entry& entry) noexcept {entries.push_back(entry);return true;}
std::string read(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),{}};
}
void write(const fs::path& path,const std::string& data) {std::ofstream file(path,std::ios::binary);file<<data;assert(file.good());}
Game game() {
    Game value;
    std::strcpy(value.id,"12345");std::strcpy(value.store,"XBOX");
    std::strcpy(value.title,"Fixture \"Game\"\\Second\nLine");
    std::strcpy(value.art,"https://img.nvidiagrid.net/apps/12345/cover.jpg");
    std::strcpy(value.hero,"https://img.nvidiagrid.net/apps/12345/hero.jpg");
    return value;
}
void equalJson(const char* a,const char* b) {
    auto* first=cJSON_Parse(a);auto* second=cJSON_Parse(b);
    assert(first&&second&&cJSON_Compare(first,second,1));
    cJSON_Delete(first);cJSON_Delete(second);
}
struct ProgressState {
    std::string root,stageRoot;
    std::atomic_bool cancel{false};
    bool cancelBeforePublish=false,corruptStage=false;
    unsigned calls=0;
};
void progress(void* context,const Progress& value) noexcept {
    auto& state=*static_cast<ProgressState*>(context);++state.calls;
    if(value.phase==Phase::checking||value.phase==Phase::complete)return;
    for(const auto& path:fs::directory_iterator(state.root)) {
        if(path.path().filename().string().rfind("PPSA",0)!=0)continue;
        assert(!fs::exists(path.path()/"sce_sys/param.json"));
    }
    if(value.phase==Phase::installing&&state.corruptStage)
        for(const auto& path:fs::directory_iterator(state.stageRoot))write(path.path()/"eboot.bin","foreign program");
    if(state.cancelBeforePublish&&value.phase==Phase::writing)state.cancel.store(true);
}
void noStaging(const fs::path& root) {
    for(const auto& entry:fs::directory_iterator(root))assert(entry.path().filename().string().rfind(".opennow-stage-",0)!=0);
}
}

namespace opennow::art {
bool httpsFetch(void*,const char*,unsigned char*,std::size_t,std::size_t&,const std::atomic_bool&) noexcept {assert(false);return false;}
bool requestUrl(const char* source,Kind,char* out,std::size_t size) noexcept {
    const int n=std::snprintf(out,size,"%s",source);return n>0&&static_cast<std::size_t>(n)<size;
}
bool decode(const unsigned char* bytes,std::size_t size,unsigned width,unsigned height,std::uint32_t* out) noexcept {
    if(size!=4||bytes[0]!=0xd8)return false;
    for(std::size_t i=0;i<std::size_t(width)*height;++i)out[i]=0xffabcdefU;
    return true;
}
}

int main() {
    const auto input=game();
    const Code failures[]={Code::invalidGame,Code::stagingError,Code::transportError,Code::protocolError,Code::uploadError,Code::ioError};
    for(unsigned i=0;i<std::size(failures);++i)for(unsigned j=0;j<i;++j)
        assert(std::strcmp(message(failures[i]),message(failures[j])));
    char param[4096],forward[4096],upstream[4096];
    assert(paramJson(input,"PPSA99200",param,sizeof(param)));
    assert(forwarderJson(input,forward,sizeof(forward)));
    const char* argv[]={"--gfn-app-id",input.id,"--store",input.store};
    psfwd_spec spec{};spec.title_id="PPSA99200";spec.name=input.title;
    spec.target=targetTitleId;spec.argc=4;spec.argv=argv;spec.creator="OpenNOW PS5";
    assert(psfwd_param_json(&spec,upstream,sizeof(upstream)));equalJson(param,upstream);
    assert(psfwd_forwarder_json(&spec,upstream,sizeof(upstream)));equalJson(forward,upstream);
    char tiny[8];assert(!paramJson(input,"PPSA99200",tiny,sizeof(tiny))&&!forwarderJson(input,tiny,sizeof(tiny)));
    assert(!paramJson(input,targetTitleId,param,sizeof(param))&&!paramJson(input,"../escape",param,sizeof(param)));
    for(const char* id:{targetTitleId,"PPSA99100","PPSA99764","PPSA99203","PPSA99008","PPSA50011","PPSA00000","PPSA12a45"})assert(!validTileId(id));
    Game bad=input;bad.id[1]='x';assert(!forwarderJson(bad,forward,sizeof(forward)));
    bad=input;std::strcpy(bad.id,"0");assert(!forwarderJson(bad,forward,sizeof(forward)));
    bad=input;std::strcpy(bad.id,"2147483648");assert(!forwarderJson(bad,forward,sizeof(forward)));
    bad=input;std::strcpy(bad.store,"XBOX\n");assert(!forwarderJson(bad,forward,sizeof(forward)));
    bad=input;std::memset(bad.title,'x',sizeof(bad.title));assert(!paramJson(bad,"PPSA99200",param,sizeof(param)));

    char temporary[]="/tmp/opennow-forwarder-XXXXXX";assert(mkdtemp(temporary));
    const fs::path root=fs::path(temporary)/"homebrew",inventory=fs::path(temporary)/"appmeta";
    fs::create_directories(root);fs::create_directory(inventory);
    const auto rootText=root.string(),inventoryText=inventory.string(),stageText=(fs::path(temporary)/"tile-stage").string();
    const char* collisions[]={inventoryText.c_str()};
    Options options;options.paths.root=rootText.c_str();options.paths.collisionRoots=collisions;options.paths.collisionRootCount=1;
    options.paths.stageRoot=stageText.c_str();
    const auto sourceTemplate=fs::absolute("assets/forwarder-template").string();options.paths.templateRoot=sourceTemplate.c_str();
    options.fetch=download;
    ProgressState state;state.root=rootText;state.stageRoot=stageText;
    Job job{&state.cancel,progress,&state};
    const auto first=create(input,options,job);
    assert(first.code==Code::created&&first.ok()&&state.calls>=5&&downloads==2);
    assert(validTileId(first.titleId)&&std::strcmp(first.titleId,targetTitleId));
    const fs::path tile=root/first.titleId;
    assert(read(tile/"eboot.bin")==read(fs::path(sourceTemplate)/"eboot.bin"));
    assert(read(tile/"sce_module/libc.prx")==read(fs::path(sourceTemplate)/"sce_module/libc.prx"));
    assert(fs::file_size(tile/"sce_sys/pic0.dds")==3840*2160+148);
    assert(read(tile/"sce_sys/pic0.dds")==read(tile/"sce_sys/pic1.dds"));
    psfwd_info standard{};assert(psfwd_read(tile.c_str(),&standard));
    assert(standard.format==1&&standard.argc==4&&standard.has_icon&&standard.has_backgrounds);
    assert(!std::strcmp(standard.target,targetTitleId)&&!std::strcmp(standard.argv[1],input.id));
    write(tile/"forwarder.json",R"({"format":"ps5-forwarder/1","target":"PPSA99082","args":["--store","XBOX","--gfn-app-id","12345"],"creator":"Forwarder Manager","futureKey":true})");
    const auto second=create(input,options);assert(second.code==Code::alreadyExists&&!std::strcmp(second.titleId,first.titleId)&&downloads==2);
    assert(list(collect,nullptr,options.paths).count==1&&entries.size()==1&&!std::strcmp(entries[0].game.store,"XBOX"));
    write(tile/"forwarder.json",R"({"format":"ps5-forwarder/1","target":"PPSA99082","args":["--gfn-app-id","12345"],"creator":"Forwarder Manager"})");
    assert(create(input,options).code==Code::alreadyExists);
    fs::create_directory(root/"PPSA99201");write(root/"PPSA99201"/"foreign","untouched");
    assert(remove("PPSA99201",options.paths).code==Code::conflict&&fs::exists(root/"PPSA99201"/"foreign"));
    const auto secret=fs::path(temporary)/"secret";write(secret,"preserve");
    fs::create_symlink(secret,tile/"linked");
    assert(remove(first.titleId,options.paths).code==Code::removed&&read(secret)=="preserve");
    assert(remove(first.titleId,options.paths).code==Code::notFound);
    const auto tomb=root/(std::string(".opennow-remove-")+first.titleId);
    fs::create_directory(tomb);write(tomb/"partial","remaining bytes");
    write(root/(std::string(".opennow-remove-")+first.titleId+".owner"),std::string("OpenNOW PS5 removal ")+first.titleId+"\n");
    assert(remove(first.titleId,options.paths).code==Code::removed&&!fs::exists(tomb));
    assert(remove(targetTitleId,options.paths).code==Code::invalidGame);
    entries.clear();assert(list(collect,nullptr,options.paths).count==0&&entries.empty());

    fs::create_directory(inventory/first.titleId);
    const auto allocated=create(input,options);assert(allocated.code==Code::created&&std::strcmp(allocated.titleId,first.titleId));
    assert(remove(allocated.titleId,options.paths).ok());fs::remove(inventory/first.titleId);
    fs::create_directories(inventory/"different-folder"/"sce_sys");
    assert(paramJson(input,first.titleId,param,sizeof(param)));write(inventory/"different-folder"/"sce_sys/param.json",param);
    const auto embedded=create(input,options);assert(embedded.code==Code::created&&std::strcmp(embedded.titleId,first.titleId));
    assert(remove(embedded.titleId,options.paths).ok());fs::remove_all(inventory/"different-folder");
    failDownload=true;assert(create(input,options).code==Code::artworkError);failDownload=false;noStaging(root);
    state.cancelBeforePublish=true;state.cancel.store(false);
    assert(create(input,options,job).code==Code::cancelled);noStaging(root);
    state.cancelBeforePublish=false;state.cancel.store(false);state.corruptStage=true;
    assert(create(input,options,job).code==Code::templateError);noStaging(root);assert(fs::is_empty(stageText));
    bool kept=false;for(const auto& entry:fs::directory_iterator(root))if(fs::exists(entry.path()/"foreign"))kept=true;assert(kept);
    state.corruptStage=false;
    const auto corruptTemplate=(fs::path(temporary)/"bad-template").string();fs::create_directory(corruptTemplate);
    write(fs::path(corruptTemplate)/"eboot.bin","not the standard forwarder");
    options.paths.templateRoot=corruptTemplate.c_str();assert(create(input,options).code==Code::templateError);options.paths.templateRoot=sourceTemplate.c_str();

    for(unsigned i=99200;i<=99899;++i)fs::create_directory(root/("PPSA"+std::to_string(i)));
    assert(create(input,options).code==Code::noFreeTitleId);

    assert(!std::strcmp(opennow_storage_file("settings.bin"),"/download0/opennow/settings.bin"));
    assert(!std::strcmp(opennow_app_root(),"/app0"));
    assert(!std::strcmp(opennow_storage_root(),"/download0/opennow"));
    assert(!std::strcmp(opennow_storage_file("tile-stage"),"/download0/opennow/tile-stage"));
    assert(!std::strcmp(opennow_app_file("opennow-tile-installer.elf"),"/app0/opennow-tile-installer.elf"));
    assert(!std::strcmp(opennow_app_file("assets/cacert.pem"),"/app0/assets/cacert.pem"));
    assert(!*opennow_storage_file("unknown.bin"));
    assert(fs::is_empty(stageText));
    fs::remove_all(temporary);
    std::puts("forwarder tests passed: standard metadata, title allocation, atomic publication, cancellation and safe removal");
}
