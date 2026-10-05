// SPDX-License-Identifier: GPL-3.0-or-later
#define OPENNOW_GPU 1
#define GL_GLEXT_PROTOTYPES 1
#include "../src/stream/native/gpu_presenter.cpp"
#include <cassert>
#include <cstring>
#include <utility>

namespace {
std::vector<opennow::video::VideoRect> viewports;
float clearAlpha=0,cropX=0,cropY=0;
unsigned clears=0,draws=0;
void image(GLenum,void*) {}
}
extern "C" {
void opennow_media_note(const char*) {}
void* ps5_opengl_memory_image_create(void*,unsigned,unsigned,unsigned,unsigned,unsigned) {return reinterpret_cast<void*>(1);}
void ps5_opengl_memory_image_destroy(void*) {}
int ps5_opengl_set_scanout_format(std::uint64_t,std::int32_t[4]) {return 0;}
void glFinish() {}
void glDeleteTextures(GLsizei,const GLuint*) {}
void glGenTextures(GLsizei n,GLuint* textures) {for(int i=0;i<n;++i)textures[i]=i+1;}
void glUseProgram(GLuint) {}
void glBindVertexArray(GLuint) {}
void glDisable(GLenum) {}
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h) {viewports.push_back({static_cast<unsigned>(x),static_cast<unsigned>(y),static_cast<unsigned>(w),static_cast<unsigned>(h)});}
void glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat alpha) {assert(r==0&&g==0&&b==0);clearAlpha=alpha;}
void glClear(GLbitfield mask) {assert(mask==GL_COLOR_BUFFER_BIT);assert((viewports.back()==opennow::video::VideoRect{0,0,3840,2160}));++clears;}
void glActiveTexture(GLenum) {}
void glBindTexture(GLenum,GLuint) {}
void glTexParameteri(GLenum,GLenum,GLint) {}
GLint glGetUniformLocation(GLuint,const GLchar* name) {return !std::strcmp(name,"crop")?1:0;}
void glUniform1i(GLint,GLint) {}
void glUniform2f(GLint location,GLfloat x,GLfloat y) {assert(location==1);cropX=x;cropY=y;}
void glDrawArrays(GLenum mode,GLint first,GLsizei count) {assert(mode==GL_TRIANGLES&&first==0&&count==3);++draws;}
GLenum glGetError() {return GL_NO_ERROR;}
void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*) {}
}

int main() {
 using namespace opennow;
 using namespace opennow::video;
 gpu::ready=true;gpu::width=3840;gpu::height=2160;gpu::imageTarget=image;
 std::vector<std::uint8_t> pixels(3840u*2176u*3u);
 for(const auto codec:{NativeCodec::h264,NativeCodec::hevc_main10_sdr,NativeCodec::hevc_main10}){
  for(const auto size:{std::pair{1440u,1080u},std::pair{2560u,1080u},std::pair{3840u,2160u}}){
   const auto mode=*nativeMode(codec,size.first,size.second,60);
   const unsigned bytes=mode.storage==SampleStorage::low_aligned_10bit?2:1;
   const NativeSurface surface{pixels.data(),pixels.size(),size.first,mode.coded_height,mode.pitch_components,mode.pitch_components*bytes,mode.codec_type,1};
   viewports.clear();const auto previousClears=clears,previousDraws=draws;
   assert(gpu::drawVideo(surface,mode));
   assert(viewports.size()==3);
   assert((viewports.front()==VideoRect{0,0,3840,2160}));
   assert(viewports[1]==fitVideoRect(size.first,size.second,3840,2160));
   assert(viewports.back()==viewports.front());
   assert(clears==previousClears+1&&draws==previousDraws+1);
   assert(clearAlpha==(mode.hdr?192.0f/255.0f:1.0f));
   assert(cropX==float(mode.visible_width)/surface.pitch_components&&cropY==float(mode.visible_height)/surface.height);
  }
 }
 viewports.clear();std::uint32_t pixel=0;
 gpu::drawInterface(&pixel);
 assert((viewports==std::vector<VideoRect>{{0,0,3840,2160}}));
 assert(!gpu::hdrScanout);
 gpu::qualified={};
 gpu::qualified[0]={*nativeMode(NativeCodec::h264,1920,1080,60),{true,true,true,true,false,3840,2160,120}};
 gpu::qualified[1]={*nativeMode(NativeCodec::h264,3840,2160,120),{true,true,true,true,false,3840,2160,30}};
 auto request=settingsFor(StreamProfile::native_1080);
 assert(gpu::allocationFor(request)->visible_width==1920);
 request.fps=91;assert(!gpu::settingsAvailable(request));
 request.width=3840;request.height=2160;request.fps=60;assert(!gpu::settingsAvailable(request));
 request.fps=30;assert(gpu::allocationFor(request)->visible_width==3840);
 assert(gpu::bestSettings().mode==VideoMode::h264Hardware&&gpu::bestSettings().width==3840&&gpu::bestSettings().fps==30);
 gpu::qualified={};assert(!gpu::allocationFor(request));
 assert(gpu::settingsAvailable(StreamSettings{}));
 gpu::ready=false;assert(!gpu::allocationFor(request));
}
