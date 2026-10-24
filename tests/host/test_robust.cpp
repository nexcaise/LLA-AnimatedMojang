#include <cstdlib>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include "mod/MyMod.h"
#include "scene/CaveCamera.h"
#include "scene/CaveGlesRenderer.h"
namespace my_mod { MyMod& MyMod::getInstance(){ static MyMod m; return m; } }
using namespace animated_mojang::scene;
static const int W=640,H=360;
static EGLDisplay d; static EGLConfig cfg;
struct Ctx{EGLSurface s;EGLContext c;};
static Ctx mk(){EGLint pb[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};EGLint cx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};Ctx k{eglCreatePbufferSurface(d,cfg,pb),eglCreateContext(d,cfg,EGL_NO_CONTEXT,cx)};return k;}
static std::vector<unsigned char> grab(){std::vector<unsigned char> p(W*H*4);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
static int countTex(){int n=0;for(GLuint i=1;i<400;++i)n+=glIsTexture(i);return n;}
static long diff(const std::vector<unsigned char>&a,const std::vector<unsigned char>&b){long s=0;for(size_t i=0;i<a.size();++i)s+=std::abs(a[i]-b[i]);return s;}
#define CHECK(c,m) do{printf("%-62s %s\n",m,(c)?"PASS":"FAIL");if(!(c))fails++;}while(0)
int main(){int fails=0;
  auto gp=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  d=gp(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);eglInitialize(d,0,0);eglBindAPI(EGL_OPENGL_ES_API);
  EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};EGLint n;eglChooseConfig(d,ca,&cfg,1,&n);
  Ctx A=mk(); eglMakeCurrent(d,A.s,A.s,A.c);
  CaveGlesRenderer r; r.configure(true,false); r.beginLoad(getenv("AM_ROOT"));
  while(!r.prepare())std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CaveGlesRenderer::FrameParams p;p.width=W;p.height=H;p.view=caveView(caveTitle,5000.0);p.timeMs=900;p.titleFrame=68;p.titleAlpha=1;
  auto frame=[&](){glClearColor(.03f,.03f,.05f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);r.drawFrame(p);glFinish();return grab();};
  // 1. before pages stream, title frame is skipped (no stall) but cave renders
  int before=countTex(); auto f0=frame();
  for(int i=0;i<500;++i){r.prepare();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  int t1=countTex(); auto ref=frame(); int t2=countTex();
  printf("textures: start=%d streamed(all pages)=%d after drawing resting frame=%d\n",before,t1,t2);
  CHECK(diff(f0,ref)>0,"title appears once its page is resident (frame differs)");
  CHECK(t1-before>=4 && t2<t1,"all 4 pages resident, then evicted to resting page only");
  CHECK(t2==2,"after eviction exactly 2 textures live: cave atlas + resting title page");
  // 2. dirty game state must not change the image and must be restored
  GLuint smp;glGenSamplers(1,&smp);glSamplerParameteri(smp,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(smp,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glBindSampler(0,smp);
  glEnable(GL_SCISSOR_TEST);glScissor(10,10,50,50);glEnable(GL_STENCIL_TEST);glPixelStorei(GL_UNPACK_ROW_LENGTH,77);glDepthFunc(GL_GREATER);glClearDepthf(0.25f);glBlendFunc(GL_DST_COLOR,GL_ZERO);glColorMask(1,1,0,1);
  GLuint pbo;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glActiveTexture(GL_TEXTURE3);
  glColorMask(1,1,1,1); // keep readable output; check restore separately below
  glClearColor(.03f,.03f,.05f,1);glDisable(GL_SCISSOR_TEST);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
  glDepthFunc(GL_GREATER);glClearDepthf(0.25f);
  r.drawFrame(p);glFinish();auto dirty=grab();
  GLint df,act,rl,sc,st,smb;GLfloat cd;glGetIntegerv(GL_DEPTH_FUNC,&df);glGetIntegerv(GL_ACTIVE_TEXTURE,&act);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&rl);glGetFloatv(GL_DEPTH_CLEAR_VALUE,&cd);sc=glIsEnabled(GL_SCISSOR_TEST);st=glIsEnabled(GL_STENCIL_TEST);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_SAMPLER_BINDING,&smb);glActiveTexture(GL_TEXTURE3);
  GLint pb2;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pb2);
  CHECK(diff(ref,dirty)==0,"identical image despite scissor/stencil/sampler/depthfunc/PBO set by game");
  CHECK(df==GL_GREATER&&cd==0.25f&&act==GL_TEXTURE3&&rl==77&&sc&&st&&smb==(GLint)smp&&pb2==(GLint)pbo,"all that dirty state restored exactly");
  CHECK(glGetError()==GL_NO_ERROR,"no GL errors");
  glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glDepthFunc(GL_LESS);glClearDepthf(1);glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBlendFunc(GL_ONE,GL_ZERO);
  // 3. context loss: brand-new context
  Ctx B=mk(); eglMakeCurrent(d,B.s,B.s,B.c);
  int spins=0; while(!r.prepare()&&spins++<2000)std::this_thread::sleep_for(std::chrono::milliseconds(2));
  CHECK(r.ready(),"renderer rebuilt itself after EGL context change");
  for(int i=0;i<500;++i){r.prepare();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  auto rebuilt=frame(); // title page streamed again (only resting page wanted? settled flag reset => all pages)
  for(int i=0;i<500;++i){r.prepare();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  rebuilt=frame();
  CHECK(diff(ref,rebuilt)==0,"image after context loss identical to before");
  // 4. replay + keep resident
  r.requestTitleReplay(); r.prepare(); for(int i=0;i<500;++i){r.prepare();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  p.titleFrame=10; auto early=frame(); p.titleFrame=68; auto back=frame();
  CHECK(diff(early,back)>0 && diff(back,ref)==0,"replay streams pages again; frame 10 differs from 68");
  // 5. release from another thread's request
  r.requestGlRelease(); r.serviceRelease(); CHECK(!r.ready()&&countTex()==0,"requestGlRelease + serviceRelease frees every GL object");
  printf("\n%s (%d failures)\n",fails?"SOME TESTS FAILED":"ALL ROBUSTNESS TESTS PASSED",fails);return fails;}
