#include "Renderer.hpp"
#include "GLState.hpp"
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
uniform mat4 vp;
out vec3 n;out vec3 worldPosition;out vec2 uv;out vec2 lightUV;
void main(){gl_Position=vp*vec4(position,1);n=normal;uv=uv0;lightUV=uv1;worldPosition=position;}
)";
const char *fragment = R"(#version 300 es
precision highp float;
in vec3 n;in vec3 worldPosition;in vec2 uv;in vec2 lightUV;
uniform sampler2D albedo,lightmap,normalMap,mrMap,aoMap,emissiveMap;
uniform mat3 uvMatrix[5];uniform int uvSet[5];uniform int present[5];
uniform vec3 color,emission,eye;
uniform int hasTexture,baked,lightSrgb,unlit,surface,details;
uniform float intensity,metallic,roughness,normalScale,occlusionStrength;
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
 vec3 base=color*(present[0]!=0?texture(albedo,coords(0)).rgb:vec3(1));
 vec3 emissive=emission*(present[4]!=0?texture(emissiveMap,coords(4)).rgb:vec3(1));
 vec3 N=normalize(n),L=normalize(vec3(.3,1,.5));vec3 geometric=N;
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
  vec3 skyF=F0+(max(vec3(1.-rough),F0)-F0)*pow(1.-nv,5.);
  float visibility=baked!=0?clamp(dot(irradiance,vec3(.2126,.7152,.0722)),0.,1.):1.;
  float detail=baked!=0?clamp(1.+.3*(dot(N,L)-dot(geometric,L)),.7,1.3):1.;
  lit=lit*(1.-metal)*detail+(key+sky*skyF)*visibility*ao;
 }
 if(unlit!=0)lit=base;
 lit+=emissive;
 float fog=1.-exp(-length(worldPosition.xz)*.008);lit=mix(lit,vec3(.028,.045,.065),fog);
 result=vec4(lit,1);
}
)";
const char *screenVertex = R"(#version 300 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)";
const char *toneFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D frame;uniform vec2 resolution;uniform int typing;out vec4 result;
vec3 encode(vec3 x){return mix(12.92*x,1.055*pow(x,vec3(1./2.4))-.055,step(vec3(.0031308),x));}
void main(){vec4 sampleColor=texture(frame,vec2(uv.x,1.-uv.y));vec3 x=sampleColor.rgb;vec3 mapped=mix(x,clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.),sampleColor.a);vec3 c=encode(mapped);
 vec2 p=abs(gl_FragCoord.xy-resolution*.5);if((p.x<1.&&p.y<7.)||(p.y<1.&&p.x<7.))c=typing!=0?vec3(.5,1.,.65):vec3(.9);
 result=vec4(c,1);}
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
    scene_ = program(vertex, fragment);
    tone_ = program(screenVertex, toneFragment);
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
    world_.reset();
}
void Renderer::prepare(std::shared_ptr<const world::World> world) {
    GLState state;
    init();
    std::vector<Batch> batches;
    std::vector<GLuint> textures, colors, samplers;
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
        if (anisotropy) glGetFloatv(0x84FF, &maximum);
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
                glSamplerParameterf(sampler, 0x84FE, std::min(8.f, maximum));
        }
        for (auto &mesh : world->meshes) {
            Batch b;
            layout(b.vao, b.vbo);
            b.count = mesh.vertices.size();
            b.material = mesh.material;
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
}
void Renderer::freeTargets() {
    glDeleteFramebuffers(1, &msaa_);
    glDeleteFramebuffers(1, &resolve_);
    glDeleteRenderbuffers(1, &color_);
    glDeleteRenderbuffers(1, &depth_);
    glDeleteTextures(1, &resolveTexture_);
    msaa_ = resolve_ = color_ = depth_ = resolveTexture_ = 0;
    width_ = height_ = 0;
}
void Renderer::targets(int w, int h) {
    if (w == width_ && h == height_)
        return;
    freeTargets();
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(4, maxSamples);
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
    width_ = w;
    height_ = h;
}
void Renderer::draw(GLuint target, int w, int h, const sim::Camera &camera, std::span<const surfaces::Surface> surfaces,
                    bool typing) {
    if (!world_)
        return;
    GLState state;
    init();
    targets(w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
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
    glUseProgram(scene_);
    auto vp = perspective(float(w) / h) * camera.view();
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
    glBindSampler(0, repeat_);
    glBindSampler(1, clamp_);
    for (auto b : batches_) {
        const auto &m = world_->materials[b.material];
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
        float matrices[45]; GLint sets[5], present[5];
        for (int i = 0; i < 5; ++i) {
            const auto &slot = m.slots[i];
            float c = std::cos(slot.rotation), s = std::sin(slot.rotation);
            float matrix[9]{c*slot.scale[0], s*slot.scale[0], 0, -s*slot.scale[1], c*slot.scale[1], 0,
                            slot.offset[0], slot.offset[1], 1};
            std::copy_n(matrix, 9, matrices+i*9);
            sets[i] = slot.texCoord; present[i] = slot.texture >= 0;
            int unit = i == 0 ? 0 : i+1;
            glActiveTexture(GL_TEXTURE0+unit);
            glBindTexture(GL_TEXTURE_2D, present[i] ? (i == 0 || i == 4 ? colorTextures_ : textures_)[slot.texture] : 0);
            glBindSampler(unit, present[i] ? samplers_[slot.texture] : repeat_);
        }
        glUniformMatrix3fv(glGetUniformLocation(scene_, "uvMatrix"), 5, GL_FALSE, matrices);
        glUniform1iv(glGetUniformLocation(scene_, "uvSet"), 5, sets);
        glUniform1iv(glGetUniformLocation(scene_, "present"), 5, present);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m.lightmap >= 0 ? textures_[m.lightmap] : 0);
        glBindVertexArray(b.vao);
        glDrawArrays(GL_TRIANGLES, 0, b.count);
    }
    glBindSampler(0, clamp_);
    uniform(scene_, "baked", 0);
    uniform(scene_, "unlit", 1);
    uniform(scene_, "surface", 1);
    uniform(scene_, "emission", Vec3{});
    for (const auto &s : surfaces) {
        Vec3 right{std::cos(s.yaw), 0, -std::sin(s.yaw)}, up{0, 1, 0}, normal{std::sin(s.yaw), 0, std::cos(s.yaw)};
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
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glUseProgram(tone_);
    uniform(tone_, "frame", 0);
    uniform(tone_, "typing", typing);
    glUniform2f(glGetUniformLocation(tone_, "resolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    ++frames;
}
void Renderer::destroy() {
    GLState state;
    freeWorld();
    freeTargets();
    glDeleteProgram(scene_);
    glDeleteProgram(tone_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteVertexArrays(1, &screenVao_);
    glDeleteSamplers(1, &repeat_);
    glDeleteSamplers(1, &clamp_);
    scene_ = tone_ = quadVbo_ = quadVao_ = screenVao_ = repeat_ = clamp_ = 0;
}
} // namespace hyprune::render
