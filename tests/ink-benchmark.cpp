// Offscreen PTL benchmark. Invoke ONLY through dev/benchmark-ink.py (lifetime flock).
#include "render/Renderer.hpp"
#include "render/PNG.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fstream>
#include <iostream>
#include <thread>
using namespace hyprune;
int main(int argc,char **argv) {
 try {
  if(argc!=3)throw std::runtime_error("usage: ink-benchmark WORLD OUTPUT_DIR");
  auto platform=(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  auto query=(PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
  auto info=(PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
  EGLDeviceEXT devices[16];EGLint n=0;EGLDisplay display=EGL_NO_DISPLAY;
  if(!platform||!query||!info||!query(16,devices,&n))throw std::runtime_error("EGL enumeration unavailable");
  for(int i=0;i<n;++i){
   auto node=info(devices[i],EGL_DRM_RENDER_NODE_FILE_EXT);if(!node)continue;
   std::ifstream f(std::filesystem::path("/sys/class/drm")/std::filesystem::path(node).filename()/"device/vendor");
   std::string vendor;f>>vendor;
   if(vendor=="0x8086"){display=platform(EGL_PLATFORM_DEVICE_EXT,devices[i],nullptr);break;}
  }
  if(display==EGL_NO_DISPLAY||!eglInitialize(display,nullptr,nullptr))throw std::runtime_error("Intel EGL unavailable; no fallback to dGPU");
  eglBindAPI(EGL_OPENGL_ES_API);EGLConfig config;
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};
  eglChooseConfig(display,attrs,&config,1,&n);
  EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},size[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
  auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx),surface=eglCreatePbufferSurface(display,config,size);
  if(!eglMakeCurrent(display,surface,surface,context))throw std::runtime_error("Intel EGL context unavailable");
  std::string device=(const char*)glGetString(GL_RENDERER);
  if(device.find("Intel")==device.npos)throw std::runtime_error("not Intel");
  GLuint tex,fbo;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,3840,2160,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  render::Renderer renderer;auto scene=world::load(argv[1]);renderer.prepare(scene);
  auto graphics=render::graphicsDefaults();graphics.update({{"quality","low"},{"dynamic_resolution",false},{"render_scale",.5}});renderer.applyGraphics(graphics);
  renderer.profile["outline"]={{"enabled",true},{"colour",{.02,.025,.04}},{"widthPx",2},{"depthThreshold",.02},{"normalThreshold",.2}};
  renderer.showInteractionHints=false;renderer.pointerX=renderer.pointerY=-1;
  sim::Camera camera;auto spawn=scene->spawn();camera.position=spawn.position;camera.yaw=spawn.yaw;camera.pitch=spawn.pitch;
  world::Json report={{"renderer",device},{"resolution",{3840,2160}},{"scale",.5},{"pose",{{"position",{camera.position.x,camera.position.y,camera.position.z}},{"yaw",camera.yaw},{"pitch",camera.pitch}}},{"samplesPerCase",120},{"cases",world::Json::array()}};
  auto destination=std::filesystem::path(argv[2]);std::filesystem::create_directories(destination);
  auto median=[](std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];};
  for(int repeat=0;repeat<3;++repeat)for(auto tier:{"off","fast","smooth"}){
   renderer.graphics["outline"]=std::string(tier)!="off";renderer.graphics["outline_quality"]=tier;
   std::vector<double> total,edge,upscale,world;
   for(int frame=0;frame<150;++frame){
    renderer.draw(fbo,3840,2160,camera,{},false);glFinish();
    auto timings=renderer.timings();auto effects=timings["gpuEffectsMs"];
    if(frame>=30 && !effects["outline"].is_null()){
     double sum=0;for(auto &v:timings["gpuPassMs"])sum+=v.get<double>();
     total.push_back(sum);edge.push_back(effects["outline"]);upscale.push_back(effects["upscale_sharpen"]);world.push_back(effects["world_shading_shared"]);
    }
   }
   if(total.empty())throw std::runtime_error("GPU timer unavailable");
   auto item=world::Json{{"tier",tier},{"repeat",repeat},{"totalMs",median(total)},{"edgeMs",median(edge)},{"upscaleMs",median(upscale)},{"worldMs",median(world)}};
   report["cases"].push_back(item);std::cerr<<item.dump()<<'\n';
   if(repeat==0){auto pixels=renderer.snapshot();render::writePNG(destination/(std::string(tier)+".png"),pixels.width,pixels.height,pixels.rgba);}
  }
  // Alternating pairs with an enclosing GPU query avoid summing asynchronous
  // pass medians. GPUTimer detects the outer query and skips nesting.
  using Read64=void(*)(GLuint,GLenum,GLuint64*);
  auto read64=(Read64)eglGetProcAddress("glGetQueryObjectui64vEXT");
  GLuint queryId;glGenQueries(1,&queryId);
  for(auto tier:{"fast","smooth"}) {
   std::vector<double> deltas,offTimes,onTimes;
   for(int pair=0;pair<120;++pair){
    double values[2]{};
    for(int step=0;step<2;++step){
     int enabled=step^(pair%2);renderer.graphics["outline"]=bool(enabled);renderer.graphics["outline_quality"]=tier;
     glFinish();glBeginQuery(0x88BF,queryId);renderer.draw(fbo,3840,2160,camera,{},false);glEndQuery(0x88BF);glFinish();
     GLuint64 ns=0;read64(queryId,GL_QUERY_RESULT,&ns);values[enabled]=ns/1e6;
    }
    if(pair>=20){deltas.push_back(values[1]-values[0]);offTimes.push_back(values[0]);onTimes.push_back(values[1]);}
   }
   std::sort(deltas.begin(),deltas.end());
   report["paired"][tier]={{"offMs",median(offTimes)},{"onMs",median(onTimes)},{"addedMs",median(deltas)},{"addedP95Ms",deltas[deltas.size()*95/100]}};
   std::cerr<<"PAIRED "<<tier<<" "<<report["paired"][tier].dump()<<'\n';
  }
  glDeleteQueries(1,&queryId);
  std::ofstream(destination/"timings.json")<<report.dump(2)<<'\n';
  renderer.destroy();glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
