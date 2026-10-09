#include <cstring>
#include "render/Renderer.hpp"
#include "render/ProgramCache.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <iostream>
#include <cassert>
#include <fstream>
using namespace hyprune;
int main(int argc,char **argv) {
    assert(argc>=2);
    std::filesystem::path temporary;
    if (argc==2) { char path[]="/tmp/hyprune-program-test-XXXXXX"; assert(mkdtemp(path)); temporary=path; setenv("XDG_CACHE_HOME",path,1); }
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = EGL_NO_DISPLAY;
    bool intel = argc>3 && std::string(argv[3])=="intel";
    if (intel) {
        auto query = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
        auto info = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(eglGetProcAddress("eglQueryDeviceStringEXT"));
        EGLDeviceEXT devices[16]; EGLint n=0; assert(query && info && query(16,devices,&n));
        for (int i=0;i<n;++i) {
            auto node=info(devices[i],EGL_DRM_RENDER_NODE_FILE_EXT); if (!node) continue;
            std::ifstream f(std::filesystem::path("/sys/class/drm")/std::filesystem::path(node).filename()/"device/vendor");
            std::string vendor; f>>vendor;
            if (vendor=="0x8086") { display=platform(EGL_PLATFORM_DEVICE_EXT,devices[i],nullptr);break; }
        }
    } else display=platform(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,nullptr);
    assert(display!=EGL_NO_DISPLAY && eglInitialize(display,nullptr,nullptr));
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};
    EGLConfig config; EGLint count; assert(eglChooseConfig(display,attrs,&config,1,&count));
    EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE}, size[]={EGL_WIDTH,320,EGL_HEIGHT,180,EGL_NONE};
    auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx), surface=eglCreatePbufferSurface(display,config,size);
    assert(eglMakeCurrent(display,surface,surface,context));
    if (auto forbid = getenv("HYPRUNE_TEST_FORBID_RENDERER"))
        assert(!std::strstr(reinterpret_cast<const char *>(glGetString(GL_RENDERER)), forbid) &&
               !std::strstr(reinterpret_cast<const char *>(glGetString(GL_VENDOR)), forbid));
    render::Renderer renderer; renderer.warmShaders=argc<3 || std::string(argv[2])!="cold";
    auto w=world::load(argv[1]);
    renderer.prepare(w);
    auto afterPrepare=render::ProgramCache::misses+render::ProgramCache::hits;
    sim::Camera camera; camera.position=w->spawn().position+Vec3{0,1.7f,0};
    world::Json samples=world::Json::array();
    for (int i=0;i<15;++i) {
        auto g=render::graphicsDefaults();g["quality"]=std::array{"low","medium","high","ultra","low"}[i%5];
        g["sky"]=std::array{"off","gradient","environment"}[i%3];
        g["fog"]=std::array{"off","linear","height"}[i%3];g["render_scale"]=1.;g["dynamic_resolution"]=false;
        renderer.applyGraphics(g); renderer.debug=i%2;
        auto start=std::chrono::steady_clock::now();
        renderer.draw(0,320,180,camera,{},true);
        auto submit=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        glFinish();
        auto total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        samples.push_back({{"submitMs",submit},{"completeMs",total}});
        assert(glGetError()==GL_NO_ERROR);
    }
    assert(afterPrepare==render::ProgramCache::misses+render::ProgramCache::hits);
    auto timings=renderer.timings(); renderer.destroy();
    std::cout<<world::Json{{"device",reinterpret_cast<const char*>(glGetString(GL_RENDERER))},{"warm",renderer.warmShaders},{"timings",timings},{"samples",samples}}.dump(2)<<'\n';
    if (!temporary.empty()) {
        GLint formats=0;glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS,&formats);
        if (formats) {
            auto hits=render::ProgramCache::hits;
            render::Renderer again; again.prepare(w); again.destroy(); assert(render::ProgramCache::hits>hits);
            auto file=*std::filesystem::directory_iterator(temporary/"hyprune/programs");
            std::ofstream corrupt(file.path(),std::ios::binary|std::ios::trunc);corrupt<<"bad";corrupt.close();
            auto rejected=render::ProgramCache::rejected;
            render::Renderer fallback;fallback.prepare(w);fallback.destroy();assert(render::ProgramCache::rejected>rejected);
        }
        std::filesystem::remove_all(temporary);
    }
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
