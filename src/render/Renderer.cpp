#include "Renderer.hpp"
#include "GLState.hpp"
#include "PNG.hpp"
#include "world/Environment.hpp"
#include "world/Mounts.hpp"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include <numeric>
#include FT_FREETYPE_H
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace hyprune::render {
namespace {
GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(log);
    }
    return s;
}
GLuint program(const char *vertex, const char *fragment) {
    GLuint v = shader(GL_VERTEX_SHADER, vertex), f = shader(GL_FRAGMENT_SHADER, fragment), p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        glDeleteProgram(p);
        throw std::runtime_error(log);
    }
    return p;
}
const char *vertex = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv0;
layout(location=3) in vec2 uv1;
uniform mat4 vp,model;
out vec3 n;out vec3 worldPosition;out vec2 uv;out vec2 lightUV;
void main(){vec4 pos=model*vec4(position,1);gl_Position=vp*pos;n=mat3(transpose(inverse(model)))*normal;uv=uv0;lightUV=uv1;worldPosition=pos.xyz;}
)";
const char *fragment = R"(#version 300 es
precision highp float;
in vec3 n;in vec3 worldPosition;in vec2 uv;in vec2 lightUV;
uniform sampler2D albedo,lightmap,normalMap,mrMap,aoMap,emissiveMap;
uniform mat3 uvMatrix[5];uniform int uvSet[5];uniform int present[5];
uniform vec3 color,emission,eye;
uniform int hasTexture,baked,lightSrgb,unlit,surface,details;
uniform float intensity,metallic,roughness,normalScale,occlusionStrength;
uniform samplerCube environmentMap;
uniform float cubeLevels,envRotation,envIntensity,alpha,alphaCutoff;
uniform int alphaMode,excludeUnlit,effects;
uniform vec3 fogColor,sunDirection,sunColor;
uniform float fogDensity,heightFalloff,baseHeight,sunIntensity;
out vec4 result;
vec3 decode(vec3 x){return mix(x/12.92,pow((x+.055)/1.055,vec3(2.4)),step(vec3(.04045),x));}
vec2 coords(int i){return (uvMatrix[i]*vec3(uvSet[i]==0?uv:lightUV,1)).xy;}
vec3 mappedNormal(vec3 N,vec2 st){
 // Derivatives use the transformed coordinates: mirrored and rotated islands retain handedness.
 vec3 dx=dFdx(worldPosition),dy=dFdy(worldPosition);vec2 tx=dFdx(st),ty=dFdy(st);
 vec3 a=cross(dy,N),b=cross(N,dx);vec3 T=a*tx.x+b*ty.x,B=a*tx.y+b*ty.y;
 float magnitude=max(dot(T,T),dot(B,B));
 if(magnitude<1e-16)return N;
 vec3 map=texture(normalMap,st).xyz*2.-1.;map.xy*=normalScale;
 return normalize(mat3(T*inversesqrt(magnitude),B*inversesqrt(magnitude),N)*map);
}
void main(){
 if(surface!=0){vec3 texel=hasTexture!=0?decode(texture(albedo,uv).rgb):vec3(1);result=vec4(color*texel,0);return;}
 vec4 baseSample=present[0]!=0?texture(albedo,coords(0)):vec4(1);
 float coverage=alpha*baseSample.a;if(alphaMode==1&&coverage<alphaCutoff)discard;
 vec3 base=color*baseSample.rgb;
 vec3 emissive=emission*(present[4]!=0?texture(emissiveMap,coords(4)).rgb:vec3(1));
 vec3 N=normalize(gl_FrontFacing?n:-n),L=normalize(sunDirection);vec3 geometric=N;
 if(details!=0&&present[1]!=0)N=mappedNormal(N,coords(1));
 vec3 irradiance=vec3(.24+.76*max(dot(N,L),0.));
 if(baked!=0){irradiance=texture(lightmap,lightUV).rgb;if(lightSrgb!=0)irradiance=decode(irradiance);irradiance*=intensity;}
 float ao=present[3]!=0?mix(1.,texture(aoMap,coords(3)).r,occlusionStrength):1.;
 vec3 lit=base*irradiance*ao;
 if(details!=0&&unlit==0){
  vec2 mr=present[2]!=0?texture(mrMap,coords(2)).bg:vec2(1);
  float metal=clamp(metallic*mr.x,0.,1.),rough=clamp(roughness*mr.y,.08,1.);
  vec3 V=normalize(eye-worldPosition),H=normalize(L+V),F0=mix(vec3(.04),base,metal);
  float nv=max(dot(N,V),0.),nh=max(dot(N,H),0.),nl=max(dot(N,L),0.);
  float power=min(2./(rough*rough)-2.,512.);
  vec3 F=F0+(1.-F0)*pow(1.-max(dot(V,H),0.),5.);
  vec3 key=F*((power+2.)/25.132741)*pow(nh,power)*nl;
  vec3 R=reflect(-V,N);vec3 sky=mix(vec3(.055,.07,.085),vec3(.32,.43,.58),clamp(R.y*.5+.5,0.,1.));
  sky=mix(sky,vec3(.16,.21,.28),rough*rough);
  if(effects!=0){float c=cos(envRotation),s=sin(envRotation);R=vec3(c*R.x+s*R.z,R.y,-s*R.x+c*R.z);sky=textureLod(environmentMap,R,rough*(cubeLevels-1.)).rgb*envIntensity;key*=sunColor*sunIntensity;}
  vec3 skyF=F0+(max(vec3(1.-rough),F0)-F0)*pow(1.-nv,5.);
  float visibility=baked!=0?clamp(dot(irradiance,vec3(.2126,.7152,.0722)),0.,1.):1.;
  float detail=baked!=0?clamp(1.+.3*(dot(N,L)-dot(geometric,L)),.7,1.3):1.;
  lit=lit*(1.-metal)*detail+(key+sky*skyF)*visibility*ao;
 }
 if(unlit!=0)lit=base;
 lit+=emissive;
 // Camera-relative integrated height density along the view segment.
 float dy=(worldPosition.y-eye.y)*heightFalloff;
 float integral=abs(dy)<.001?1.:clamp((1.-exp(clamp(-dy,-40.,40.)))/dy,0.,1e6);
 float density=fogDensity*exp(clamp(-(eye.y-baseHeight)*heightFalloff,-40.,40.))*integral;
 float fog=1.-exp(-length(worldPosition-eye)*density);
 if(unlit!=0&&excludeUnlit!=0)fog=0.;lit=mix(lit,fogColor,clamp(fog,0.,1.));
 result=vec4(lit,alphaMode==2?coverage:1.);
}
)";
const char *screenVertex = R"(#version 300 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)";
const char *toneFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D frame,bloomMap;uniform vec2 resolution,pointer;uniform int typing,toneMapping,scope;uniform float exposure,bloomStrength;out vec4 result;
vec3 encode(vec3 x){return mix(12.92*x,1.055*pow(max(x,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),x));}
void main(){vec2 st=vec2(uv.x,1.-uv.y);vec4 sampleColor=texture(frame,st);vec3 x=(sampleColor.rgb+texture(bloomMap,st).rgb*bloomStrength)*exposure;
 vec3 mapped=toneMapping==0?clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.):toneMapping==1?x/(1.+x):clamp(x,0.,1.);
 vec3 c=encode(mix(sampleColor.rgb,mapped,sampleColor.a));
 vec2 p=abs(gl_FragCoord.xy-resolution*pointer);if((p.x<1.&&p.y<7.)||(p.y<1.&&p.x<7.))c=typing!=0?vec3(.5,1.,.65):vec3(.9);
 if(scope!=0){vec2 q=(gl_FragCoord.xy/resolution-.5)*vec2(resolution.x/resolution.y,1.);c*=1.-.86*smoothstep(.24,.48,length(q));float ring=abs(length(q)-.28);if(ring<.001)c=mix(c,vec3(.5,.8,.65),.4);}
 result=vec4(c,1);}
)";
const char *bloomFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D image;uniform vec2 stepSize;uniform float threshold;uniform int extract;out vec4 result;
vec3 tap(vec2 st){vec4 c=texture(image,st);return c.rgb*(extract!=0?c.a:1.);}
void main(){vec3 c;
 if(extract!=0){c=(tap(uv+stepSize*vec2(-.5,-.5))+tap(uv+stepSize*vec2(.5,-.5))+tap(uv+stepSize*vec2(-.5,.5))+tap(uv+stepSize*vec2(.5,.5)))*.25;float l=max(max(c.r,c.g),c.b);c*=max(0.,l-threshold)/max(l,.0001);}
 else c=tap(uv)*.227027027+ (tap(uv+stepSize*1.384615384)+tap(uv-stepSize*1.384615384))*.316216216+(tap(uv+stepSize*3.23076923)+tap(uv-stepSize*3.23076923))*.07027027;
 result=vec4(c,1);}
)";
const char *skyFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform samplerCube environmentMap;uniform vec3 forward,right,up;uniform float tanFov,aspect,rotation;out vec4 result;
void main(){vec3 d=normalize(forward+right*((uv.x*2.-1.)*tanFov*aspect)+up*((uv.y*2.-1.)*tanFov));float c=cos(rotation),s=sin(rotation);d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);result=vec4(textureLod(environmentMap,d,0.).rgb,1);}
)";
void layout(GLuint &vao, GLuint &vbo) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (GLuint i = 0; i < 4; i++)
        glEnableVertexAttribArray(i);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(world::Vertex), (void *)offsetof(world::Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(world::Vertex), (void *)offsetof(world::Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(world::Vertex), (void *)offsetof(world::Vertex, uv));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(world::Vertex), (void *)offsetof(world::Vertex, uv1));
}
void uniform(GLuint p, const char *name, int value) {
    glUniform1i(glGetUniformLocation(p, name), value);
}
void uniform(GLuint p, const char *name, Vec3 value) {
    glUniform3f(glGetUniformLocation(p, name), value.x, value.y, value.z);
}
} // namespace
void Renderer::init() {
    if (scene_)
        return;
    device = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    scene_ = program(vertex, fragment);
    tone_ = program(screenVertex, toneFragment);
    bloom_ = program(screenVertex, bloomFragment);
    sky_ = program(screenVertex, skyFragment);
    auto gpu = std::string(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    intelQuality = intelQuality || gpu.find("Intel") != std::string::npos;
    layout(quadVao_, quadVbo_);
    glGenVertexArrays(1, &screenVao_);
    glGenSamplers(1, &repeat_);
    glGenSamplers(1, &clamp_);
    for (auto s : {repeat_, clamp_}) {
        glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_S, s == repeat_ ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glSamplerParameteri(s, GL_TEXTURE_WRAP_T, s == repeat_ ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    }
}
void Renderer::freeWorld() {
    for (auto b : batches_) {
        glDeleteBuffers(1, &b.vbo);
        glDeleteVertexArrays(1, &b.vao);
    }
    batches_.clear();
    glDeleteTextures(textures_.size(), textures_.data());
    textures_.clear();
    glDeleteTextures(colorTextures_.size(), colorTextures_.data());
    colorTextures_.clear();
    glDeleteSamplers(samplers_.size(), samplers_.data());
    samplers_.clear();
    glDeleteTextures(1, &environment_);
    environment_ = 0;
    world_.reset();
}
void Renderer::prepare(std::shared_ptr<const world::World> world) {
    GLState state;
    for (auto fence : frameFences_)
        glDeleteSync(fence);
    frameFences_.clear();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    init();
    textAtlas();
    size_t gpuBytes = 0;
    for (const auto &mesh : world->meshes)
        gpuBytes += mesh.vertices.size() * sizeof(world::Vertex);
    for (size_t i = 0; i < world->images.size(); ++i) {
        bool color = false;
        for (const auto &m : world->materials)
            color |= m.slots[0].texture == int(i) || m.slots[4].texture == int(i);
        int w = world->images[i].width, h = world->images[i].height;
        do {
            gpuBytes += size_t(w) * h * 4 * (color ? 2 : 1);
            if (w == 1 && h == 1)
                break;
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
        } while (true);
    }
    if (gpuBytes > 512 * 1024 * 1024)
        throw std::runtime_error("world GPU budget exceeded including material mip chains");
    std::vector<Batch> batches;
    std::vector<GLuint> textures, colors, samplers;
    GLuint cube = 0;
    auto newProfile = world->atmosphere.is_object() ? world->atmosphere : world::defaultAtmosphere();
    int resolution = intelQuality ? 64 : 128;
    if (newProfile.contains("environment"))
        resolution = std::min(resolution, newProfile["environment"]["maxResolution"].get<int>());
    const auto &cached = intelQuality ? world->environmentLow : world->environmentHigh;
    auto levels = cached.empty() ? world::prefilterEnvironment(*world, resolution) : cached;
    for (auto &level : levels)
        gpuBytes += size_t(level.size) * level.size * 6 * 8;
    if (gpuBytes > 512 * 1024 * 1024)
        throw std::runtime_error("environment exceeds world GPU budget");
    try {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glActiveTexture(GL_TEXTURE0);
        bool anisotropy = false;
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (int i = 0; i < extensions; ++i) {
            std::string_view e(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)));
            anisotropy |= e == "GL_EXT_texture_filter_anisotropic" || e == "GL_ARB_texture_filter_anisotropic";
        }
        float maximum = 1;
        if (anisotropy)
            glGetFloatv(0x84FF, &maximum);
        for (size_t i = 0; i < world->images.size(); ++i) {
            const auto &image = world->images[i];
            bool color = false;
            for (const auto &m : world->materials)
                color |= m.slots[0].texture == int(i) || m.slots[4].texture == int(i);
            auto upload = [&](GLint format) {
                GLuint tex;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             image.rgba.data());
                glGenerateMipmap(GL_TEXTURE_2D);
                return tex;
            };
            textures.push_back(upload(GL_RGBA8));
            colors.push_back(color ? upload(GL_SRGB8_ALPHA8) : 0);
            GLuint sampler;
            glGenSamplers(1, &sampler);
            samplers.push_back(sampler);
            auto settings = i < world->samplers.size() ? world->samplers[i] : world::Sampler{};
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, settings.minFilter);
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, settings.magFilter);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, settings.wrapS);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, settings.wrapT);
            // Preserve explicit nearest filtering; anisotropy is a runtime quality choice.
            if (anisotropy && settings.magFilter == GL_LINEAR && settings.minFilter == GL_LINEAR_MIPMAP_LINEAR)
                glSamplerParameterf(sampler, 0x84FE, std::min(intelQuality ? 2.f : 8.f, maximum));
        }
        glGenTextures(1, &cube);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
        glBindSampler(6, 0);
        for (size_t mip = 0; mip < levels.size(); mip++)
            for (int face = 0; face < 6; face++)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGBA16F, levels[mip].size, levels[mip].size,
                             0, GL_RGBA, GL_FLOAT, levels[mip].faces[face].data());
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        for (auto &mesh : world->meshes) {
            Batch b;
            layout(b.vao, b.vbo);
            b.count = mesh.vertices.size();
            b.material = mesh.material;
            b.node = mesh.node;
            for (auto &v : mesh.vertices)
                b.center += v.position;
            if (!mesh.vertices.empty())
                b.center = b.center / float(mesh.vertices.size());
            batches.push_back(b);
            glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(world::Vertex), mesh.vertices.data(),
                         GL_STATIC_DRAW);
        }
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("world GPU upload failed");
    } catch (...) {
        for (auto b : batches) {
            glDeleteBuffers(1, &b.vbo);
            glDeleteVertexArrays(1, &b.vao);
        }
        glDeleteTextures(1, &cube);
        glDeleteTextures(textures.size(), textures.data());
        glDeleteTextures(colors.size(), colors.data());
        glDeleteSamplers(samplers.size(), samplers.data());
        throw;
    }
    freeWorld();
    world_ = std::move(world);
    batches_ = std::move(batches);
    textures_ = std::move(textures);
    colorTextures_ = std::move(colors);
    samplers_ = std::move(samplers);
    environment_ = cube;
    cubeLevels_ = levels.size();
    profile = newProfile;
    collisionChanged();
}
void Renderer::collisionChanged() {
    GLState state;
    std::vector<world::Vertex> wire;
    for (auto &triangle : world_->collision)
        for (auto edge :
             {std::pair{triangle.a, triangle.b}, std::pair{triangle.b, triangle.c}, std::pair{triangle.c, triangle.a}})
            for (auto point : {edge.first, edge.second}) {
                world::Vertex vertex{};
                vertex.position = point;
                vertex.normal = {0, 1, 0};
                wire.push_back(vertex);
            }
    if (!collisionVao_)
        layout(collisionVao_, collisionVbo_);
    glBindVertexArray(collisionVao_);
    glBindBuffer(GL_ARRAY_BUFFER, collisionVbo_);
    glBufferData(GL_ARRAY_BUFFER, wire.size() * sizeof(world::Vertex), wire.data(), GL_STATIC_DRAW);
    collisionCount_ = wire.size();
}
void Renderer::freeTargets() {
    glDeleteFramebuffers(1, &msaa_);
    glDeleteFramebuffers(1, &resolve_);
    glDeleteRenderbuffers(1, &color_);
    glDeleteRenderbuffers(1, &depth_);
    glDeleteTextures(1, &resolveTexture_);
    msaa_ = resolve_ = color_ = depth_ = resolveTexture_ = 0;
    glDeleteFramebuffers(2, bloomFbo_);
    glDeleteTextures(2, bloomTexture_);
    bloomFbo_[0] = bloomFbo_[1] = bloomTexture_[0] = bloomTexture_[1] = 0;
    width_ = height_ = 0;
}
void Renderer::targets(int w, int h) {
    if (w == width_ && h == height_ && allocatedIntel_ == intelQuality)
        return;
    freeTargets();
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(intelQuality ? 2 : 4, maxSamples);
    allocatedIntel_ = intelQuality;
    glGenFramebuffers(1, &msaa_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_);
    glGenRenderbuffers(1, &color_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR MSAA framebuffer unavailable");
    glGenFramebuffers(1, &resolve_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_);
    glGenTextures(1, &resolveTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR resolve framebuffer unavailable");
    glGenFramebuffers(2, bloomFbo_);
    glGenTextures(2, bloomTexture_);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glBindTexture(GL_TEXTURE_2D, bloomTexture_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, std::max(1, w / 2), std::max(1, h / 2), 0, GL_RGBA, GL_HALF_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTexture_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("bloom target unavailable");
    }
    width_ = w;
    height_ = h;
}
void Renderer::draw(GLuint target, int w, int h, const sim::Camera &camera, std::span<const surfaces::Surface> surfaces,
                    bool typing) {
    if (!world_)
        return;
    while (!frameFences_.empty()) {
        auto ready = glClientWaitSync(frameFences_.front(), 0, 0);
        if (ready != GL_ALREADY_SIGNALED && ready != GL_CONDITION_SATISFIED)
            break;
        glDeleteSync(frameFences_.front());
        frameFences_.pop_front();
    }
    if (frames && (captureRequested || frameFences_.size() >= 2)) {
        GLState guard;
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_RASTERIZER_DISCARD);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        present(target, w, h, frameTyping_);
        ++skipped_;
        return;
    }
    auto started = std::chrono::steady_clock::now();
    if (lastFrame_.time_since_epoch().count())
        frameMs_ = std::chrono::duration<double, std::milli>(started - lastFrame_).count();
    lastFrame_ = started;
    frameCamera_ = camera;
    frameWorld_ = world_->id;
    frameTyping_ = typing;
    GLState state;
    init();
    targets(w, h);
    glLineWidth(1);
    gpu_[0].begin();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDepthRangef(0, 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_);
    glViewport(0, 0, w, h);
    glClearColor(.028f, .045f, .065f, 1);
    glClearDepthf(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auto fog = profile.at("fog"), sky = profile.at("sky"), sun = sky.at("sun"), bloom = profile.at("bloom");
    auto vec = [](const world::Json &j) { return Vec3{j[0], j[1], j[2]}; };
    float rotation = profile.contains("environment") ? profile["environment"]["rotation"].get<float>() : 0;
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_CUBE_MAP, environment_);
    glBindSampler(6, 0);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(sky_);
    uniform(sky_, "environmentMap", 6);
    auto forward = camera.forward(), right = normalized(cross(forward, {0, 1, 0})), up = cross(right, forward);
    uniform(sky_, "forward", forward);
    uniform(sky_, "right", right);
    uniform(sky_, "up", up);
    glUniform1f(glGetUniformLocation(sky_, "tanFov"), std::tan(camera.fov * .01745329252f / 2));
    glUniform1f(glGetUniformLocation(sky_, "aspect"), float(w) / h);
    glUniform1f(glGetUniformLocation(sky_, "rotation"), rotation);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);
    glUseProgram(scene_);
    auto vp = perspective(float(w) / h, camera.fov * .01745329252f) * camera.view();
    glUniformMatrix4fv(glGetUniformLocation(scene_, "vp"), 1, GL_FALSE, vp.v.data());
    uniform(scene_, "albedo", 0);
    uniform(scene_, "lightmap", 1);
    uniform(scene_, "surface", 0);
    uniform(scene_, "normalMap", 2);
    uniform(scene_, "mrMap", 3);
    uniform(scene_, "aoMap", 4);
    uniform(scene_, "emissiveMap", 5);
    uniform(scene_, "eye", camera.position);
    uniform(scene_, "details", materialDetails);
    uniform(scene_, "effects", effects);
    uniform(scene_, "environmentMap", 6);
    uniform(scene_, "excludeUnlit", fog["excludeUnlit"].get<bool>());
    uniform(scene_, "fogColor", vec(fog["color"]));
    uniform(scene_, "sunDirection", vec(sun["direction"]));
    uniform(scene_, "sunColor", vec(sun["color"]));
    glUniform1f(glGetUniformLocation(scene_, "fogDensity"), fog["density"].get<float>());
    glUniform1f(glGetUniformLocation(scene_, "heightFalloff"), fog["heightFalloff"].get<float>());
    glUniform1f(glGetUniformLocation(scene_, "baseHeight"), fog["baseHeight"].get<float>());
    glUniform1f(glGetUniformLocation(scene_, "sunIntensity"), sun["intensity"].get<float>());
    glUniform1f(glGetUniformLocation(scene_, "cubeLevels"), cubeLevels_);
    glUniform1f(glGetUniformLocation(scene_, "envRotation"), rotation);
    glUniform1f(glGetUniformLocation(scene_, "envIntensity"), 1);
    glBindSampler(0, repeat_);
    glBindSampler(1, clamp_);
    std::vector<size_t> order(batches_.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        bool ta = world_->materials[batches_[a].material].alphaMode == 2,
             tb = world_->materials[batches_[b].material].alphaMode == 2;
        if (ta != tb)
            return !ta;
        return ta && length(batches_[a].center - camera.position) > length(batches_[b].center - camera.position);
    });
    for (auto index : order) {
        auto b = batches_[index];
        const auto &m = world_->materials[b.material];
        if (m.alphaMode == 2) {
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        // Material doubleSided controls culling, with back-face normal reversal in shader.
        if (m.doubleSided)
            glDisable(GL_CULL_FACE);
        else
            glEnable(GL_CULL_FACE);
        glUniform1f(glGetUniformLocation(scene_, "alpha"), m.alpha);
        glUniform1f(glGetUniformLocation(scene_, "alphaCutoff"), m.alphaCutoff);
        uniform(scene_, "alphaMode", m.alphaMode);
        uniform(scene_, "color", m.color);
        uniform(scene_, "emission", m.emissive);
        uniform(scene_, "hasTexture", m.baseTexture >= 0);
        uniform(scene_, "baked", m.lightmap >= 0);
        uniform(scene_, "lightSrgb", m.lightmapSrgb);
        uniform(scene_, "unlit", m.unlit);
        glUniform1f(glGetUniformLocation(scene_, "intensity"), m.intensity);
        glUniform1f(glGetUniformLocation(scene_, "metallic"), m.metallic);
        glUniform1f(glGetUniformLocation(scene_, "roughness"), m.roughness);
        glUniform1f(glGetUniformLocation(scene_, "normalScale"), m.normalScale);
        glUniform1f(glGetUniformLocation(scene_, "occlusionStrength"), m.occlusionStrength);
        float matrices[45];
        GLint sets[5], present[5];
        for (int i = 0; i < 5; ++i) {
            const auto &slot = m.slots[i];
            float c = std::cos(slot.rotation), s = std::sin(slot.rotation);
            float matrix[9]{c * slot.scale[0], s * slot.scale[0], 0, -s * slot.scale[1], c * slot.scale[1], 0,
                            slot.offset[0],    slot.offset[1],    1};
            std::copy_n(matrix, 9, matrices + i * 9);
            sets[i] = slot.texCoord;
            present[i] = slot.texture >= 0;
            int unit = i == 0 ? 0 : i + 1;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D,
                          present[i] ? (i == 0 || i == 4 ? colorTextures_ : textures_)[slot.texture] : 0);
            glBindSampler(unit, present[i] ? samplers_[slot.texture] : repeat_);
        }
        glUniformMatrix3fv(glGetUniformLocation(scene_, "uvMatrix"), 5, GL_FALSE, matrices);
        glUniform1iv(glGetUniformLocation(scene_, "uvSet"), 5, sets);
        glUniform1iv(glGetUniformLocation(scene_, "present"), 5, present);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m.lightmap >= 0 ? textures_[m.lightmap] : 0);
        auto model = transforms.contains(b.node) ? transforms.at(b.node) : Mat4::identity();
        glUniformMatrix4fv(glGetUniformLocation(scene_, "model"), 1, GL_FALSE, model.v.data());
        glBindVertexArray(b.vao);
        glDrawArrays(GL_TRIANGLES, 0, b.count);
    }
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindSampler(0, clamp_);
    uniform(scene_, "baked", 0);
    uniform(scene_, "unlit", 1);
    uniform(scene_, "surface", 1);
    uniform(scene_, "emission", Vec3{});
    auto identity = Mat4::identity();
    glUniformMatrix4fv(glGetUniformLocation(scene_, "model"), 1, GL_FALSE, identity.v.data());
    cpu_[0] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    gpu_[0].end();
    gpu_[1].begin();
    auto surfacesStarted = std::chrono::steady_clock::now();
    for (const auto &s : surfaces) {
        Vec3 right = s.right(), up = s.up(), normal = s.normal();
        auto quad = [&](float width, float height, Vec3 center, bool border) {
            world::Vertex vertices[6];
            const float xy[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
            for (int i = 0; i < 6; i++) {
                vertices[i].position = center + right * ((xy[i][0] - .5f) * width) + up * ((.5f - xy[i][1]) * height);
                vertices[i].normal = normal;
                vertices[i].uv[0] = xy[i][0];
                vertices[i].uv[1] = xy[i][1];
            }
            uniform(scene_, "hasTexture", !border && s.texture != 0);
            uniform(scene_, "color",
                    border ? (s.focused ? Vec3{.08f, .9f, .45f} : Vec3{.1f, .4f, .55f}) : Vec3{1, 1, 1});
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, border ? 0 : s.texture);
            glBindVertexArray(quadVao_);
            glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        };
        quad(s.width + .07f, s.height + .07f, s.center - normal * .015f, true);
        quad(s.width, s.height, s.center, false);
    }
    worldOverlays(camera, surfaces, w, h);
    // Editor primitives are plain vertices under the same GL guard. No seat or
    // compositor callbacks occur while the render pass draws handles/path strokes.
    glDisable(GL_DEPTH_TEST);
    uniform(scene_, "hasTexture", 0);
    std::map<std::array<float, 3>, std::vector<world::Vertex>> strokes;
    if (debug) {
        uniform(scene_, "color", Vec3{.2f, 1, .3f});
        glBindVertexArray(collisionVao_);
        glDrawArrays(GL_LINES, 0, collisionCount_);
    }
    auto frameLines = lines;
    if (debug) {
        auto saved = std::move(lines);
        lines.clear();
        debugGeometry(camera, surfaces);
        std::map<std::array<float, 3>, std::vector<world::Vertex>> wires;
        for (auto &line : lines)
            for (auto point : {line.a, line.b}) {
                world::Vertex vertex{};
                vertex.position = point;
                vertex.normal = {0, 1, 0};
                wires[{line.color.x, line.color.y, line.color.z}].push_back(vertex);
            }
        for (auto &[color, vertices] : wires) {
            uniform(scene_, "color", Vec3{color[0], color[1], color[2]});
            glBindVertexArray(quadVao_);
            glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(world::Vertex), vertices.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_LINES, 0, vertices.size());
        }
        lines = std::move(saved);
    }
    for (auto &l : frameLines) {
        auto delta = l.b - l.a;
        auto side = normalized(cross(delta, camera.position - (l.a + l.b) * .5f)) * l.width;
        if (length(side) < .00001f)
            continue;
        std::array<Vec3, 6> p = {l.a - side, l.b - side, l.b + side, l.a - side, l.b + side, l.a + side};
        world::Vertex v[6];
        for (int i = 0; i < 6; i++) {
            v[i].position = p[i];
            v[i].normal = {0, 0, 1};
        }
        auto &group = strokes[{l.color.x, l.color.y, l.color.z}];
        group.insert(group.end(), std::begin(v), std::end(v));
    }
    for (auto &[color, vertices] : strokes) {
        uniform(scene_, "color", Vec3{color[0], color[1], color[2]});
        glBindVertexArray(quadVao_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(world::Vertex), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    }
    cpu_[1] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - surfacesStarted).count();
    gpu_[1].end();
    gpu_[2].begin();
    auto bloomStarted = std::chrono::steady_clock::now();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    // Application pixels carry alpha zero and never contribute to world bloom.
    glDisable(GL_DEPTH_TEST);
    glUseProgram(bloom_);
    uniform(bloom_, "image", 0);
    glBindVertexArray(screenVao_);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, clamp_);
    glViewport(0, 0, std::max(1, w / 2), std::max(1, h / 2));
    glUniform1f(glGetUniformLocation(bloom_, "threshold"), effects ? bloom["threshold"].get<float>() : 1e20f);
    float radius = bloom["radius"];
    if (!effects || bloom["strength"].get<float>() <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    } else
        for (int pass = 0; pass < 3; pass++) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[pass == 1 ? 1 : 0]);
            glBindTexture(GL_TEXTURE_2D, pass == 0 ? resolveTexture_ : bloomTexture_[pass == 1 ? 0 : 1]);
            uniform(bloom_, "extract", pass == 0);
            glUniform2f(glGetUniformLocation(bloom_, "stepSize"),
                        pass == 2   ? 0
                        : pass == 0 ? 1.f / w
                                    : radius / w,
                        pass == 1   ? 0
                        : pass == 0 ? 1.f / h
                                    : radius / h);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    cpu_[2] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bloomStarted).count();
    gpu_[2].end();
    gpu_[3].begin();
    auto outputStarted = std::chrono::steady_clock::now();
    present(target, w, h, typing);
    gpu_[3].end();
    cpu_[3] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - outputStarted).count();
    renderMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    ++frames;
    frameFences_.push_back(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    glFlush();
}
void Renderer::present(GLuint target, int w, int h, bool typing) {
    glViewport(0, 0, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(tone_);
    uniform(tone_, "frame", 0);
    uniform(tone_, "typing", typing);
    uniform(tone_, "bloomMap", 1);
    auto curve = profile["toneMapping"].get<std::string>();
    uniform(tone_, "toneMapping", curve == "aces" ? 0 : curve == "reinhard" ? 1 : 2);
    glUniform1f(glGetUniformLocation(tone_, "exposure"), profile["exposure"].get<float>());
    glUniform1f(glGetUniformLocation(tone_, "bloomStrength"), effects ? profile["bloom"]["strength"].get<float>() : 0);
    glUniform2f(glGetUniformLocation(tone_, "pointer"), pointerX, pointerY);
    uniform(tone_, "scope", scopeVignette ? 1 : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTexture_[0]);
    glBindSampler(1, clamp_);
    glUniform2f(glGetUniformLocation(tone_, "resolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glBindSampler(0, clamp_);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for (auto &label : labels)
        text(label.x * w, label.y * h, label.text, label.color, w, h);
    for (auto &sprite : icons_)
        icon(sprite, w, h);
    debugHUD(w, h);
}
void Renderer::debugHUD(int w, int h) {
    if (debug) {
        std::ostringstream message;
        message << std::fixed << std::setprecision(2);
        message << "FPS " << (frameMs_ > 0 ? 1000 / frameMs_ : 0) << "  FRAME " << frameMs_ << " ms\nCPU render "
                << renderMs_ << "  update " << updateMs << "  capture " << captureMs << "  IPC " << ipcMs
                << " ms\nGPU ";
        bool supported = std::all_of(gpu_.begin(), gpu_.end(), [](auto &t) { return t.available; });
        if (supported) {
            double total = 0;
            for (auto &t : gpu_)
                total += t.milliseconds;
            message << total << " ms";
        } else
            message << "N/A (timer unavailable/disjoint/enclosing query)";
        message << "\nScene/windows/bloom/output CPU ";
        for (auto &v : cpu_)
            message << v << " ";
        text(22, 24, message.str(), {.4f, 1, .6f}, w, h);
    }
}

void Renderer::drawHUD(GLuint target, int w, int h) {
    GLState state;
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    debugHUD(w, h);
}

void Renderer::textAtlas() {
    if (loadedFont_ == font && textTexture_)
        return;
    auto pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(font.c_str()));
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    auto match = FcFontMatch(nullptr, pattern, &result);
    FcPatternDestroy(pattern);
    FcChar8 *path = nullptr;
    if (!match || FcPatternGetString(match, FC_FILE, 0, &path) != FcResultMatch) {
        if (match)
            FcPatternDestroy(match);
        throw std::runtime_error("overlay font unavailable");
    }
    FT_Library library;
    if (FT_Init_FreeType(&library)) {
        FcPatternDestroy(match);
        throw std::runtime_error("font initialization failed");
    }
    FT_Face face;
    auto status = FT_New_Face(library, reinterpret_cast<const char *>(path), 0, &face);
    FcPatternDestroy(match);
    if (status) {
        FT_Done_FreeType(library);
        throw std::runtime_error("overlay font failed");
    }
    FT_Set_Pixel_Sizes(face, 0, 16);
    constexpr int width = 512, height = 256, cell = 32;
    std::vector<uint8_t> atlas(width * height);
    for (int i = 32; i < 127; ++i) {
        if (FT_Load_Char(face, i, FT_LOAD_RENDER))
            continue;
        auto &g = face->glyph;
        int x = (i % 16) * cell, y = (i / 16) * cell;
        int gw = std::min(30u, g->bitmap.width), gh = std::min(30u, g->bitmap.rows);
        for (int row = 0; row < gh; ++row)
            for (int column = 0; column < gw; ++column)
                atlas[(y + row) * width + x + column] = g->bitmap.buffer[row * g->bitmap.pitch + column];
        glyphs_[i] = {
            float(gw),        float(gh),        float(g->bitmap_left), float(g->bitmap_top), float(g->advance.x) / 64,
            float(x) / width, float(y) / height};
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    if (!textProgram_)
        textProgram_ = program(R"(#version 300 es
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;out vec2 uv;void main(){gl_Position=vec4(position,0,1);uv=texcoord;})",
                               R"(#version 300 es
precision highp float;in vec2 uv;uniform sampler2D atlas;uniform int iconMode;uniform vec3 color;out vec4 result;void main(){float a=(iconMode!=0?texture(atlas,uv).a:texture(atlas,uv).r);result=vec4(color,a);})");
    if (!textTexture_)
        glGenTextures(1, &textTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textTexture_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!textVao_) {
        glGenVertexArrays(1, &textVao_);
        glGenBuffers(1, &textBuffer_);
    }
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
    loadedFont_ = font;
}
void Renderer::text(float x, float y, const std::string &value, Vec3 color, int w, int h) {
    if (!textTexture_)
        return;
    float left = x;
    std::vector<float> vertices;
    vertices.reserve(value.size() * 24);
    for (auto character : value) {
        auto c = static_cast<unsigned char>(character);
        if (c == '\n') {
            x = left;
            y += 21;
            continue;
        }
        if (c < 32 || c >= 127)
            c = '?';
        auto g = glyphs_[c];
        float x0 = x + g.left, y0 = y + 16 - g.top, x1 = x0 + g.width, y1 = y0 + g.height;
        std::array<std::array<float, 4>, 6> points = {{{x0, y0, g.u, g.v},
                                                       {x1, y0, g.u + g.width / 512, g.v},
                                                       {x1, y1, g.u + g.width / 512, g.v + g.height / 256},
                                                       {x0, y0, g.u, g.v},
                                                       {x1, y1, g.u + g.width / 512, g.v + g.height / 256},
                                                       {x0, y1, g.u, g.v + g.height / 256}}};
        for (auto &p : points) {
            vertices.push_back(p[0] * 2 / w - 1);
            vertices.push_back(p[1] * 2 / h - 1);
            vertices.push_back(p[2]);
            vertices.push_back(p[3]);
        }
        x += g.advance;
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(textProgram_);
    uniform(textProgram_, "iconMode", 0);
    uniform(textProgram_, "atlas", 0);
    uniform(textProgram_, "color", color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textTexture_);
    glBindSampler(0, clamp_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 4);
    glDisable(GL_BLEND);
}
void Renderer::applyOverlayStyle(const world::Json &patch) {
    auto next = overlayStylePatch(overlayStyle, patch);
    if (next == overlayStyle)
        return;
    bool atlasChanged = next["iconAtlas"] != overlayStyle["iconAtlas"];
    PNG atlas;
    if (atlasChanged && !next["iconAtlas"].is_null()) {
        auto descriptor = next["iconAtlas"];
        atlas = readAtlasPNG(descriptor["path"]);
        int cell = descriptor["cellSize"], count = (atlas.width / cell) * (atlas.height / cell);
        if (atlas.width % cell || atlas.height % cell || descriptor["waypointIndex"] >= count ||
            descriptor["promptIndex"] >= count)
            throw std::runtime_error("atlas cells out of image bounds");
    }
    GLState guard;
    auto previousFont = font;
    font = next["fontFamily"];
    try {
        textAtlas();
    } catch (...) {
        font = previousFont;
        throw;
    }
    GLuint texture = 0;
    if (!atlas.rgba.empty()) {
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.width, atlas.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     atlas.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    if (atlasChanged) {
        glDeleteTextures(1, &iconTexture_);
        iconTexture_ = texture;
        iconWidth_ = atlas.width;
        iconHeight_ = atlas.height;
    }
    overlayStyle = next;
}
void Renderer::icon(const Icon &sprite, int w, int h) {
    if (!iconTexture_)
        return;
    int cell = overlayStyle["iconAtlas"]["cellSize"], cols = iconWidth_ / cell;
    float u = float(sprite.index % cols * cell) / iconWidth_, v = float(sprite.index / cols * cell) / iconHeight_;
    float x = sprite.x * w, y = sprite.y * h;
    float vertices[24];
    int i = 0;
    for (auto xy : {std::pair{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}}) {
        vertices[i++] = (x + (xy.first - .5f) * 24) * 2 / w - 1;
        vertices[i++] = (y + (xy.second - .5f) * 24) * 2 / h - 1;
        vertices[i++] = u + xy.first * cell / iconWidth_;
        vertices[i++] = v + xy.second * cell / iconHeight_;
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(textProgram_);
    uniform(textProgram_, "iconMode", 1);
    uniform(textProgram_, "atlas", 0);
    uniform(textProgram_, "color", sprite.color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, iconTexture_);
    glBindSampler(0, clamp_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
}
void Renderer::worldOverlays(const sim::Camera &camera, std::span<const surfaces::Surface> surfaces, int w, int h) {
    labels.clear();
    icons_.clear();
    float aspect = float(w) / h;
    auto color = [&](const char *key) {
        auto c = overlayStyle[key];
        return Vec3{c[0], c[1], c[2]};
    };
    auto position = [](const world::Json &p) { return Vec3{p[0], p[1], p[2]}; };
    auto marker = [&](Vec3 point, const std::string &label, bool waypointMarker, bool prompt) {
        auto projected = camera.project(point, aspect);
        if (!projected.visible)
            return;
        auto labelColor = color(prompt ? "promptColor" : "labelColor");
        labels.push_back({projected.x + 14.f / w, projected.y, label.substr(0, 128), labelColor});
        if (iconTexture_ && (waypointMarker || prompt))
            icons_.push_back({projected.x, projected.y,
                              overlayStyle["iconAtlas"][prompt ? "promptIndex" : "waypointIndex"],
                              color(prompt ? "promptColor" : "waypointColor")});
        else if (waypointMarker)
            labels.push_back({projected.x - 5.f / w, projected.y, "+", color("waypointColor")});
    };
    std::vector<world::Vertex> routeVertices;
    if (!waypoint.is_null() && !route.empty()) {
        Vec3 previous = camera.position + Vec3{0, -camera.eyeHeight + .025f, 0};
        for (auto &node : route) {
            auto next = position(node["position"]);
            next.y += -1.7f + .025f;
            for (auto p : {previous, next}) {
                world::Vertex v{};
                v.position = p;
                v.normal = {0, 1, 0};
                routeVertices.push_back(v);
            }
            previous = next;
        }
        uniform(scene_, "hasTexture", 0);
        auto linear = color("routeColor");
        auto decode = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
        linear = {decode(linear.x), decode(linear.y), decode(linear.z)};
        uniform(scene_, "color", linear);
        glBindVertexArray(quadVao_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, routeVertices.size() * sizeof(world::Vertex), routeVertices.data(),
                     GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, routeVertices.size());
    }
    if (!waypoint.is_null())
        marker(position(waypoint["position"]), waypoint["label"], true, false);
    if (showAreaMarkers)
        for (auto &place : world_->places)
            marker(position(place["position"]), place["name"], false, false);
    const surfaces::Surface *selected = nullptr;
    std::optional<surfaces::Hit> closest;
    for (auto &surface : surfaces)
        if (auto hit = surfaces::intersect(surface, camera.position, camera.forward());
            hit && hit->distance <= useDistance && (!closest || hit->distance < closest->distance)) {
            selected = &surface;
            closest = hit;
        }
    if (selected) {
        auto obstruction = interaction::raycast(camera.position, camera.forward(), world_->collision);
        if (!obstruction || obstruction->distance >= closest->distance - .08f)
            marker(selected->center + selected->up() * (selected->height / 2 + .12f), "E USE / F FOCUS", false, true);
    }
    if (!toolPrompt.is_null())
        marker(position(toolPrompt["position"]), toolPrompt["text"], false, true);
    world::Json projected = world::Json::array();
    for (auto &label : labels)
        projected.push_back({{"x", label.x}, {"y", label.y}, {"text", label.text}});
    overlayDiagnostics = {{"frame", frames + 1},   {"heading", camera.yaw},
                          {"pitch", camera.pitch}, {"areaMarkers", showAreaMarkers},
                          {"labels", projected},   {"routeSegments", routeVertices.size() / 2}};
}
void Renderer::debugGeometry(const sim::Camera &camera, std::span<const surfaces::Surface> surfaces) {
    auto line = [&](Vec3 a, Vec3 b, Vec3 color) { lines.push_back({a, b, color, .012f}); };
    auto box = [&](const std::array<Vec3, 8> &v, Vec3 color) {
        for (int i = 0; i < 8; ++i)
            for (int bit : {1, 2, 4})
                if (!(i & bit))
                    line(v[i], v[i | bit], color);
    };
    for (auto &surface : surfaces) {
        std::array<Vec3, 8> corners;
        for (int i = 0; i < 8; ++i)
            corners[i] = surface.center + surface.right() * ((i & 1 ? 1 : -1) * surface.width * .5f) +
                         surface.up() * ((i & 2 ? 1 : -1) * surface.height * .5f) +
                         surface.normal() * ((i & 4 ? 1 : -1) * .04f);
        box(corners, {1, .55f, .1f});
    }
    for (auto &zone : world_->placementZones) {
        auto lo = zone["bounds"]["min"], hi = zone["bounds"]["max"];
        std::array<Vec3, 8> corners;
        for (int i = 0; i < 8; ++i)
            corners[i] = {i & 1 ? hi[0].get<float>() : lo[0].get<float>(),
                          i & 2 ? hi[1].get<float>() : lo[1].get<float>(),
                          i & 4 ? hi[2].get<float>() : lo[2].get<float>()};
        box(corners, {.4f, .7f, 1});
    }
    // Exact player capsule; thin native lines also remain readable from inside it.
    Vec3 low = camera.position + Vec3{0, -camera.eyeHeight + .3f, 0}, high = camera.position + Vec3{0, -.2f, 0};
    for (int j = 0; j < 24; ++j) {
        float a = j * 6.2831853f / 24, b = (j + 1) * 6.2831853f / 24;
        Vec3 p{.3f * std::cos(a), 0, .3f * std::sin(a)}, q{.3f * std::cos(b), 0, .3f * std::sin(b)};
        line(low + p, low + q, {1, .25f, .6f});
        line(high + p, high + q, {1, .25f, .6f});
        if (j % 6 == 0)
            line(low + p, high + p, {1, .25f, .6f});
        for (auto center : {low, high})
            for (int plane = 0; plane < 2; ++plane) {
                Vec3 u = plane ? Vec3{0, .3f * std::sin(a), .3f * std::cos(a)}
                               : Vec3{.3f * std::cos(a), .3f * std::sin(a), 0};
                Vec3 v = plane ? Vec3{0, .3f * std::sin(b), .3f * std::cos(b)}
                               : Vec3{.3f * std::cos(b), .3f * std::sin(b), 0};
                line(center + u, center + v, {1, .25f, .6f});
            }
    }
    for (auto &edge : world_->walkingEdges) {
        const world::Json *a = nullptr, *b = nullptr;
        for (auto &node : world_->walkingNodes) {
            if (node["id"] == edge["from"])
                a = &node;
            if (node["id"] == edge["to"])
                b = &node;
        }
        if (a && b) {
            auto p = a->at("position"), q = b->at("position");
            line({p[0], p[1], p[2]}, {q[0], q[1], q[2]}, {.1f, 1, .65f});
        }
    }
}
world::Json Renderer::timings() const {
    world::Json cpu = world::Json::array(), gpu = world::Json::array();
    for (auto &v : cpu_)
        cpu.push_back(v);
    for (auto &timer : gpu_)
        gpu.push_back(timer.available ? world::Json(timer.milliseconds) : world::Json(nullptr));
    return {{"frameMs", frameMs_},
            {"cpuRenderMs", renderMs_},
            {"cpuUpdateMs", updateMs},
            {"captureMs", captureMs},
            {"gpuCaptureMs", gpuCaptureMs >= 0 ? world::Json(gpuCaptureMs) : world::Json(nullptr)},
            {"ipcMs", ipcMs},
            {"cpuPassMs", cpu},
            {"gpuPassMs", gpu},
            {"debug", debug},
            {"reusedFrames", skipped_},
            {"inFlight", frameFences_.size()},
            {"renderer", device},
            {"msaaSamples", samples_},
            {"quality", intelQuality ? "intel" : "high"},
            {"collisionTriangles", world_ ? world_->collision.size() : 0}};
}
bool Renderer::completed() {
    if (frameFences_.empty())
        return true;
    auto result = glClientWaitSync(frameFences_.back(), 0, 0);
    return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}
Renderer::Pixels Renderer::snapshot() {
    if (!completed())
        throw std::runtime_error("world frame still on GPU");
    if (!frames || !resolve_)
        throw std::runtime_error("no completed world frame to capture");
    auto started = std::chrono::steady_clock::now();
    GLState state;
    GLuint fbo = 0, texture = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
        throw std::runtime_error("capture framebuffer unavailable");
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_SAMPLE_COVERAGE);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    present(fbo, width_, height_, frameTyping_);
    Pixels pixels{width_, height_, frameCamera_, std::vector<uint8_t>(size_t(width_) * height_ * 4), 0};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    // Hyprland target convention: row zero is logical top. No second row flip.
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &texture);
    pixels.worldId = frameWorld_;
    pixels.ageMs = std::chrono::duration<double, std::milli>(started - lastFrame_).count();
    pixels.readbackMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return pixels;
}
void Renderer::destroy() {
    GLState state;
    for (auto fence : frameFences_)
        glDeleteSync(fence);
    frameFences_.clear();
    freeWorld();
    freeTargets();
    for (auto &timer : gpu_)
        timer.destroy();
    glDeleteTextures(1, &iconTexture_);
    iconTexture_ = 0;
    glDeleteProgram(textProgram_);
    glDeleteTextures(1, &textTexture_);
    glDeleteVertexArrays(1, &textVao_);
    glDeleteBuffers(1, &textBuffer_);
    textProgram_ = textTexture_ = textVao_ = textBuffer_ = 0;
    loadedFont_.clear();
    glDeleteVertexArrays(1, &collisionVao_);
    glDeleteBuffers(1, &collisionVbo_);
    collisionVao_ = collisionVbo_ = 0;
    collisionCount_ = 0;
    glDeleteProgram(scene_);
    glDeleteProgram(tone_);
    glDeleteProgram(bloom_);
    glDeleteProgram(sky_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteVertexArrays(1, &screenVao_);
    glDeleteSamplers(1, &repeat_);
    glDeleteSamplers(1, &clamp_);
    scene_ = tone_ = bloom_ = sky_ = quadVbo_ = quadVao_ = screenVao_ = repeat_ = clamp_ = 0;
}
} // namespace hyprune::render
