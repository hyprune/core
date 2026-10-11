// RFC-0012 world-anchored markers: icons, labels, stacks and edge arrows drawn at
// native output resolution in the present pass, with that frame's camera. The
// caller's GL state guard covers everything here; this file only issues draws
// into the bound framebuffer and leaves blending, scissor and depth off.
#include "Renderer.hpp"
#include "interaction/Hands.hpp"
#include <cmath>
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
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("marker shader: ") + log);
    }
    return s;
}
GLuint link(const char *vs, const char *fs) {
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs), p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(p);
        throw std::runtime_error("marker program link failed");
    }
    return p;
}
constexpr const char *quadVertex = R"(#version 300 es
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;out vec2 uv;
void main(){gl_Position=vec4(position,0,1);uv=texcoord;})";
// Shapes in pixels around the icon centre (y down), with a dark rim for contrast.
constexpr const char *shapeFragment = R"(#version 300 es
precision highp float;in vec2 uv;out vec4 result;
uniform int shape;uniform float radius;uniform vec3 color;uniform float alpha;uniform int ring;uniform int dashed;
uniform float angle;uniform float px;uniform vec2 extent;
float box(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.))+min(max(d.x,d.y),0.);}
float tri(vec2 p,vec2 q){p.x=abs(p.x);vec2 a=p-q*clamp(dot(p,q)/dot(q,q),0.,1.);vec2 b=p-q*vec2(clamp(p.x/q.x,0.,1.),1.);
 float s=-sign(q.y);vec2 d=min(vec2(dot(a,a),s*(p.x*q.y-p.y*q.x)),vec2(dot(b,b),s*(p.y-q.y)));return -sqrt(d.x)*sign(d.y);}
float hexagon(vec2 p,float r){const vec3 k=vec3(-.866025404,.5,.577350269);p=abs(p);p-=2.*min(dot(k.xy,p),0.)*k.xy;
 p-=vec2(clamp(p.x,-k.z*r,k.z*r),r);return length(p)*sign(p.y);}
void main(){
 vec2 p=uv;float r=radius;float d;
 if(shape==0)d=(abs(p.x)+abs(p.y)-r*.92)*.7071;                                   // quest: diamond
 else if(shape==1)d=abs((abs(p.x)+abs(p.y)-r*.8)*.7071)-r*.14;                     // optional quest: hollow diamond
 else if(shape==2)d=min(length(p)-r*.42,abs(length(p)-r*.8)-r*.1);                 // waypoint/route: dot in a ring
 else if(shape==3)d=min(box(p-vec2(0.,r*.32),vec2(r*.56,r*.42)),tri(p-vec2(0.,-r*.92),vec2(r*.86,r*.8))); // home
 else if(shape==4)d=abs(box(p,vec2(r*.86,r*.5))-r*.06)-r*.1;                       // workspace: a screen
 else if(shape==5)d=tri(p-vec2(0.,-r*.8),vec2(r*.82,r*1.45));                      // place/vista: a peak
 else if(shape==6)d=min(length(p)-r*.24,abs(length(p)-r*.72)-r*.13);              // focus: a target
 else if(shape==7)d=abs(hexagon(p,r*.66))-r*.13;                                   // break: hexagon
 else if(shape==8){vec2 q=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*p;    // edge arrow
  d=tri(vec2(q.y,-q.x)-vec2(0.,-r*.9),vec2(r*.75,r*1.4));}
 else if(shape==9)d=length(p)-r;                                                   // stack badge
 else if(shape==10){vec2 q=p;q.y*=float(ring==0?1:-1);d=tri(q-vec2(0.,-r*.5),vec2(r*.7,r*.8));} // level chevron
 else d=box(p,extent-vec2(r))-r;                                                   // label panel
 if(ring==1&&shape<8)d=min(d,abs(length(p)-r*1.18)-r*.08);                         // tier 1 ring
 float a=clamp(.5-d/px,0.,1.);
 float rim=clamp(.5-(d+1.6*px)/px,0.,1.);
 vec3 c=shape==11?color:mix(vec3(.02,.03,.04),color,rim);
 if(dashed==1&&shape<8){float t=atan(p.y,p.x)*1.9099;float dash=step(.5,fract(t));
  float o=clamp(.5-(abs(length(p)-r*1.32)-.9*px)/px,0.,1.)*dash;
  a=max(a*.55,o);c=mix(c,color,o);}
 a*=alpha;result=vec4(c*a,a);})";
constexpr const char *textFragment = R"(#version 300 es
precision highp float;in vec2 uv;uniform sampler2D atlas;uniform vec3 color;uniform float alpha;out vec4 result;
void main(){float a=texture(atlas,uv).r*alpha;result=vec4(color*a,a);})";
int shapeOf(const std::string &icon, bool arrow) {
    if (arrow)
        return 8;
    if (icon == "quest" || icon == "offer" || icon == "turn-in")
        return 0;
    if (icon == "quest-optional")
        return 1;
    if (icon == "waypoint" || icon == "route")
        return 2;
    if (icon == "home")
        return 3;
    if (icon == "workspace")
        return 4;
    if (icon == "focus")
        return 6;
    if (icon == "break")
        return 7;
    return 5; // place, vista, discover, machine and the rest
}
} // namespace
void Renderer::prepareMarkers(const sim::Camera &camera, std::span<const surfaces::Surface> surfaces, int w, int h) {
    markerLayout_ = {};
    if (!markers)
        return;
    quests::MarkerView view;
    view.camera = camera;
    view.width = w;
    view.height = h;
    view.scale = std::max(logicalScale, std::clamp(w / 1920.f, 1.f, 2.f));
    view.reticleX = pointerX >= 0 ? pointerX : .5f;
    view.reticleY = pointerY >= 0 ? pointerY : .5f;
    view.now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    // Window rectangles on screen: the aimed (or used) window excludes markers, others dim them.
    float aspect = float(w) / std::max(1, h);
    const surfaces::Surface *aimed = nullptr;
    float aimedDistance = 1e9f;
    for (auto &s : surfaces)
        if (auto hit = surfaces::intersect(s, camera.position, camera.forward()); hit && hit->distance < aimedDistance) {
            aimed = &s;
            aimedDistance = hit->distance;
        }
    if (aimed && world_)
        if (auto wall = interaction::raycast(camera.position, camera.forward(), world_->collision, &world_->collisionIndex);
            wall && wall->distance < aimedDistance - .08f)
            aimed = nullptr;
    int checked = 0;
    for (auto &s : surfaces) {
        bool isAimed = &s == aimed || (frameTyping_ && s.focused);
        if (!isAimed && world_) {
            if (++checked > 16)
                continue;
            auto ray = s.center - camera.position;
            if (auto wall = interaction::raycast(camera.position, normalized(ray), world_->collision, &world_->collisionIndex);
                wall && wall->distance < length(ray) - .1f)
                continue; // behind a wall: it cannot be under a marker
        }
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        bool front = true;
        for (int i = 0; i < 4; ++i) {
            auto corner = s.center + s.right() * ((i & 1 ? .5f : -.5f) * s.width) + s.up() * ((i & 2 ? .5f : -.5f) * s.height);
            auto p = camera.project(corner, aspect);
            if (p.depth <= .01f) {
                front = false;
                break;
            }
            x0 = std::min(x0, p.x * w);
            x1 = std::max(x1, p.x * w);
            y0 = std::min(y0, p.y * h);
            y1 = std::max(y1, p.y * h);
        }
        if (front)
            view.windows.push_back({x0, y0, x1, y1, isAimed});
    }
    markerLayout_ = markers->layout(view);
    markerDiagnostics = markerLayout_.json();
    markerDiagnostics["frame"] = frames + 1;
    markerDiagnostics["windows"] = view.windows.size();
}
void Renderer::markerText(float x, float y, const std::string &value, Vec3 color, float alpha, int w, int h, float scale) {
    if (!textTexture_ || value.empty())
        return;
    std::vector<float> vertices;
    for (unsigned char c : value) {
        if (c < 32 || c >= 127)
            c = '?';
        auto g = glyphs_[c];
        float x0 = x + g.left * scale, y0 = y + (16 - g.top) * scale, x1 = x0 + g.width * scale, y1 = y0 + g.height * scale;
        float quad[6][4] = {{x0, y0, g.u, g.v},
                            {x1, y0, g.u + g.width / 512, g.v},
                            {x1, y1, g.u + g.width / 512, g.v + g.height / 256},
                            {x0, y0, g.u, g.v},
                            {x1, y1, g.u + g.width / 512, g.v + g.height / 256},
                            {x0, y1, g.u, g.v + g.height / 256}};
        for (auto &q : quad) {
            vertices.push_back(q[0] * 2 / w - 1);
            vertices.push_back(q[1] * 2 / h - 1);
            vertices.push_back(q[2]);
            vertices.push_back(q[3]);
        }
        x += g.advance * scale;
    }
    glUseProgram(markerTextProgram_);
    glUniform1i(glGetUniformLocation(markerTextProgram_, "atlas"), 0);
    glUniform3f(glGetUniformLocation(markerTextProgram_, "color"), color.x, color.y, color.z);
    glUniform1f(glGetUniformLocation(markerTextProgram_, "alpha"), alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textTexture_);
    glBindSampler(0, clamp_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertices.size() / 4);
}
float Renderer::markerTextWidth(const std::string &value, float scale) const {
    float n = 0;
    for (unsigned char c : value)
        n += glyphs_[c >= 32 && c < 127 ? c : '?'].advance * scale;
    return n;
}
void Renderer::markerShape(float x, float y, int shape, float radius, Vec3 color, float alpha, int ring, bool dashed,
                           float angle, int w, int h, float extentX, float extentY) {
    float half = shape == 11 ? 0 : radius * 1.6f;
    float hx = shape == 11 ? extentX + 2 : half, hy = shape == 11 ? extentY + 2 : half;
    float vertices[24];
    int i = 0;
    for (auto xy : {std::pair{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}}) {
        vertices[i++] = (x + xy.first * hx) * 2 / w - 1;
        vertices[i++] = (y + xy.second * hy) * 2 / h - 1;
        vertices[i++] = xy.first * hx;
        vertices[i++] = xy.second * hy;
    }
    glUseProgram(markerProgram_);
    auto u = [&](const char *name) { return glGetUniformLocation(markerProgram_, name); };
    glUniform1i(u("shape"), shape);
    glUniform1f(u("radius"), radius);
    glUniform3f(u("color"), color.x, color.y, color.z);
    glUniform1f(u("alpha"), alpha);
    glUniform1i(u("ring"), ring);
    glUniform1i(u("dashed"), dashed ? 1 : 0);
    glUniform1f(u("angle"), angle);
    glUniform1f(u("px"), 1.f);
    glUniform2f(u("extent"), extentX, extentY);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
void Renderer::drawMarkers(int w, int h) {
    if (markerLayout_.items.empty() || !textVao_)
        return;
    if (!markerProgram_) {
        markerProgram_ = link(quadVertex, shapeFragment);
        markerTextProgram_ = link(quadVertex, textFragment);
    }
    auto colour = [&](const char *key) {
        auto &c = overlayStyle[key];
        return Vec3{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
    };
    const Vec3 quest = colour("questColor"), waypointColour = colour("waypointColor"), label = colour("labelColor");
    const float s = std::max(logicalScale, std::clamp(w / 1920.f, 1.f, 2.f));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (auto &item : markerLayout_.items) {
        Vec3 c = item.kind == "waypoint" || item.kind == "route" ? waypointColour
                 : item.kind == "quest" || item.kind == "focus" || item.kind == "break"
                     ? quest
                     : label * (item.tier == 4 ? .85f : 1.f);
        float radius = (item.small ? 6.f : 12.f) * s; // 24 px icons, 12 px for the persistent diamond
        int shape = shapeOf(item.icon, item.arrow);
        if (item.arrow)
            radius = (item.small ? 9.f : 12.f) * s;
        markerShape(item.x, item.y, shape, radius, c, item.alpha, item.tier == 1 && !item.small && !item.arrow, item.occluded,
                    item.angle, w, h);
        if (item.chevron && !item.arrow)
            markerShape(item.x, item.y + (item.chevron > 0 ? -1 : 1) * (radius + 9 * s), 10, 6 * s, c, item.alpha,
                        item.chevron > 0 ? 0 : 1, false, 0, w, h);
        if (item.stack > 1) {
            float bx = item.x + radius * .95f, by = item.y - radius * .95f;
            markerShape(bx, by, 9, 7.5f * s, {.02f, .03f, .04f}, item.alpha * .9f, 0, false, 0, w, h);
            auto count = std::to_string(std::min(item.stack, 9));
            float scale = .62f * s;
            markerText(bx - markerTextWidth(count, scale) / 2, by - 6.5f * scale, count, label, item.alpha, w, h, scale);
        }
        if (item.small && item.label.empty())
            continue;
        // Distance under the icon (arrows: on the inward side), label to the right.
        float textScale = .78f * s;
        float ax = item.x, ay = item.y + radius + 4 * s;
        if (item.arrow) {
            ax = item.x - std::cos(item.angle) * (radius + 26 * s);
            ay = item.y - std::sin(item.angle) * (radius + 26 * s) - 8 * s;
        }
        auto distance = item.distance;
        float dw = markerTextWidth(distance, textScale);
        markerText(ax - dw / 2 + 1, ay + 1, distance, {0, 0, 0}, item.alpha * .7f, w, h, textScale);
        markerText(ax - dw / 2, ay, distance, label, item.alpha * .95f, w, h, textScale);
        if (!item.label.empty()) {
            float ls = .9f * s, lw = markerTextWidth(item.label, ls);
            float lx = item.arrow ? ax - lw / 2 : item.x + radius * 1.5f + 6 * s;
            float ly = item.arrow ? ay + 16 * s : item.y - 9 * ls;
            lx = std::clamp(lx, 8.f, std::max(8.f, w - lw - 8));
            markerShape(lx + lw / 2, ly + 9 * ls, 11, 4 * s, {.018f, .025f, .035f}, item.alpha * .62f, 0, false, 0, w, h,
                        lw / 2 + 6 * s, 11 * ls);
            markerText(lx, ly, item.label, item.tier == 1 ? c : label, item.alpha, w, h, ls);
        }
    }
    glDisable(GL_BLEND);
}
void Renderer::destroyMarkers() {
    if (markerProgram_)
        glDeleteProgram(markerProgram_);
    if (markerTextProgram_)
        glDeleteProgram(markerTextProgram_);
    markerProgram_ = markerTextProgram_ = 0;
}
} // namespace hyprune::render
