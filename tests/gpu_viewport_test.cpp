// SPDX-License-Identifier: GPL-3.0-or-later
#define OPENNOW_GPU 1
#define GL_GLEXT_PROTOTYPES 1
#include "../src/stream/native/gpu_presenter.cpp"
#include <cassert>
#include <cstring>
#include <set>
#include <utility>

namespace {
std::vector<opennow::video::VideoRect> viewports;
float clearAlpha=0,cropX=0,cropY=0;
unsigned clears=0,draws=0,swaps=0,uploads=0;
GLuint nextName=1,boundFbo=0;
GLenum fboStatus=GL_FRAMEBUFFER_COMPLETE;
std::set<GLuint> textures,fbos;
std::vector<std::pair<int,GLuint>> drawn;
int shaderMode=-1;
unsigned long long now=1000000;
void image(GLenum,void*) {}
}
extern "C" {
void opennow_media_note(const char*) {}
unsigned long long sceKernelGetProcessTime() {return now;}
EGLBoolean eglSwapBuffers(EGLDisplay,EGLSurface) {++swaps;return EGL_TRUE;}
EGLBoolean eglMakeCurrent(EGLDisplay,EGLSurface,EGLSurface,EGLContext) {return EGL_TRUE;}
EGLBoolean eglDestroySurface(EGLDisplay,EGLSurface) {return EGL_TRUE;}
EGLBoolean eglDestroyContext(EGLDisplay,EGLContext) {return EGL_TRUE;}
EGLBoolean eglTerminate(EGLDisplay) {return EGL_TRUE;}
void glDeleteVertexArrays(GLsizei,const GLuint*) {}
void glDeleteProgram(GLuint) {}
void glGenFramebuffers(GLsizei n,GLuint* names) {for(int i=0;i<n;++i)fbos.insert(names[i]=nextName++);}
void glDeleteFramebuffers(GLsizei n,const GLuint* names) {for(int i=0;i<n;++i)assert(fbos.erase(names[i]));}
void glBindFramebuffer(GLenum,GLuint fbo) {assert(!fbo||fbos.count(fbo));boundFbo=fbo;}
void glFramebufferTexture2D(GLenum,GLenum,GLenum,GLuint texture,GLint) {assert(textures.count(texture));}
GLenum glCheckFramebufferStatus(GLenum) {return fboStatus;}
void* ps5_opengl_memory_image_create(void*,unsigned,unsigned,unsigned,unsigned,unsigned) {return reinterpret_cast<void*>(1);}
void ps5_opengl_memory_image_destroy(void*) {}
int ps5_opengl_set_scanout_format(std::uint64_t,std::int32_t[4]) {return 0;}
void glFinish() {}
void glDeleteTextures(GLsizei n,const GLuint* names) {for(int i=0;i<n;++i)if(names[i])assert(textures.erase(names[i]));}
void glGenTextures(GLsizei n,GLuint* names) {for(int i=0;i<n;++i)textures.insert(names[i]=nextName++);}
void glUseProgram(GLuint) {}
void glBindVertexArray(GLuint) {}
void glDisable(GLenum) {}
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h) {viewports.push_back({static_cast<unsigned>(x),static_cast<unsigned>(y),static_cast<unsigned>(w),static_cast<unsigned>(h)});}
void glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat alpha) {assert(r==0&&g==0&&b==0);clearAlpha=alpha;}
void glClear(GLbitfield mask) {assert(mask==GL_COLOR_BUFFER_BIT);drawn.push_back({-1,boundFbo});assert((viewports.back()==opennow::video::VideoRect{0,0,3840,2160}));++clears;}
void glActiveTexture(GLenum) {}
void glBindTexture(GLenum,GLuint) {}
void glTexParameteri(GLenum,GLenum,GLint) {}
GLint glGetUniformLocation(GLuint,const GLchar* name) {return !std::strcmp(name,"crop")?1:!std::strcmp(name,"mode")?2:0;}
void glUniform1i(GLint location,GLint value) {if(location==2)shaderMode=value;}
void glUniform2f(GLint location,GLfloat x,GLfloat y) {assert(location==1);cropX=x;cropY=y;}
void glDrawArrays(GLenum mode,GLint first,GLsizei count) {assert(mode==GL_TRIANGLES&&first==0&&count==3);++draws;drawn.push_back({shaderMode,boundFbo});}
GLenum glGetError() {return GL_NO_ERROR;}
void glTexImage2D(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void* data) {if(data)++uploads;}
}

using Draws=std::vector<std::pair<int,GLuint>>;
void overlayScenarios() {
 using namespace opennow;
 using namespace opennow::video;
 std::vector<std::uint8_t> pixels(3840u*2176u*3u);
 const auto hdr=*nativeMode(NativeCodec::hevc_main10,3840,2160,60),sdr=*nativeMode(NativeCodec::h264,1920,1080,60);
 const NativeSurface hdrSurface{pixels.data(),pixels.size(),3840,hdr.coded_height,hdr.pitch_components,hdr.pitch_components*2,hdr.codec_type,1};
 const NativeSurface sdrSurface{pixels.data(),pixels.size(),1920,sdr.coded_height,sdr.pitch_components,sdr.pitch_components,sdr.codec_type,1};
 std::uint32_t ui[1]{};
 const auto start=[&]{drawn.clear();return swaps;};
 const auto liveBefore=textures.size();
 auto s=start();
 assert(gpu::drawVideo(sdrSurface,sdr)&&!gpu::hasRetainedVideo()&&fbos.empty());
 assert(gpu::present(ui)&&swaps==s+1&&drawn.size()==2&&drawn[1]==std::pair(1,0U));
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{0,0}}));

 gpu::setOverlay(true,true);assert(gpu::overlayPending());
 s=start();assert(gpu::present(ui)&&swaps==s&&drawn.empty());
 assert(gpu::drawVideo(hdrSurface,hdr)&&gpu::hasRetainedVideo()&&fbos.size()==1);
 const GLuint fbo=*fbos.begin();
 assert(drawn.size()==2&&drawn[0]==std::pair(-1,fbo)&&drawn[1]==std::pair(2,fbo)&&clearAlpha==192.0f/255.0f);
 auto u=uploads;s=start();
 assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{6,0},{5,0}})&&uploads==u+1&&!gpu::overlayPending());

 gpu::setOverlay(true,false);assert(!gpu::overlayPending());
 gpu::setOverlay(true,true);assert(gpu::overlayPending());
 u=uploads;s=start();
 assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{6,0},{5,0}})&&uploads==u+1&&gpu::hdrScanout);
 u=uploads;s=start();assert(gpu::drawVideo(hdrSurface,hdr)&&gpu::present(ui)&&swaps==s+1&&uploads==u);
 assert(drawn.size()==4&&drawn[1].second==fbo&&drawn[2]==std::pair(6,0U)&&drawn[3]==std::pair(5,0U));

 gpu::setOverlay(false,false);assert(gpu::overlayPending());
 s=start();
 assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{6,0}})&&fbos.empty()&&!gpu::hasRetainedVideo()&&!gpu::overlayPending());
 s=start();assert(gpu::drawVideo(hdrSurface,hdr)&&gpu::present(ui)&&swaps==s+1&&drawn.size()==2&&drawn[1]==std::pair(2,0U)&&fbos.empty());

 gpu::setOverlay(true,true);
 assert(gpu::drawVideo(sdrSurface,sdr)&&gpu::hasRetainedVideo()&&!gpu::hdrScanout);
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{6,0},{4,0}}));
 gpu::setOverlay(false,false);
 s=start();assert(gpu::drawVideo(sdrSurface,sdr)&&gpu::present(ui)&&swaps==s+1&&drawn.size()==2&&drawn[1]==std::pair(1,0U)&&fbos.empty());

 gpu::setOverlay(true,true);
 assert(gpu::drawVideo(hdrSurface,hdr)&&gpu::hasRetainedVideo()&&fbos.size()==1);
 gpu::invalidateVideo();
 assert(fbos.empty()&&!gpu::hasRetainedVideo()&&!gpu::overlayPending());
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{0,0}})&&!gpu::hdrScanout);

 gpu::setOverlay(true,true);
 s=start();assert(gpu::present(ui)&&swaps==s&&drawn.empty());
 now+=99999;assert(gpu::present(ui)&&swaps==s&&drawn.empty());
 now+=1;s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{-1,0},{4,0}})&&clearAlpha==1.0f);
 gpu::setOverlay(false,false);
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{-1,0}}));
 gpu::invalidateVideo();

 fboStatus=GL_FRAMEBUFFER_UNSUPPORTED;
 const auto live=textures.size();
 gpu::setOverlay(true,true);
 s=start();assert(gpu::drawVideo(hdrSurface,hdr)&&!gpu::hasRetainedVideo()&&fbos.empty()&&textures.size()==live);
 assert(drawn.size()==2&&drawn[1]==std::pair(2,0U));
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{5,0}}));
 gpu::setOverlay(true,true);
 s=start();assert(gpu::drawVideo(hdrSurface,hdr)&&fbos.empty()&&gpu::present(ui)&&swaps==s+1);
 gpu::setOverlay(true,true);
 s=start();assert(gpu::present(ui)&&swaps==s&&drawn.empty());
 now+=50000;assert(gpu::drawVideo(hdrSurface,hdr)&&gpu::present(ui)&&swaps==s+1);
 gpu::setOverlay(true,true);
 now+=60000;s=start();assert(gpu::present(ui)&&swaps==s&&drawn.empty());
 now+=100000;s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{-1,0},{5,0}})&&clearAlpha==192.0f/255.0f);
 gpu::setOverlay(false,false);
 s=start();assert(gpu::present(ui)&&swaps==s+1&&(drawn==Draws{{-1,0}})&&fbos.empty());
 gpu::invalidateVideo();fboStatus=GL_FRAMEBUFFER_COMPLETE;
 gpu::setOverlay(true,true);assert(gpu::drawVideo(hdrSurface,hdr)&&fbos.size()==1);
 gpu::setOverlay(false,false);assert(gpu::present(ui)&&fbos.empty());

 assert(textures.size()==liveBefore+1&&textures.count(gpu::overlayTexture));
 gpu::display=reinterpret_cast<EGLDisplay>(1);gpu::context=reinterpret_cast<EGLContext>(1);
 gpu::shutdown();
 assert(!gpu::ready&&!gpu::overlayTexture&&textures.size()==liveBefore);
 gpu::ready=true;
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
 overlayScenarios();
 gpu::ready=false;assert(!gpu::allocationFor(request));
}
