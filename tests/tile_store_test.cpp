// SPDX-License-Identifier: GPL-3.0-or-later
#include "forwarder/tile_store.hpp"
#include "vendor/cJSON.h"
#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs=std::filesystem;
using namespace opennow;
using namespace opennow::forwarder;
namespace store=opennow::forwarder::store;
namespace {
char reservationPath[1024],savedReservation[1024];
volatile std::sig_atomic_t replaced=0;
void replaceReservation(int) {
    if(::rename(reservationPath,savedReservation)==0&&::mkdir(reservationPath,0755)==0)replaced=1;
}
std::string read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
void write(const fs::path& path,const std::string& text) {
    std::ofstream file(path,std::ios::binary);file<<text;assert(file.good());
}
Game game(const char* id="12345",const char* shop="XBOX") {
    Game value;
    std::snprintf(value.id,sizeof(value.id),"%s",id);
    std::snprintf(value.store,sizeof(value.store),"%s",shop);
    std::strcpy(value.title,"Fixture \"Game\"\\Second\nLine");
    return value;
}
void metadata(const fs::path& stage,const Game& input,const char* titleId=nullptr) {
    char id[10],body[4096];store::initialTitleId(input,id);
    assert(store::forwarderJson(input,body,sizeof(body)));write(stage/"forwarder.json",body);
    assert(store::paramJson(input,titleId?titleId:id,body,sizeof(body)));write(stage/"sce_sys/param.json",body);
}
void stage(const fs::path& path,const Game& input) {
    fs::create_directories(path/"sce_sys");fs::create_directory(path/"sce_module");
    fs::copy_file("assets/forwarder-template/eboot.bin",path/"eboot.bin");
    fs::copy_file("assets/forwarder-template/sce_module/libc.prx",path/"sce_module/libc.prx");
    metadata(path,input);
    write(path/"sce_sys/icon0.png","bounded icon fixture");
    for(const char* name:{"pic0.dds","pic1.dds"}) {
        write(path/"sce_sys"/name,"DDS ");fs::resize_file(path/"sce_sys"/name,3840*2160+148);
    }
}
bool collect(void* context,const Entry& entry) noexcept {
    static_cast<std::vector<Entry>*>(context)->push_back(entry);return true;
}
void clean(const fs::path& root) {
    for(const auto& entry:fs::directory_iterator(root))
        assert(entry.path().filename().string().rfind(".opennow-stage-",0)!=0);
}
void waitOk(pid_t child) {
    int status=0;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
}
}

int main() {
    const auto input=game();char id[10];store::initialTitleId(input,id);
    assert(store::validGame(input)&&store::validTileId(id));
    assert(!std::strcmp(id,"PPSA99287"));
    for(const char* value:{targetTitleId,"PPSA99100","PPSA99008","PPSA99764","PPSA99203","PPSA50011","PPSA00000","../escape","PPSA12a45"})assert(!store::validTileId(value));
    assert(store::validTileId("CUSA12345")&&store::validTileId("LAPY12345"));
    Game bad=input;std::memset(bad.id,'1',sizeof(bad.id));assert(!store::validGame(bad));
    bad=input;std::strcpy(bad.id,"2147483648");assert(!store::validGame(bad));
    bad=input;std::strcpy(bad.id,"0");assert(!store::validGame(bad));
    bad=input;std::strcpy(bad.store,"XBOX\n");assert(!store::validGame(bad));
    char tiny[4];assert(!store::forwarderJson(input,tiny,sizeof(tiny))&&!store::paramJson(input,id,tiny,sizeof(tiny)));
    assert(store::verifyTemplate("assets/forwarder-template"));
    assert(!store::verifyTemplate("/nonexistent/template"));

    char temporary[]="/tmp/opennow-tile-store-XXXXXX";assert(mkdtemp(temporary));
    const fs::path base=temporary,root=base/"homebrew",collision=base/"appmeta",source=base/"source";
    fs::create_directory(root);fs::create_directory(collision);stage(source,input);
    const auto rootText=root.string(),collisionText=collision.string();
    const char* collisions[]={collisionText.c_str()};
    Paths paths;paths.root=rootText.c_str();paths.collisionRoots=collisions;paths.collisionRootCount=1;
    assert(paths.templateRoot==nullptr);
    Paths missing=paths;const auto missingText=(base/"missing").string();missing.root=missingText.c_str();
    assert(store::list(nullptr,nullptr,missing).code==Code::listed);
    assert(store::list(nullptr,nullptr,paths).code==Code::listed);
    const auto first=store::install(source.c_str(),paths);
    assert(first.code==Code::created&&!std::strcmp(first.titleId,id));clean(root);
    const auto tile=root/first.titleId;
    assert(read(tile/"eboot.bin")==read(source/"eboot.bin"));
    assert(read(tile/"sce_module/libc.prx")==read(source/"sce_module/libc.prx"));
    assert(read(tile/"sce_sys/pic0.dds")==read(source/"sce_sys/pic0.dds"));
    std::vector<Entry> entries;
    auto listed=store::list(collect,&entries,paths);
    assert(listed.code==Code::listed&&listed.count==1&&entries.size()==1);
    assert(!std::strcmp(entries[0].game.id,input.id)&&!std::strcmp(entries[0].game.title,input.title));
    write(tile/"forwarder.json",R"({"target":"PPSA99082","args":["--gfn-app-id","12345"],"creator":"Forwarder Manager"})");
    const auto preserved=read(tile/"forwarder.json");
    assert(store::install(source.c_str(),paths).code==Code::alreadyExists);
    assert(read(tile/"forwarder.json")==preserved);

    const auto secret=base/"secret";write(secret,"preserve");
    write(source/"foreign","unexpected");assert(store::install(source.c_str(),paths).code==Code::invalidGame);fs::remove(source/"foreign");
    fs::create_symlink(secret,source/"linked");assert(store::install(source.c_str(),paths).code==Code::invalidGame);fs::remove(source/"linked");
    fs::rename(source/"sce_sys/icon0.png",base/"saved-icon");
    fs::create_symlink(secret,source/"sce_sys/icon0.png");
    assert(store::install(source.c_str(),paths).code==Code::invalidGame);
    fs::remove(source/"sce_sys/icon0.png");fs::rename(base/"saved-icon",source/"sce_sys/icon0.png");
    fs::rename(source/"sce_sys",base/"saved-sys");fs::create_directory_symlink(base/"saved-sys",source/"sce_sys");
    assert(store::install(source.c_str(),paths).code==Code::invalidGame);
    fs::remove(source/"sce_sys");fs::rename(base/"saved-sys",source/"sce_sys");
    fs::create_directory_symlink(source,base/"source-link");
    assert(store::install((base/"source-link").c_str(),paths).code==Code::invalidGame);
    fs::create_directory_symlink(base,base/"parent-link");
    assert(store::install((base/"parent-link/source").c_str(),paths).code==Code::invalidGame);
    const auto goodMetadata=read(source/"forwarder.json");
    write(source/"forwarder.json","{malformed");assert(store::install(source.c_str(),paths).code==Code::invalidGame);
    write(source/"forwarder.json",std::string(16384,'x'));assert(store::install(source.c_str(),paths).code==Code::invalidGame);
    write(source/"forwarder.json",goodMetadata);
    fs::resize_file(source/"sce_sys/pic0.dds",10);
    assert(store::install(source.c_str(),paths).code==Code::invalidGame);
    fs::resize_file(source/"sce_sys/pic0.dds",3840*2160+148);
    fs::resize_file(source/"sce_sys/icon0.png",3*1024*1024);
    assert(store::install(source.c_str(),paths).code==Code::invalidGame);write(source/"sce_sys/icon0.png","icon");
    const auto program=read(source/"eboot.bin");auto corrupt=program;corrupt[0]^=1;
    write(source/"eboot.bin",corrupt);assert(store::install(source.c_str(),paths).code==Code::templateError);
    write(source/"eboot.bin",program);clean(root);

    fs::create_directory(root/"PPSA99201");write(root/"PPSA99201/foreign","keep");
    assert(store::remove("PPSA99201",paths).code==Code::conflict&&read(root/"PPSA99201/foreign")=="keep");
    fs::create_symlink(secret,tile/"linked");
    assert(store::remove(id,paths).code==Code::removed&&read(secret)=="preserve");
    assert(store::remove(id,paths).code==Code::notFound);
    const auto tomb=root/(std::string(".opennow-remove-")+id),owner=root/(std::string(".opennow-remove-")+id+".owner");
    fs::create_directory(tomb);write(tomb/"partial","remaining");
    assert(store::remove(id,paths).code==Code::conflict&&fs::exists(tomb/"partial"));
    write(owner,std::string("OpenNOW PS5 removal ")+id+"\n");
    assert(store::remove(id,paths).code==Code::removed&&!fs::exists(tomb)&&!fs::exists(owner));
    write(owner,std::string("OpenNOW PS5 removal ")+id+"\n");
    assert(store::remove(id,paths).code==Code::removed&&!fs::exists(owner));

    fs::create_directory(collision/id);
    const auto collided=store::install(source.c_str(),paths);
    assert(collided.code==Code::created&&std::strcmp(collided.titleId,id));
    auto* param=cJSON_Parse(read(root/collided.titleId/"sce_sys/param.json").c_str());assert(param);
    assert(!std::strcmp(cJSON_GetObjectItemCaseSensitive(param,"titleId")->valuestring,collided.titleId));cJSON_Delete(param);
    assert(store::remove(collided.titleId,paths).ok());fs::remove(collision/id);
    fs::create_directories(collision/"different-folder/sce_sys");
    char body[4096];assert(store::paramJson(input,id,body,sizeof(body)));write(collision/"different-folder/sce_sys/param.json",body);
    const auto embedded=store::install(source.c_str(),paths);
    assert(embedded.code==Code::created&&std::strcmp(embedded.titleId,id));
    assert(store::remove(embedded.titleId,paths).ok());fs::remove_all(collision/"different-folder");
    fs::create_directory(root/id);write(root/id/"foreign","never overwritten");
    const auto foreign=store::install(source.c_str(),paths);
    assert(foreign.code==Code::created&&std::strcmp(foreign.titleId,id)&&read(root/id/"foreign")=="never overwritten");
    assert(store::remove(foreign.titleId,paths).ok());fs::remove_all(root/id);

    struct rlimit oldLimit;assert(getrlimit(RLIMIT_FSIZE,&oldLimit)==0);
    auto smallLimit=oldLimit;smallLimit.rlim_cur=65536;
    const auto oldSignal=std::signal(SIGXFSZ,SIG_IGN);assert(setrlimit(RLIMIT_FSIZE,&smallLimit)==0);
    const auto failed=store::install(source.c_str(),paths);
    assert(setrlimit(RLIMIT_FSIZE,&oldLimit)==0);std::signal(SIGXFSZ,oldSignal);
    assert(failed.code==Code::ioError);clean(root);assert(!fs::exists(root/id));
    std::snprintf(reservationPath,sizeof(reservationPath),"%s",(root/id).c_str());
    std::snprintf(savedReservation,sizeof(savedReservation),"%s",(base/"saved-reservation").c_str());
    std::signal(SIGXFSZ,replaceReservation);assert(setrlimit(RLIMIT_FSIZE,&smallLimit)==0);
    const auto replacedFailure=store::install(source.c_str(),paths);
    assert(setrlimit(RLIMIT_FSIZE,&oldLimit)==0);std::signal(SIGXFSZ,oldSignal);
    assert(replacedFailure.code==Code::ioError&&replaced&&fs::exists(root/id)&&fs::is_empty(root/id));
    assert(fs::is_empty(base/"saved-reservation"));clean(root);
    fs::remove(root/id);fs::remove(base/"saved-reservation");
    const auto abandoned=root/".opennow-stage-interrupted";
    fs::create_directories(abandoned/"sce_sys");write(abandoned/"sce_sys/param.json","{partial");
    assert(store::install(source.c_str(),paths).code==Code::created);
    fs::remove_all(abandoned);
    assert(store::remove(id,paths).ok());
    const auto upload=root/".opennow-upload-ready",partialUpload=root/".opennow-upload-partial";
    stage(upload,input);
    fs::create_directories(partialUpload/"sce_sys");write(partialUpload/"sce_sys/param.json","{partial");
    const auto uploaded=store::install(upload.c_str(),paths);
    assert(uploaded.code==Code::created&&!std::strcmp(uploaded.titleId,id));
    assert(store::remove(id,paths).ok());
    fs::remove_all(upload);fs::remove_all(partialUpload);

    int pipefd[2];assert(pipe(pipefd)==0);
    pid_t child=fork();assert(child>=0);
    if(!child) {
        close(pipefd[0]);const auto value=store::install(source.c_str(),paths);
        assert(::write(pipefd[1],&value,sizeof(value))==sizeof(value));_exit(0);
    }
    close(pipefd[1]);const auto concurrent=store::install(source.c_str(),paths);
    Result other;assert(::read(pipefd[0],&other,sizeof(other))==sizeof(other));close(pipefd[0]);waitOk(child);
    assert((concurrent.code==Code::created&&other.code==Code::alreadyExists)||(other.code==Code::created&&concurrent.code==Code::alreadyExists));
    assert(!std::strcmp(concurrent.titleId,other.titleId));assert(store::remove(id,paths).ok());
    assert(pipe(pipefd)==0);child=fork();assert(child>=0);
    if(!child) {
        close(pipefd[0]);const int fd=open(root.c_str(),O_RDONLY|O_DIRECTORY);assert(fd>=0&&flock(fd,LOCK_EX)==0);
        assert(::write(pipefd[1],"x",1)==1);for(;;)pause();
    }
    close(pipefd[1]);char ready;assert(::read(pipefd[0],&ready,1)==1);close(pipefd[0]);
    assert(kill(child,SIGKILL)==0);int status;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status));
    assert(store::install(source.c_str(),paths).code==Code::created);assert(store::remove(id,paths).ok());
    for(unsigned i=99200;i<=99899;++i)fs::create_directory(root/("PPSA"+std::to_string(i)));
    assert(store::install(source.c_str(),paths).code==Code::noFreeTitleId);
    assert(store::remove(targetTitleId,paths).code==Code::invalidGame);
    fs::remove_all(base);
    std::puts("tile store tests passed: bounded staging, ownership, collisions, idempotence, atomic copy failure, process locking and interrupted deletion");
}
