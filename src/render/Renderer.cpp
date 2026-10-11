#include "Renderer.hpp"
#include <EGL/egl.h>
#include "GLState.hpp"
#include "PNG.hpp"
#include "ProgramCache.hpp"
#include "Fsr.hpp"
#include "world/Environment.hpp"
#include "world/Mounts.hpp"
#include "world/Visibility.hpp"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include <cstring>
#include <numeric>
#include <unordered_map>
#include <thread>
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
GLuint program(const char *vertex, const char *fragment);
const char *vertex = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv0;
layout(location=3) in vec2 uv1;
uniform mat4 vp,model;uniform mat3 normalMatrix;
out vec3 n;out vec3 worldPosition;out vec2 uv;out vec2 lightUV;
void main(){vec4 pos=model*vec4(position,1);gl_Position=vp*pos;n=normalMatrix*normal;uv=uv0;lightUV=uv1;worldPosition=pos.xyz;}
)";
// Per-material constants, one std140 range per material (MaterialStd140).
#define MATERIAL_BLOCK "layout(std140) uniform Material{mat3 uvMatrix[5];int uvSet[5];int present[5];" \
    "vec3 color;float alpha;vec3 emission;float alphaCutoff;float intensity,metallic,roughness,normalScale," \
    "occlusionStrength;int alphaMode,hasTexture,baked,lightSrgb,unlit;};\n"
const char *depthFragment = "#version 300 es\nprecision highp float;\n" MATERIAL_BLOCK R"(in vec2 uv;in vec2 lightUV;
uniform sampler2D albedo;out vec4 result;
void main(){if(alphaMode==1){vec2 st=(uvMatrix[0]*vec3(uvSet[0]==0?uv:lightUV,1)).xy;
float a=alpha*(present[0]!=0?texture(albedo,st).a:1.);if(a<alphaCutoff)discard;}result=vec4(0);}
)";
const char *fragment = "#version 300 es\nprecision highp float;precision highp int;\n" MATERIAL_BLOCK R"(
in vec3 n;in vec3 worldPosition;in vec2 uv;in vec2 lightUV;
uniform sampler2D albedo,lightmap,normalMap,mrMap,aoMap,emissiveMap;
uniform vec3 eye;
uniform int details,nativeOutput,nativeTone;
uniform float nativeExposure;
uniform int inkEnabled;
uniform samplerCube environmentMap;
uniform float cubeLevels,envRotation,envIntensity;
uniform int excludeUnlit,effects;
#ifdef HYPRUNE_FEATURES
const int normalEnabled=HYPRUNE_NORMAL,specularEnabled=HYPRUNE_SPECULAR,aoEnabled=HYPRUNE_AO,reflectionEnabled=HYPRUNE_REFLECTION,fogMode=HYPRUNE_FOG;
#else
uniform int normalEnabled,specularEnabled,aoEnabled,reflectionEnabled,fogMode;
#endif
uniform vec3 fogColor,sunDirection,sunColor;
uniform float fogDensity,heightFalloff,baseHeight,sunIntensity;
out vec4 result;
// Octahedral geometry normal in the otherwise opaque HDR alpha channel. All
// 14 bits round-trip through FP16 exactly (normal exponents only, no NaN/Inf).
// Negative values distinguish metadata from sky/bloom coverage. No extra MRT,
// geometry submission, multisample storage or resolve is needed.
float packNormal(vec3 n){
 n/=abs(n.x)+abs(n.y)+abs(n.z);
 vec2 o=n.z>=0.?n.xy:(1.-abs(n.yx))*mix(vec2(-1),vec2(1),step(vec2(0),n.xy));
 uvec2 q=uvec2(round(clamp(o*.5+.5,0.,1.)*127.));uint bits=q.x|(q.y<<7);
 return -uintBitsToFloat(((113u+(bits>>10))<<23)|((bits&1023u)<<13));
}
// NaN/Inf guards: mix() and step() evaluate both branches, pow(x, y) is undefined for x < 0
// and for x == 0 with y <= 0 (Intel returns NaN), normalize(0) is NaN. A NaN pixel reads
// back black and smears through bloom.
vec3 decode(vec3 x){return mix(x/12.92,pow(max((x+.055)/1.055,vec3(0)),vec3(2.4)),step(vec3(.04045),x));}
vec3 safeNormalize(vec3 v,vec3 fallback){float l=dot(v,v);return l>1e-20?v*inversesqrt(l):fallback;}
vec2 coords(int i){return (uvMatrix[i]*vec3(uvSet[i]==0?uv:lightUV,1)).xy;}
vec3 mappedNormal(vec3 N,vec2 st){
 // Derivatives use the transformed coordinates: mirrored and rotated islands retain handedness.
 vec3 dx=dFdx(worldPosition),dy=dFdy(worldPosition);vec2 tx=dFdx(st),ty=dFdy(st);
 vec3 a=cross(dy,N),b=cross(N,dx);vec3 T=a*tx.x+b*ty.x,B=a*tx.y+b*ty.y;
 float magnitude=max(dot(T,T),dot(B,B));
 if(magnitude<1e-16)return N;
 vec3 map=texture(normalMap,st).xyz*2.-1.;map.xy*=normalScale;
 return safeNormalize(mat3(T*inversesqrt(magnitude),B*inversesqrt(magnitude),N)*map,N);
}
void main(){
 // Face normal from derivatives before any discard (derivatives need uniform control flow).
 vec3 face=safeNormalize(cross(dFdx(worldPosition),dFdy(worldPosition)),vec3(0,1,0));
 vec4 baseSample=present[0]!=0?texture(albedo,coords(0)):vec4(1);
 float coverage=alpha*baseSample.a;if(alphaMode==1&&coverage<alphaCutoff)discard;
 vec3 base=color*baseSample.rgb;
 vec3 emissive=emission*(present[4]!=0?texture(emissiveMap,coords(4)).rgb:vec3(1));
 // A degenerate (zero) interpolated normal falls back to the face normal, facing the eye.
 if(dot(face,eye-worldPosition)<0.)face=-face;
 vec3 N=safeNormalize(gl_FrontFacing?n:-n,face),L=safeNormalize(sunDirection,vec3(0,1,0));vec3 geometric=N;
 if(details!=0&&normalEnabled!=0&&present[1]!=0)N=mappedNormal(N,coords(1));
 vec3 irradiance=vec3(.24+.76*max(dot(N,L),0.));
 if(baked!=0){irradiance=texture(lightmap,lightUV).rgb;if(lightSrgb!=0)irradiance=decode(irradiance);irradiance*=intensity;}
 float ao=aoEnabled!=0&&present[3]!=0?mix(1.,texture(aoMap,coords(3)).r,occlusionStrength):1.;
 vec3 lit=base*irradiance*ao;
 if(details!=0&&specularEnabled!=0&&unlit==0){
  vec2 mr=present[2]!=0?texture(mrMap,coords(2)).bg:vec2(1);
  float metal=clamp(metallic*mr.x,0.,1.),rough=clamp(roughness*mr.y,.08,1.);
  vec3 V=safeNormalize(eye-worldPosition,N),H=safeNormalize(L+V,N),F0=mix(vec3(.04),base,metal);
  float nv=clamp(dot(N,V),0.,1.),nh=clamp(dot(N,H),1e-6,1.),nl=max(dot(N,L),0.);
  // Blinn-Phong exponent: 0 at roughness 1 would make pow(0, 0) on back-facing half vectors.
  float power=clamp(2./(rough*rough)-2.,1e-3,512.);
  vec3 F=F0+(1.-F0)*pow(clamp(1.-dot(V,H),0.,1.),5.);
  vec3 key=F*((power+2.)/25.132741)*pow(nh,power)*nl;
  vec3 R=reflect(-V,N);vec3 sky=mix(vec3(.055,.07,.085),vec3(.32,.43,.58),clamp(R.y*.5+.5,0.,1.));
  sky=mix(sky,vec3(.16,.21,.28),rough*rough);
  if(effects!=0&&reflectionEnabled!=0){float c=cos(envRotation),s=sin(envRotation);R=vec3(c*R.x+s*R.z,R.y,-s*R.x+c*R.z);sky=textureLod(environmentMap,R,rough*(cubeLevels-1.)).rgb*envIntensity;}
  if(effects!=0)key*=sunColor*sunIntensity;
  if(reflectionEnabled==0)sky=vec3(0);
  vec3 skyF=F0+(max(vec3(1.-rough),F0)-F0)*pow(1.-nv,5.); // nv is clamped to 0..1
  float visibility=baked!=0?clamp(dot(irradiance,vec3(.2126,.7152,.0722)),0.,1.):1.;
  float detail=baked!=0?clamp(1.+.3*(dot(N,L)-dot(geometric,L)),.7,1.3):1.;
  lit=lit*(1.-metal)*detail+(key+sky*skyF)*visibility*ao;
 }
 if(unlit!=0)lit=base;
 lit+=emissive;
 lit=min(lit,vec3(6e4)); // stays finite in FP16 targets (max 65504): no Inf into bloom/tone
 // Camera-relative integrated height density along the view segment.
 float fog=0.;
 if(fogMode!=0){
 float density=fogDensity;
 if(fogMode==2){
 float dy=(worldPosition.y-eye.y)*heightFalloff;
 float integral=abs(dy)<.001?1.:clamp((1.-exp(clamp(-dy,-40.,40.)))/dy,0.,1e6);
 density=fogDensity*exp(clamp(-(eye.y-baseHeight)*heightFalloff,-40.,40.))*integral;
 }
 fog=1.-exp(-length(worldPosition-eye)*density);
 }
 if(unlit!=0&&excludeUnlit!=0)fog=0.;lit=mix(lit,fogColor,clamp(fog,0.,1.));
 if(nativeOutput!=0){vec3 x=lit*nativeExposure;
 vec3 mapped=nativeTone==0?clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.):nativeTone==1?x/(1.+x):clamp(x,0.,1.);
 vec3 encoded=mix(12.92*mapped,1.055*pow(max(mapped,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),mapped));
 result=vec4(encoded*coverage,coverage);}
 else result=vec4(lit,alphaMode==2?coverage:inkEnabled!=0?packNormal(geometric):1.);
}
)";
// Native app pixels stay in their encoded colour space. This avoids a decode /
// encode round-trip and keeps the material shader out of the native 4K pass.
const char *nativeFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D albedo;uniform int hasTexture;uniform vec3 color;out vec4 result;
vec3 encode(vec3 x){return mix(12.92*x,1.055*pow(max(x,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),x));}
uniform float opacity;
void main(){result=(hasTexture!=0?texture(albedo,uv):vec4(encode(color),1))*opacity;if(result.a<=0.)discard;}
)";
const char *screenVertex = R"(#version 300 es
precision highp float;
out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)";
// Bilinear reconstruction with bounded unsharp masking. Depth is nearest-sampled
// (never interpolated across a wall edge), preserving native window occlusion.
// Samplers default to lowp in GLSL ES: NVIDIA then returns depth at fp16 (steps of
// ~1 m at 10 m), which hid walls 3 cm in front of their world panel.
const char *upscaleFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D image,bloomMap;uniform highp sampler2D depthImage;uniform float sharpness,exposure,bloomStrength;uniform int toneMapping;out vec4 result;
void main(){vec2 st=uv,d=1./vec2(textureSize(image,0));
 vec4 c=texture(image,st);result=c;
 if(sharpness>0.){vec3 a=texture(image,st+vec2(d.x,0)).rgb,b=texture(image,st-vec2(d.x,0)).rgb;
 vec3 e=texture(image,st+vec2(0,d.y)).rgb,f=texture(image,st-vec2(0,d.y)).rgb;
 vec3 lo=min(c.rgb,min(min(a,b),min(e,f))),hi=max(c.rgb,max(max(a,b),max(e,f)));
 result=vec4(clamp(c.rgb+sharpness*(c.rgb-(a+b+e+f)*.25),lo,hi),c.a);}
 vec3 x=(result.rgb+texture(bloomMap,st).rgb*bloomStrength)*exposure;
 vec3 mapped=toneMapping==0?clamp((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14),0.,1.):toneMapping==1?x/(1.+x):clamp(x,0.,1.);
 result=vec4(mix(12.92*mapped,1.055*pow(max(mapped,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),mapped)),0);
 ivec2 size=textureSize(depthImage,0);gl_FragDepth=texelFetch(depthImage,clamp(ivec2(st*vec2(size)),ivec2(0),size-1),0).r;}
)";
const char *toneFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D frame,bloomMap;uniform vec2 resolution,pointer;uniform int typing,toneMapping,scope;uniform float exposure,bloomStrength;out vec4 result;
vec3 encode(vec3 x){return mix(12.92*x,1.055*pow(max(x,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),x));}
void main(){vec2 st=vec2(uv.x,1.-uv.y);vec4 sampleColor=texture(frame,st);vec3 c=sampleColor.rgb;
 if(scope!=0){vec2 q=(gl_FragCoord.xy/resolution-.5)*vec2(resolution.x/resolution.y,1.);c*=1.-.86*smoothstep(.24,.48,length(q));float ring=abs(length(q)-.28);if(ring<.001)c=mix(c,vec3(.5,.8,.65),.4);}
 result=vec4(c,1);}
)";
// The reticle as a signed distance field in output pixels (y down, origin at the aim
// point), antialiased over one pixel, with an optional dark outline for contrast.
const char *reticleFragment = R"(#version 300 es
precision highp float;
uniform vec2 centre;uniform vec3 color;uniform int style,state,outline;uniform float len,thick,gap,opacity,px;out vec4 result;
float box(vec2 p,vec2 a,vec2 b){vec2 c=(a+b)*.5,e=abs(b-a)*.5;vec2 d=abs(p-c)-e;return length(max(d,0.))+min(max(d.x,d.y),0.);}
float seg(vec2 p,vec2 a,vec2 b){vec2 pa=p-a,ba=b-a;float h=clamp(dot(pa,ba)/dot(ba,ba),0.,1.);return length(pa-ba*h);}
float arms(vec2 p,bool top){float t=thick*.5,d=1e9;
 d=min(d,box(p,vec2(gap,-t),vec2(len,t)));d=min(d,box(p,vec2(-len,-t),vec2(-gap,t)));
 d=min(d,box(p,vec2(-t,gap),vec2(t,len)));if(top)d=min(d,box(p,vec2(-t,-len),vec2(t,-gap)));return d;}
float brackets(vec2 p,float r){vec2 q=abs(p);float k=r*.42,t=thick*.5;
 return min(box(q,vec2(r-k,r-t),vec2(r+t,r+t)),box(q,vec2(r-t,r-k),vec2(r+t,r+t)));}
void main(){vec2 p=gl_FragCoord.xy-centre;float d=1e9,t=thick*.5;
 bool hollow=state==3;
 if(state==4){float c=.70710678;p=vec2(c*p.x-c*p.y,c*p.x+c*p.y);}
 if(state==6||state==4){d=arms(p,true);} // typing: a plain cross; invalid: every style becomes an x
 else if(style==0||style==1||style==4){d=arms(p,style!=1);}
 else if(style==2){float r=max(gap+thick,len*.62);d=abs(length(p)-r)-t;}
 else if(style==5){vec2 q=vec2(abs(p.x),p.y);d=seg(q,vec2(0.,gap*.35),vec2(len*.62,gap*.35+len*.55))-t;}
 float dot=style==3?max(thick*1.25,1.6*px):max(thick*.75,1.1*px);
 bool wantsDot=(style<=3)&&state!=4&&state!=6;
 if(wantsDot)d=min(d,hollow?abs(length(p)-dot-px*.6)-px*.55:length(p)-dot);
 if(state==2||state==5)d=min(d,brackets(p,len+gap*.5+3.*px));
 float inner=clamp(.5-d,0.,1.),outer=outline!=0?clamp(.5-(d-px),0.,1.):0.;
 float a=max(inner,outer*.6)*opacity; // out of range: the warning colour, hollow centre (not dimmed)
 if(a<=0.)discard;
 result=vec4(color*inner*opacity,a);}
)";
const char *bloomFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform sampler2D image;uniform vec2 stepSize;uniform float threshold;uniform int extract;out vec4 result;
// Only opaque/masked world colour reaches bloom. Alpha is normal metadata,
// never coverage here; live alpha surfaces compose after extraction.
vec3 tap(vec2 st){return texture(image,st).rgb;}
void main(){vec3 c;
 if(extract!=0){c=(tap(uv+stepSize*vec2(-.5,-.5))+tap(uv+stepSize*vec2(.5,-.5))+tap(uv+stepSize*vec2(-.5,.5))+tap(uv+stepSize*vec2(.5,.5)))*.25;float l=max(max(c.r,c.g),c.b);c*=max(0.,l-threshold)/max(l,.0001);}
 else c=tap(uv)*.227027027+ (tap(uv+stepSize*1.384615384)+tap(uv-stepSize*1.384615384))*.316216216+(tap(uv+stepSize*3.23076923)+tap(uv-stepSize*3.23076923))*.07027027;
 result=vec4(c,1);}
)";
const char *skyFragment = R"(#version 300 es
precision highp float;
in vec2 uv;uniform samplerCube environmentMap;uniform vec3 forward,right,up;uniform float tanFov,aspect,rotation;uniform vec2 lensShift;uniform int skyMode;uniform vec3 zenith,horizon,ground;out vec4 result;
void main(){vec3 d=normalize(forward+right*((uv.x*2.-1.-lensShift.x)*tanFov*aspect)+up*((uv.y*2.-1.-lensShift.y)*tanFov));if(skyMode==1){result=vec4(mix(horizon,d.y>0.?zenith:ground,abs(d.y)),1);return;}float c=cos(rotation),s=sin(rotation);d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);result=vec4(textureLod(environmentMap,d,0.).rgb,1);}
)";
// Resolve colour/depth once, then pack normal + linear depth for one-fetch
// neighbour tests. MSAA coverage blends metadata only at existing geometry edges;
// the depth discontinuity still marks those edges. No extra MSAA attachment.
const char *inkPackFragment = R"(#version 300 es
precision highp float;precision highp int;
uniform highp sampler2D image,depths;layout(location=0) out highp uvec4 result;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);float d=texelFetch(depths,p,0).r;
 if(d>=.999999){result=uvec4(0);return;}
 uint f=floatBitsToUint(abs(texelFetch(image,p,0).a));
 uint bits=((((f>>23)&255u)-113u)*1024u+((f>>13)&1023u))&16383u;
 result=uvec4(bits,floatBitsToUint(.10001/max(.000001,2.0002-2.*d)),0,0);
})";
const char *inkFragment = R"(#version 300 es
precision highp float;precision highp int;
uniform highp usampler2D packedData;uniform float depthThreshold,normalThreshold;uniform int radius;out vec4 result;
uvec2 tap(ivec2 p){return texelFetch(packedData,clamp(p,ivec2(0),textureSize(packedData,0)-1),0).rg;}
vec3 normal(uint bits){
 vec2 o=vec2(bits&127u,(bits>>7)&127u)/127.*2.-1.;
 vec3 n=vec3(o,1.-abs(o.x)-abs(o.y));
 if(n.z<0.)n.xy=(1.-abs(n.yx))*mix(vec2(-1),vec2(1),step(vec2(0),n.xy));
 return normalize(n);
}
bool edge(uvec2 a,uvec2 b){
 if((a.y==0u)!=(b.y==0u))return true;
 if(a.y==0u)return false;
 float z=uintBitsToFloat(a.y),oz=uintBitsToFloat(b.y);
 if(abs(z-oz)>depthThreshold*min(z,oz))return true;
 // Flat geometry takes the cheap bit-equality path. Decode normals only at
 // changing normals; packed depth retains full float precision, even at .0001.
 if(a.x==b.x)return false;
 return 1.-dot(normal(a.x),normal(b.x))>normalThreshold;
}
void main(){ivec2 p=ivec2(gl_FragCoord.xy);uvec2 c=tap(p);vec2 d=vec2(64);
 for(int r=1;r<=radius;++r){
  if(d.x==64.){if(edge(c,tap(p+ivec2(r,0))))d.x=float(r)-.5;
               else if(edge(c,tap(p-ivec2(r,0))))d.x=.5-float(r);}
  if(d.y==64.){if(edge(c,tap(p+ivec2(0,r))))d.y=float(r)-.5;
               else if(edge(c,tap(p-ivec2(0,r))))d.y=.5-float(r);}
 }
 result=vec4(d,0,1);
})";
// A sparse native composite avoids increasing the HDR/tone shader's register
// pressure. Flat cells discard after one nearest fetch; only thin lines blend.
const char *inkCompositeFragment = R"(#version 300 es
precision highp float;
uniform highp sampler2D edges;uniform vec2 internalPerOutput;uniform float halfWidth,outputPerInternal;uniform vec3 colour;out vec4 result;
void main(){vec2 p=gl_FragCoord.xy*internalPerOutput;
 vec2 d=texelFetch(edges,ivec2(p),0).rg;
 if(min(abs(d.x),abs(d.y))>32.)discard;
 d=abs(d-(fract(p)-.5));
 float coverage=clamp((halfWidth-min(d.x,d.y))*outputPerInternal+.5,0.,1.);
 if(coverage<=0.)discard;
 result=vec4(colour,coverage);
})";
// AMD FidelityFX FSR 1: EASU (edge-adaptive upscale) then RCAS (robust contrast-
// adaptive sharpening), both on display-encoded (perceptual) colour, as AMD
// specifies. The headers need GLSL ES 3.1 (textureGather, bitfield ops).
const std::string fsrVertex = std::string("#version 310 es\n") + (screenVertex + std::string_view(screenVertex).find('\n') + 1);
const std::string fsrPrelude = "#version 310 es\nprecision highp float;precision highp int;\n#define A_GPU 1\n#define A_GLSL 1\n";
// GLSL ES has no implicit int->uint conversion; the header's constant-setup
// helpers (unused on the GPU) assign literal 0 to uint lanes three times.
std::string fsrHeader() {
    std::string text = fsr::ffxFsr1;
    for (auto [from, to] : {std::pair{"con3[2]=con3[3]=0;}", "con3[2]=con3[3]=0u;}"},
                            {" con[2]=0;\n con[3]=0;}", " con[2]=0u;\n con[3]=0u;}"}}) {
        auto at = text.find(from);
        if (at == std::string::npos)
            throw std::runtime_error("unexpected ffx_fsr1.h revision");
        text.replace(at, std::string_view(from).size(), to);
    }
    return text;
}
const std::string easuFragment = fsrPrelude + fsr::ffxA + R"(
uniform highp sampler2D image;uniform highp uvec4 con0,con1,con2,con3;out vec4 result;
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p){return textureGather(image,p,0);}
AF4 FsrEasuGF(AF2 p){return textureGather(image,p,1);}
AF4 FsrEasuBF(AF2 p){return textureGather(image,p,2);}
)" + fsrHeader() + R"(
void main(){AF3 c;FsrEasuF(c,AU2(gl_FragCoord.xy),con0,con1,con2,con3);result=vec4(c,0);}
)";
// RCAS also carries the old upscale's nearest internal-resolution depth.
const std::string rcasFragment = fsrPrelude + fsr::ffxA + R"(
uniform highp sampler2D image,depthImage;uniform highp uvec4 con;out vec4 result;
#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p){return texelFetch(image,clamp(p,ivec2(0),textureSize(image,0)-1),0);}
void FsrRcasInputF(inout AF1 r,inout AF1 g,inout AF1 b){}
)" + fsrHeader() + R"(
void main(){AF1 r,g,b;FsrRcasF(r,g,b,AU2(gl_FragCoord.xy),con);result=vec4(r,g,b,0);
 vec2 st=gl_FragCoord.xy/vec2(textureSize(image,0));ivec2 size=textureSize(depthImage,0);
 gl_FragDepth=texelFetch(depthImage,clamp(ivec2(st*vec2(size)),ivec2(0),size-1),0).r;}
)";
// FsrEasuCon / FsrRcasCon from ffx_fsr1.h, evaluated on the CPU.
std::array<GLuint, 16> easuConstants(float inW, float inH, float outW, float outH) {
    float c[16]{inW / outW, inH / outH, .5f * inW / outW - .5f, .5f * inH / outH - .5f,
                1 / inW,    1 / inH,    1 / inW,               -1 / inH,
                -1 / inW,   2 / inH,    1 / inW,               2 / inH,
                0,          4 / inH,    0,                     0};
    std::array<GLuint, 16> bits;
    std::memcpy(bits.data(), c, sizeof c);
    return bits;
}
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
// sRGB twin of the linear block formats produced by world::load.
GLenum srgbVariant(GLenum format) {
    switch (format) {
    case 0x8E8C: return 0x8E8D; // BPTC (BC7)
    case 0x83F0: return 0x8C4C; // S3TC DXT1 (BC1)
    case 0x83F3: return 0x8C4F; // S3TC DXT5 (BC3)
    case 0x93B0: return 0x93D0; // ASTC 4x4
    default: throw std::runtime_error("unknown compressed texture format");
    }
}
bool extension(std::string_view name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (int i = 0; i < count; ++i)
        if (name == reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)))
            return true;
    return false;
}
// KHR_parallel_shader_compile: compiles start early and run on driver threads
// while textures stream; a later upload job finishes each once the driver
// reports completion. Without the extension, start+finish is the old blocking path.
struct PendingProgram {
    GLuint program = 0, vertex = 0, fragment = 0;
    std::filesystem::path cache;
    std::chrono::steady_clock::time_point started;
    bool cached = false;
};
bool parallelCompile() {
    static int available = -1;
    if (available < 0) {
        available = extension("GL_KHR_parallel_shader_compile");
        if (available)
            if (auto threads = reinterpret_cast<void (*)(GLuint)>(eglGetProcAddress("glMaxShaderCompilerThreadsKHR")))
                threads(0xffffffffu); // implementation-chosen thread count
    }
    return available;
}
PendingProgram startProgram(const char *vertex, const char *fragment) {
    PendingProgram p;
    p.cache = ProgramCache::path(vertex, fragment);
    if ((p.program = ProgramCache::load(p.cache))) {
        p.cached = true;
        return p;
    }
    parallelCompile();
    p.started = std::chrono::steady_clock::now();
    auto stage = [](GLenum type, const char *source) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        return s;
    };
    p.vertex = stage(GL_VERTEX_SHADER, vertex);
    p.fragment = stage(GL_FRAGMENT_SHADER, fragment);
    p.program = glCreateProgram();
    glProgramParameteri(p.program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glAttachShader(p.program, p.vertex);
    glAttachShader(p.program, p.fragment);
    glLinkProgram(p.program);
    return p;
}
bool programReady(const PendingProgram &p) {
    if (p.cached || !p.program || !parallelCompile())
        return true;
    GLint done = 0;
    glGetProgramiv(p.program, 0x91B1 /*COMPLETION_STATUS_KHR*/, &done);
    return done;
}
GLuint finishProgram(PendingProgram &p) {
    if (p.cached)
        return p.program;
    auto fail = [&](const char *log) {
        glDeleteShader(p.vertex);
        glDeleteShader(p.fragment);
        glDeleteProgram(p.program);
        p = {};
        throw std::runtime_error(log);
    };
    char log[2048];
    for (GLuint s : {p.vertex, p.fragment}) {
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            fail(log);
        }
    }
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p.program, sizeof log, nullptr, log);
        fail(log);
    }
    glDeleteShader(p.vertex);
    glDeleteShader(p.fragment);
    ProgramCache::compileMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p.started).count();
    ProgramCache::save(p.program, p.cache);
    return std::exchange(p, {}).program;
}
GLuint program(const char *vertex, const char *fragment) {
    auto p = startProgram(vertex, fragment);
    return finishProgram(p);
}
// Preset-specialised world shader: graphics feature switches become constants,
// so the compiler drops disabled paths (smaller program, wider SIMD on Intel).
std::string specialised(const char *source, int key) {
    std::string text = source, defines = "#define HYPRUNE_FEATURES 1\n";
    const char *names[]{"HYPRUNE_NORMAL", "HYPRUNE_SPECULAR", "HYPRUNE_AO", "HYPRUNE_REFLECTION"};
    for (int i = 0; i < 4; ++i)
        defines += std::string("#define ") + names[i] + " " + std::to_string((key >> i) & 1) + "\n";
    defines += "#define HYPRUNE_FOG " + std::to_string(key >> 4) + "\n";
    return text.insert(text.find('\n') + 1, defines);
}
// Deletes shared names once: second[i] is only deleted where it differs from first[i].
template <class Delete> void release(std::vector<GLuint> &first, std::vector<GLuint> &second, Delete del) {
    for (size_t i = 0; i < second.size(); ++i)
        if (i < first.size() && second[i] == first[i])
            second[i] = 0;
    del(first.size(), first.data());
    del(second.size(), second.data());
    first.clear();
    second.clear();
}
// Uniform locations are looked up once per program and name, not per draw.
struct NameHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};
std::unordered_map<GLuint, std::unordered_map<std::string, GLint, NameHash, std::equal_to<>>> locations;
GLint location(GLuint p, std::string_view name) {
    auto &names = locations[p];
    if (auto it = names.find(name); it != names.end())
        return it->second;
    return names.emplace(name, glGetUniformLocation(p, std::string(name).c_str())).first->second;
}
void deleteProgram(GLuint &p) {
    locations.erase(p);
    glDeleteProgram(p);
    p = 0;
}
void uniform(GLuint p, const char *name, int value) {
    glUniform1i(location(p, name), value);
}
void uniform(GLuint p, const char *name, Vec3 value) {
    glUniform3f(location(p, name), value.x, value.y, value.z);
}
void uniform(GLuint p, const char *name, float value) {
    glUniform1f(location(p, name), value);
}
// std140 image of MATERIAL_BLOCK; the size is checked against the linked block.
struct MaterialStd140 {
    float uvMatrix[5][3][4];
    int32_t uvSet[5][4], present[5][4];
    float color[3], alpha, emission[3], alphaCutoff;
    float intensity, metallic, roughness, normalScale, occlusionStrength;
    int32_t alphaMode, hasTexture, baked, lightSrgb, unlit, pad[2];
};
static_assert(sizeof(MaterialStd140) == 480);
MaterialStd140 std140(const world::Material &m) {
    MaterialStd140 out{};
    for (int i = 0; i < 5; ++i) {
        const auto &slot = m.slots[i];
        float c = std::cos(slot.rotation), s = std::sin(slot.rotation);
        float matrix[3][3]{{c * slot.scale[0], s * slot.scale[0], 0},
                           {-s * slot.scale[1], c * slot.scale[1], 0},
                           {slot.offset[0], slot.offset[1], 1}};
        for (int column = 0; column < 3; ++column)
            std::copy_n(matrix[column], 3, out.uvMatrix[i][column]);
        out.uvSet[i][0] = slot.texCoord;
        out.present[i][0] = slot.texture >= 0;
    }
    std::copy_n(&m.color.x, 3, out.color);
    std::copy_n(&m.emissive.x, 3, out.emission);
    out.alpha = m.alpha;
    out.alphaCutoff = m.alphaCutoff;
    out.intensity = m.intensity;
    out.metallic = m.metallic;
    out.roughness = m.roughness;
    out.normalScale = m.normalScale;
    out.occlusionStrength = m.occlusionStrength;
    out.alphaMode = m.alphaMode;
    out.hasTexture = m.baseTexture >= 0;
    out.baked = m.lightmap >= 0;
    out.lightSrgb = m.lightmapSrgb;
    out.unlit = m.unlit;
    return out;
}
constexpr GLuint materialBinding = 0;
// Binds MATERIAL_BLOCK to its binding point; the program binary cache restores
// the program but not this per-program block binding.
void materialBlock(GLuint p) {
    auto index = glGetUniformBlockIndex(p, "Material");
    if (index == GL_INVALID_INDEX)
        throw std::runtime_error("material uniform block missing");
    GLint size = 0;
    glGetActiveUniformBlockiv(p, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
    if (size > GLint(sizeof(MaterialStd140)))
        throw std::runtime_error("material uniform block layout mismatch");
    glUniformBlockBinding(p, index, materialBinding);
}
// Inverse-transpose of the model's upper 3x3 (cofactors / determinant).
std::array<float, 9> normalMatrix(const Mat4 &m) {
    Vec3 a{m.v[0], m.v[1], m.v[2]}, b{m.v[4], m.v[5], m.v[6]}, c{m.v[8], m.v[9], m.v[10]};
    float det = dot(a, cross(b, c));
    if (std::abs(det) < 1e-12f)
        det = 1;
    auto x = cross(b, c) / det, y = cross(c, a) / det, z = cross(a, b) / det;
    return {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
}
} // namespace
void Renderer::init() {
    if (quadVao_)
        return;
    device = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    if (!depthProgram_)
        materialBlock(depthProgram_ = program(vertex, depthFragment));
    if (!inkNormals_) inkNormals_ = program(screenVertex, inkPackFragment);
    if (!inkProgram_) inkProgram_ = program(screenVertex, inkFragment);
    if (!inkComposite_) inkComposite_ = program(screenVertex, inkCompositeFragment);
    if (!scene_)
        materialBlock(scene_ = program(vertex, fragment));
    if (!native_)
        native_ = program(vertex, nativeFragment);
    if (!tone_)
        tone_ = program(screenVertex, toneFragment);
    if (!reticle_)
        reticle_ = program(screenVertex, reticleFragment);
    if (!upscale_)
        upscale_ = program(screenVertex, upscaleFragment);
    if (!bloom_)
        bloom_ = program(screenVertex, bloomFragment);
    if (!sky_)
        sky_ = program(screenVertex, skyFragment);
    // Device selection belongs to applyGraphics; explicit presets must win.
    layout(quadVao_, quadVbo_);
    glGenVertexArrays(1, &screenVao_);
    glGenSamplers(1, &repeat_);
    glGenSamplers(1, &clamp_);
    // Composed walls are magnified up close and minified across a court: trilinear + anisotropic.
    glGenSamplers(1, &mipClamp_);
    glSamplerParameteri(mipClamp_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glSamplerParameteri(mipClamp_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(mipClamp_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(mipClamp_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            std::string ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
            if (ext == "GL_EXT_texture_filter_anisotropic" || ext == "GL_ARB_texture_filter_anisotropic") {
                GLfloat max = 1;
                glGetFloatv(0x84FF, &max);
                glSamplerParameterf(mipClamp_, 0x84FE, std::min(8.f, max));
            }
        }
    }
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
        glDeleteBuffers(1, &b.ibo);
        glDeleteVertexArrays(1, &b.vao);
    }
    batches_.clear();
    release(textures_, colorTextures_, [](GLsizei n, const GLuint *ids) { glDeleteTextures(n, ids); });
    release(samplers_, dataSamplers_, [](GLsizei n, const GLuint *ids) { glDeleteSamplers(n, ids); });
    glDeleteBuffers(1, &materialUbo_);
    materialUbo_ = 0;
    gpuBytes_ = 0;
    glDeleteTextures(1, &environment_);
    environment_ = 0;
    world_.reset();
}
void Renderer::beginPrepare(std::shared_ptr<const world::World> world) {
    GLState state;
    cancelPrepare();
    warmStep_ = -1;
    // CPU decode and environment prefiltering already happen on the loader worker.
    srgbDecode_ = srgbDecodeAllowed && extension("GL_EXT_texture_sRGB_decode");
    if (world::gpuBytes(*world, srgbDecode_, {}) > world::gpuBudget)
        throw std::runtime_error("world GPU budget exceeded including material mip chains");
    pending_ = std::make_unique<PendingUpload>();
    auto p = pending_.get();
    p->world = std::move(world);
    p->profile = p->world->atmosphere.is_object() ? p->world->atmosphere : world::defaultAtmosphere();
    p->levels = intelQuality ? p->world->environmentLow : p->world->environmentHigh;
    if (p->levels.empty())
        p->levels = world::prefilterEnvironment(*p->world, intelQuality ? 64 : 128);
    p->gpuBytes = world::gpuBytes(*p->world, srgbDecode_, p->levels);
    if (p->gpuBytes > world::gpuBudget) {
        cancelPrepare();
        throw std::runtime_error("environment exceeds world GPU budget");
    }
    auto add = [&](size_t bytes, std::function<void()> run, std::string kind = "resource upload") {
        p->jobs.push_back({bytes, std::move(run), std::move(kind)});
    };
    // Every missing program starts compiling now; finish jobs run after the
    // texture and mesh uploads, by when parallel compiles have usually completed.
    std::vector<std::function<void()>> finishers;
    auto compile = [&](GLuint *target, const char *vs, const char *fs, std::string kind, bool block = false,
                       bool optional = false, bool variant = false) {
        auto pending = std::make_shared<PendingProgram>();
        add(0, [target, vs, fs, pending] { if (!*target) *pending = startProgram(vs, fs); }, "start " + kind);
        finishers.push_back([this, p, target, pending, kind, block, optional, variant] {
            p->jobs.push_back({0,
                               [this, target, pending, block, optional, variant] {
                                   if (*target || !pending->program)
                                       return;
                                   try {
                                       *target = finishProgram(*pending);
                                   } catch (const std::exception &e) {
                                       if (variant)
                                           return; // the generic world shader stays in use
                                       if (!optional)
                                           throw;
                                       fsrFailed_ = true; // FSR needs GLSL ES 3.1; bilinear stays
                                       fsrError_ = e.what();
                                       return;
                                   }
                                   if (block)
                                       materialBlock(*target);
                               },
                               kind, [pending] { return programReady(*pending); }});
        });
    };
    compile(&scene_, vertex, fragment, "shader scene_", true);
    compile(&native_, vertex, nativeFragment, "shader native_");
    compile(&tone_, screenVertex, toneFragment, "shader tone_");
    compile(&upscale_, screenVertex, upscaleFragment, "shader upscale_");
    compile(&bloom_, screenVertex, bloomFragment, "shader bloom_");
    compile(&sky_, screenVertex, skyFragment, "shader sky_");
    compile(&inkNormals_, screenVertex, inkPackFragment, "shader ink packed depth-normal");
    compile(&inkProgram_, screenVertex, inkFragment, "shader ink edges");
    compile(&inkComposite_, screenVertex, inkCompositeFragment, "shader ink composite");
    compile(&depthProgram_, vertex, depthFragment, "shader depth", true);
    {
        int key = featureKey();
        if (!sceneVariants_.contains(key)) {
            variantSources_[key] = specialised(fragment, key);
            sceneVariants_[key] = 0;
            compile(&sceneVariants_[key], vertex, variantSources_[key].c_str(), "shader scene_ preset variant", true,
                    false, true);
        }
    }
    if (upscaler == "fsr1" && !fsrFailed_) {
        compile(&easu_, fsrVertex.c_str(), easuFragment.c_str(), "shader fsr1 easu", false, true);
        compile(&rcas_, fsrVertex.c_str(), rcasFragment.c_str(), "shader fsr1 rcas", false, true);
    }
    p->textures.resize(p->world->images.size());
    p->colors.resize(p->textures.size());
    p->samplers.resize(p->textures.size());
    p->dataSamplers.resize(p->textures.size());
    constexpr size_t chunk = 256 * 1024;
    auto uses = world::imageUses(*p->world);
    for (size_t i = 0; i < p->textures.size(); ++i) {
        const auto &image = p->world->images[i];
        // One upload per image. Colour images are sRGB (hardware decode before
        // filtering); data images stay raw. An image used both ways keeps one sRGB
        // copy plus a skip-decode data sampler, or two copies without the extension.
        bool color = uses[i].color, data = uses[i].data || !color;
        bool twoCopies = color && data && !srgbDecode_;
        for (int pass = 0; pass < (twoCopies ? 2 : 1); ++pass) {
            bool srgb = color && pass == (twoCopies ? 1 : 0);
            auto texture = [p, i, srgb] { return srgb ? p->colors[i] : p->textures[i]; };
            add(0, [p, i, srgb, color, twoCopies] {
                const auto &im = p->world->images[i];
                auto &id = srgb ? p->colors[i] : p->textures[i];
                glGenTextures(1, &id);
                if (srgb && !twoCopies)
                    p->textures[i] = id; // data slots sample the same texture
                else if (!color)
                    p->colors[i] = id;
                glBindTexture(GL_TEXTURE_2D, id);
                if (!im.levels.empty()) {
                    // Transcoded KTX2: the package's own mip chain, no runtime mips.
                    glTexStorage2D(GL_TEXTURE_2D, im.levels.size(),
                                   srgb ? srgbVariant(im.compressedFormat) : im.compressedFormat, im.width, im.height);
                    return;
                }
                int levels = 1;
                for (int dim = std::max(im.width, im.height); dim > 1; dim /= 2)
                    ++levels;
                glTexStorage2D(GL_TEXTURE_2D, levels, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, im.width, im.height);
            });
            if (!image.levels.empty()) {
                GLenum format = srgb ? srgbVariant(image.compressedFormat) : image.compressedFormat;
                size_t blockBytes = image.compressedFormat == 0x83F0 ? 8 : 16; // BC1 vs BC3/BC7/ASTC 4x4
                for (size_t level = 0; level < image.levels.size(); ++level) {
                    int lw = std::max(1, image.width >> level), lh = std::max(1, image.height >> level);
                    size_t rowBytes = size_t((lw + 3) / 4) * blockBytes;
                    int blockRows = std::max(size_t(1), chunk / rowBytes);
                    for (int y = 0; y < lh; y += blockRows * 4) {
                        int count = std::min(blockRows * 4, lh - y);
                        size_t offset = size_t(y / 4) * rowBytes, bytes = size_t((count + 3) / 4) * rowBytes;
                        add(bytes, [p, i, level, y, lw, count, format, offset, bytes, texture] {
                            glBindTexture(GL_TEXTURE_2D, texture());
                            glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0, y, lw, count, format, bytes,
                                                      p->world->images[i].levels[level].data() + offset);
                        });
                    }
                }
                continue;
            }
            int rows = std::max(size_t(1), chunk / (size_t(image.width) * 4));
            for (int y = 0; y < image.height; y += rows) {
                int count = std::min(rows, image.height - y);
                add(size_t(image.width) * count * 4, [p, i, y, count, texture] {
                    const auto &im = p->world->images[i];
                    glBindTexture(GL_TEXTURE_2D, texture());
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, im.width, count, GL_RGBA, GL_UNSIGNED_BYTE,
                                    im.rgba.data() + size_t(y) * im.width * 4);
                });
            }
            add(
                0,
                [texture] {
                    glBindTexture(GL_TEXTURE_2D, texture());
                    glGenerateMipmap(GL_TEXTURE_2D);
                },
                "mipmap");
        }
        bool lightmap = std::any_of(p->world->materials.begin(), p->world->materials.end(),
                                    [&](auto &m) { return m.lightmap == int(i); });
        bool skipDecode = color && data && srgbDecode_;
        add(0, [this, p, i, lightmap, skipDecode] {
            auto s = i < p->world->samplers.size() ? p->world->samplers[i] : world::Sampler{};
            auto &id = p->samplers[i];
            glGenSamplers(1, &id);
            glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, s.minFilter);
            glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, s.magFilter);
            glSamplerParameteri(id, GL_TEXTURE_WRAP_S, lightmap ? GL_CLAMP_TO_EDGE : s.wrapS);
            glSamplerParameteri(id, GL_TEXTURE_WRAP_T, lightmap ? GL_CLAMP_TO_EDGE : s.wrapT);
            // Lightmaps use their package sampler: a mipmapped filter opts in to
            // trilinear irradiance (RFC-0004 mip-safe gutters). The bake pipeline's
            // 4 px gutters cover two levels; deeper levels would pull in black
            // atlas background or neighbouring islands.
            if (lightmap)
                glSamplerParameterf(id, GL_TEXTURE_MAX_LOD, float(lightmapMipLevels));
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            bool anisotropy = false;
            for (int n = 0; n < count; ++n) {
                std::string_view ext(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, n)));
                anisotropy |= ext == "GL_EXT_texture_filter_anisotropic" || ext == "GL_ARB_texture_filter_anisotropic";
            }
            if (anisotropy && s.magFilter == GL_LINEAR && s.minFilter == GL_LINEAR_MIPMAP_LINEAR) {
                float max = 1;
                glGetFloatv(0x84FF, &max);
                glSamplerParameterf(id, 0x84FE, std::min(graphics["anisotropy"].get<float>(), max));
            }
            p->dataSamplers[i] = id;
            if (skipDecode) {
                // Same filtering, raw texels: EXT_texture_sRGB_decode on the sampler.
                glGenSamplers(1, &p->dataSamplers[i]);
                for (GLenum name : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T}) {
                    GLint value = 0;
                    glGetSamplerParameteriv(id, name, &value);
                    glSamplerParameteri(p->dataSamplers[i], name, value);
                }
                GLfloat lod = 0, aniso = 1;
                glGetSamplerParameterfv(id, GL_TEXTURE_MAX_LOD, &lod);
                glSamplerParameterf(p->dataSamplers[i], GL_TEXTURE_MAX_LOD, lod);
                if (anisotropy) {
                    glGetSamplerParameterfv(id, 0x84FE, &aniso);
                    glSamplerParameterf(p->dataSamplers[i], 0x84FE, aniso);
                }
                glSamplerParameteri(p->dataSamplers[i], 0x8A48 /*SRGB_DECODE*/, 0x8A4A /*SKIP_DECODE*/);
            }
        });
    }
    add(
        0,
        [p] {
            GLint align = 256;
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
            p->materialStride = (sizeof(MaterialStd140) + align - 1) / align * align;
            std::vector<unsigned char> data(size_t(p->materialStride) * p->world->materials.size());
            for (size_t i = 0; i < p->world->materials.size(); ++i) {
                auto block = std140(p->world->materials[i]);
                std::memcpy(data.data() + i * p->materialStride, &block, sizeof block);
            }
            glGenBuffers(1, &p->materialUbo);
            glBindBuffer(GL_UNIFORM_BUFFER, p->materialUbo);
            glBufferData(GL_UNIFORM_BUFFER, data.size(), data.data(), GL_STATIC_DRAW);
        },
        "material constants");
    add(0, [p] {
        glGenTextures(1, &p->cube);
        glBindTexture(GL_TEXTURE_CUBE_MAP, p->cube);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    });
    for (size_t mip = 0; mip < p->levels.size(); ++mip)
        for (int face = 0; face < 6; ++face)
            add(size_t(p->levels[mip].size) * p->levels[mip].size * 16, [p, mip, face] {
                auto &l = p->levels[mip];
                glBindTexture(GL_TEXTURE_CUBE_MAP, p->cube);
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, mip, GL_RGBA16F, l.size, l.size, 0, GL_RGBA,
                             GL_FLOAT, l.faces[face].data());
            });
    p->batches.resize(p->world->meshes.size());
    for (size_t i = 0; i < p->batches.size(); ++i) {
        auto &mesh = p->world->meshes[i];
        auto &b = p->batches[i];
        b.count = mesh.vertices.size();
        b.material = mesh.material;
        b.node = mesh.node;
        b.lo = b.hi = mesh.vertices.empty() ? Vec3{} : mesh.vertices[0].position;
        for (auto &v : mesh.vertices) {
            b.center += v.position;
            b.lo = {std::min(b.lo.x, v.position.x), std::min(b.lo.y, v.position.y), std::min(b.lo.z, v.position.z)};
            b.hi = {std::max(b.hi.x, v.position.x), std::max(b.hi.y, v.position.y), std::max(b.hi.z, v.position.z)};
        }
        if (b.count)
            b.center = b.center / float(b.count);
        // Indexed meshes draw their optimized GPU copy with glDrawElements;
        // meshes without one (e.g. built in code) draw the flat list.
        bool indexed = !mesh.indices.empty();
        const auto *vertices = indexed ? &mesh.gpuVertices : &mesh.vertices;
        if (indexed)
            b.count = mesh.indices.size();
        b.indexed = indexed;
        size_t bytes = vertices->size() * sizeof(world::Vertex), indexBytes = mesh.indices.size() * 4;
        add(0, [p, i, bytes, indexBytes] {
            auto &b = p->batches[i];
            layout(b.vao, b.vbo);
            glBufferData(GL_ARRAY_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
            if (indexBytes) {
                // Element buffer binding is VAO state: our VAO is bound here.
                glGenBuffers(1, &b.ibo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ibo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBytes, nullptr, GL_STATIC_DRAW);
            }
        });
        for (size_t offset = 0; offset < bytes; offset += chunk) {
            auto count = std::min(chunk, bytes - offset);
            add(count, [p, i, offset, count, vertices] {
                glBindBuffer(GL_ARRAY_BUFFER, p->batches[i].vbo);
                glBufferSubData(GL_ARRAY_BUFFER, offset, count, reinterpret_cast<const char *>(vertices->data()) + offset);
            });
        }
        for (size_t offset = 0; offset < indexBytes; offset += chunk) {
            auto count = std::min(chunk, indexBytes - offset);
            add(count, [p, i, offset, count] {
                glBindVertexArray(p->batches[i].vao);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p->batches[i].ibo);
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, count,
                                reinterpret_cast<const char *>(p->world->meshes[i].indices.data()) + offset);
            });
        }
    }
    for (auto &finish : finishers)
        finish();
    add(
        0,
        [this] {
            init();
            textAtlas();
        },
        "font/setup");
    uploadMs_ = uploadMaxMs_ = uploadMaxJobMs_ = 0;
    uploadSlowestJob_.clear();
    uploadBytes_ = uploadSteps_ = 0;
}
bool Renderer::continuePrepare(double milliseconds, size_t byteBudget) {
    if (!pending_)
        return continueWarmup();
    GLState state;
    auto p = pending_.get();
    if (p->fence) {
        auto result = glClientWaitSync(p->fence, 0, 0);
        if (result == GL_TIMEOUT_EXPIRED)
            return false;
        if (result == GL_WAIT_FAILED) {
            cancelPrepare();
            throw std::runtime_error("world upload fence failed");
        }
        glDeleteSync(p->fence);
        p->fence = nullptr;
    }
    auto start = std::chrono::steady_clock::now();
    size_t bytes = 0;
    unsigned jobs = 0;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glActiveTexture(GL_TEXTURE0);
    // The context is shared with the compositor: an error it left pending (a failed
    // frame, a rejected buffer import) must not be blamed on this upload.
    while (glGetError() != GL_NO_ERROR) {
    }
    try {
        bool waiting = false;
        while (p->next < p->jobs.size()) {
            auto &job = p->jobs[p->next];
            if (jobs && bytes + job.bytes > byteBudget)
                break;
            if (job.ready && !job.ready()) {
                waiting = true; // a parallel compile is still running: try next tick
                break;
            }
            // Shader setup or another job may change the active texture unit.
            glActiveTexture(GL_TEXTURE0);
            auto jobStart = std::chrono::steady_clock::now();
            job.run();
            double jobMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - jobStart).count();
            if (jobMs > uploadMaxJobMs_) {
                uploadMaxJobMs_ = jobMs;
                uploadSlowestJob_ = job.kind + " #" + std::to_string(p->next);
            }
            bytes += job.bytes;
            ++p->next;
            ++jobs;
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("world GPU upload failed");
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >=
                milliseconds)
                break;
        }
        double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        uploadMs_ += elapsed;
        uploadMaxMs_ = std::max(uploadMaxMs_, elapsed);
        uploadBytes_ += bytes;
        ++uploadSteps_;
        if (jobs) {
            p->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
            return false;
        }
        if (waiting)
            return false;
        freeWorld();
        world_ = p->world;
        batches_ = std::move(p->batches);
        textures_ = std::move(p->textures);
        colorTextures_ = std::move(p->colors);
        samplers_ = std::move(p->samplers);
        dataSamplers_ = std::move(p->dataSamplers);
        materialUbo_ = p->materialUbo;
        materialStride_ = p->materialStride;
        p->materialUbo = 0;
        gpuBytes_ = p->gpuBytes;
        environment_ = p->cube;
        p->cube = 0;
        cubeLevels_ = p->levels.size();
        profile = p->profile;
        pending_.reset();
        collisionChanged();
        warmStep_ = warmShaders ? 0 : -1; warmMs_ = warmMaxMs_ = 0;
        return false;
    } catch (...) {
        cancelPrepare();
        throw;
    }
}
// All programs are uniform-driven GLES3 programs, not lazily compiled permutations.
// Exercise driver state variants (MSAA, blending, native/depth, all effects and text)
// in a private tiny target, one fenced submission per loading tick.
bool Renderer::continueWarmup() {
    if (warmStep_ < 0) return true;
    GLState guard;
    if (warmFence_) {
        auto status = glClientWaitSync(warmFence_,0,0);
        if (status == GL_TIMEOUT_EXPIRED) return false;
        if (status == GL_WAIT_FAILED) throw std::runtime_error("shader warmup fence failed");
        glDeleteSync(warmFence_); warmFence_ = nullptr;
    }
    if (warmStep_ >= 12) {
        while (!frameFences_.empty()) { glDeleteSync(frameFences_.front()); frameFences_.pop_front(); }
        warmStep_ = -1;
        freeTargets(); // Actual output size is allocated by the normal renderer.
        lastFrame_ = fpsSince_ = {}; fpsFrames_ = frames; deliveredFPS_ = 0;
        return true;
    }
    if (!warmFbo_) {
        glGenTextures(1,&warmTexture_); glBindTexture(GL_TEXTURE_2D,warmTexture_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glGenFramebuffers(1,&warmFbo_); glBindFramebuffer(GL_FRAMEBUFFER,warmFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,warmTexture_,0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("warm target");
    }
    auto saved = graphics; auto savedProfile = profile;
    bool oldDebug = debug, oldWorld = worldOnly, oldCapture = captureRequested;
    auto oldExternal = externalWorldFbo;
    auto settings = graphicsDefaults();
    settings["quality"] = warmStep_ % 2 ? "ultra" : "low";
    settings["msaa"] = std::array{1,2,4,1}[warmStep_%4];
    settings["fog"] = std::array{"off","linear","height"}[warmStep_%3];
    settings["bloom"] = std::array{"off","low","high"}[warmStep_%3];
    settings["render_scale"] = warmStep_ % 2 ? .5 : 1.; settings["dynamic_resolution"] = false;
    applyGraphics(settings);
    profile["outline"]={{"enabled",true},{"colour",{0,0,0}},{"widthPx",2},{"depthThreshold",.02},{"normalThreshold",.2}};
    debug = true; worldOnly = false; captureRequested = false; reuseFrame = false;
    externalWorldFbo = warmStep_ == 11 ? warmFbo_ : 0;
    sim::Camera view; if (!world_->spawns.empty()) view.position = world_->spawn().position + Vec3{0,1.7f,0};
    surfaces::Surface client; client.center = view.position + view.forward()*3; client.texture=warmTexture_;
    auto start = std::chrono::steady_clock::now();
    try { draw(warmFbo_,32,32,view,std::span(&client,1),warmStep_%2); }
    catch (...) { applyGraphics(saved); profile=savedProfile; debug=oldDebug; worldOnly=oldWorld; captureRequested=oldCapture; externalWorldFbo=oldExternal; throw; }
    applyGraphics(saved); profile=savedProfile; debug=oldDebug; worldOnly=oldWorld; captureRequested=oldCapture; externalWorldFbo=oldExternal;
    auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    warmMs_ += ms; warmMaxMs_ = std::max(warmMaxMs_,ms);
    ++warmStep_; warmFence_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0); glFlush();
    return false;
}
void Renderer::cancelPrepare() {
    warmStep_ = -1;
    if (warmFence_) { glDeleteSync(warmFence_); warmFence_ = nullptr; }
    if (!pending_)
        return;
    GLState state;
    auto p = pending_.get();
    if (p->fence)
        glDeleteSync(p->fence);
    for (auto b : p->batches) {
        glDeleteBuffers(1, &b.vbo);
        glDeleteBuffers(1, &b.ibo);
        glDeleteVertexArrays(1, &b.vao);
    }
    glDeleteTextures(1, &p->cube);
    release(p->textures, p->colors, [](GLsizei n, const GLuint *ids) { glDeleteTextures(n, ids); });
    release(p->samplers, p->dataSamplers, [](GLsizei n, const GLuint *ids) { glDeleteSamplers(n, ids); });
    glDeleteBuffers(1, &p->materialUbo);
    pending_.reset();
}
void Renderer::prepare(std::shared_ptr<const world::World> world) {
    beginPrepare(std::move(world));
    // Offline callers only. Runtime uses one bounded continuePrepare per tick.
    while (!continuePrepare())
        std::this_thread::yield();
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
    freeFsrTargets();
    glDeleteFramebuffers(1,&inkFbo_); glDeleteTextures(1,&inkNormal_); glDeleteTextures(1,&inkDepth_);
    inkFbo_=inkNormal_=inkDepth_=0;
    glDeleteFramebuffers(1, &msaa_);
    glDeleteFramebuffers(1, &resolve_);
    glDeleteRenderbuffers(1, &color_);
    glDeleteRenderbuffers(1, &depth_);
    glDeleteTextures(1, &resolveTexture_);
    msaa_ = resolve_ = color_ = depth_ = resolveTexture_ = 0;
    glDeleteFramebuffers(2, bloomFbo_);
    glDeleteTextures(2, bloomTexture_);
    bloomFbo_[0] = bloomFbo_[1] = bloomTexture_[0] = bloomTexture_[1] = 0;
    glDeleteTextures(1, &resolvedDepth_);
    glDeleteTextures(1, &nativeTexture_);
    glDeleteRenderbuffers(1, &nativeDepth_);
    glDeleteFramebuffers(1, &nativeFbo_);
    resolvedDepth_ = nativeTexture_ = nativeDepth_ = nativeFbo_ = 0;
    width_ = height_ = scaledWidth_ = scaledHeight_ = 0;
}
void Renderer::applyGraphics(const world::Json &config) {
    GLState guard;
    auto renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    graphics = effectiveGraphics(config, renderer ? renderer : "");
    intelQuality = graphics["quality"] == "low";
    msaa = graphics["msaa"];
    dynamicResolution = graphics["dynamic_resolution"];
    renderScale = graphics["render_scale"] == "auto" ? 0.f : graphics["render_scale"].get<float>();
    targetFPS = graphics["target_fps"];
    if (scene_)
        specializeScene();
    upscaler = getenv("HYPRUNE_UPSCALER")              ? getenv("HYPRUNE_UPSCALER")
               : graphics.value("upscaler", Json()).is_string() ? graphics["upscaler"].get<std::string>()
                                                                : "fsr1";
    // Changing samplers does not rebuild meshes/textures or invalidate client leases.
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    bool supported = false;
    for (int i = 0; i < count; ++i) {
        std::string extension = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
        supported |=
            extension == "GL_EXT_texture_filter_anisotropic" || extension == "GL_ARB_texture_filter_anisotropic";
    }
    if (supported) {
        float maximum = 1;
        glGetFloatv(0x84FF, &maximum);
        auto all = samplers_;
        all.insert(all.end(), dataSamplers_.begin(), dataSamplers_.end());
        for (auto sampler : all) {
            GLint min = 0, mag = 0;
            glGetSamplerParameteriv(sampler, GL_TEXTURE_MIN_FILTER, &min);
            glGetSamplerParameteriv(sampler, GL_TEXTURE_MAG_FILTER, &mag);
            if (min == GL_LINEAR_MIPMAP_LINEAR && mag == GL_LINEAR)
                glSamplerParameterf(sampler, 0x84FE, std::min(maximum, graphics["anisotropy"].get<float>()));
        }
    }
}
int Renderer::featureKey() const {
    return int(graphics["normal_maps"].get<bool>()) | int(graphics["specular"].get<bool>()) << 1 |
           int(graphics["ambient_occlusion"].get<bool>()) << 2 | int(graphics["reflections"].get<bool>()) << 3 |
           (graphics["fog"] == "off" ? 0 : graphics["fog"] == "linear" ? 1 : 2) << 4;
}
void Renderer::specializeScene() {
    // Outside the frame with the owner context current (prepare jobs, applyGraphics).
    int key = featureKey();
    if (sceneVariants_.contains(key))
        return; // compiled, compiling in the upload, or failed (0: generic program)
    try {
        variantSources_[key] = specialised(fragment, key);
        sceneVariants_[key] = program(vertex, variantSources_[key].c_str());
        materialBlock(sceneVariants_[key]);
    } catch (const std::exception &) {
        sceneVariants_[key] = 0;
    }
}
GLuint Renderer::worldProgram() const {
    auto it = sceneVariants_.find(featureKey());
    return it != sceneVariants_.end() && it->second ? it->second : scene_;
}
unsigned Renderer::compressedFormats() {
    // Advertised and sRGB-capable; ETC2 is core in ES 3 but often emulated on desktop GPUs.
    unsigned formats = 0;
    if (extension("GL_EXT_texture_compression_bptc"))
        formats |= world::BC7;
    if (extension("GL_EXT_texture_compression_s3tc") && extension("GL_EXT_texture_compression_s3tc_srgb"))
        formats |= world::BC1BC3;
    if (extension("GL_KHR_texture_compression_astc_ldr"))
        formats |= world::ASTC4x4;
    return formats;
}
bool Renderer::fsrPrograms() {
    if (fsrFailed_)
        return false;
    if (easu_ && rcas_)
        return true;
    try {
        if (!easu_)
            easu_ = program(fsrVertex.c_str(), easuFragment.c_str());
        if (!rcas_)
            rcas_ = program(fsrVertex.c_str(), rcasFragment.c_str());
        return true;
    } catch (const std::exception &e) {
        // Needs GLSL ES 3.1; the bilinear + sharpen path remains.
        fsrFailed_ = true;
        fsrError_ = e.what();
        return false;
    }
}
void Renderer::fsrTargets(int w, int h) {
    if (fsrInputFbo_ && fsrSize_ == std::array{scaledWidth_, scaledHeight_, w, h})
        return;
    freeFsrTargets();
    auto target = [](GLuint &fbo, GLuint &texture, int width, int height) {
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("FSR target unavailable");
    };
    target(fsrInputFbo_, fsrInputTex_, scaledWidth_, scaledHeight_);
    target(fsrEasuFbo_, fsrEasuTex_, w, h);
    fsrSize_ = {scaledWidth_, scaledHeight_, w, h};
}
void Renderer::freeFsrTargets() {
    glDeleteFramebuffers(1, &fsrInputFbo_);
    glDeleteFramebuffers(1, &fsrEasuFbo_);
    glDeleteTextures(1, &fsrInputTex_);
    glDeleteTextures(1, &fsrEasuTex_);
    fsrInputFbo_ = fsrEasuFbo_ = fsrInputTex_ = fsrEasuTex_ = 0;
    fsrSize_ = {};
}
void Renderer::targets(int w, int h) {
    int sw = std::max(1, int(std::round(w * resolution_.scale)));
    int sh = std::max(1, int(std::round(h * resolution_.scale)));
    if (w == width_ && h == height_ && sw == scaledWidth_ && sh == scaledHeight_ && allocatedSamples_ == msaa)
        return;
    freeTargets();
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::max(1, std::min(msaa, maxSamples));
    allocatedSamples_ = msaa;
    glGenFramebuffers(1, &msaa_);
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_);
    glGenRenderbuffers(1, &color_);
    glBindRenderbuffer(GL_RENDERBUFFER, color_);
    if (samples_ == 1)
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA16F, sw, sh);
    else
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA16F, sw, sh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_);
    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    if (samples_ == 1)
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sw, sh);
    else
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH_COMPONENT24, sw, sh);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR MSAA framebuffer unavailable");
    glGenFramebuffers(1, &resolve_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_);
    glGenTextures(1, &resolveTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, sw, sh, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTexture_, 0);
    glGenTextures(1, &resolvedDepth_);
    glBindTexture(GL_TEXTURE_2D, resolvedDepth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, sw, sh, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, resolvedDepth_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("HDR resolve framebuffer unavailable");
    glGenFramebuffers(1, &nativeFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, nativeFbo_);
    glGenTextures(1, &nativeTexture_);
    glBindTexture(GL_TEXTURE_2D, nativeTexture_);
    // Tone mapping precedes native composition; encoded pixels need no HDR bandwidth.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nativeTexture_, 0);
    glGenRenderbuffers(1, &nativeDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, nativeDepth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, nativeDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("native surfaces framebuffer unavailable");
    glGenFramebuffers(2, bloomFbo_);
    glGenTextures(2, bloomTexture_);
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glBindTexture(GL_TEXTURE_2D, bloomTexture_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, std::max(1, sw / 2), std::max(1, sh / 2), 0, GL_RGBA, GL_HALF_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTexture_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("bloom target unavailable");
    }
    scaledWidth_ = sw;
    scaledHeight_ = sh;
    width_ = w;
    height_ = h;
}
void Renderer::draw(GLuint target, int w, int h, const sim::Camera &camera, std::span<const surfaces::Surface> surfaces,
                    bool typing) {
    if (!world_)
        return;
    auto now = std::chrono::steady_clock::now();
    if (!fpsSince_.time_since_epoch().count()) {
        fpsSince_ = now;
        fpsFrames_ = frames;
        fpsBacklog_ = backlog_;
    }
    auto interval = std::chrono::duration<double>(now - fpsSince_).count();
    if (interval >= 1.) {
        deliveredFPS_ = (frames - fpsFrames_) / interval;
        queuePressure_ = backlog_ - fpsBacklog_ > 0 && deliveredFPS_ < targetFPS * .995;
        fpsSince_ = now;
        fpsFrames_ = frames;
        fpsBacklog_ = backlog_;
    }
    while (!frameFences_.empty()) {
        auto ready = glClientWaitSync(frameFences_.front(), 0, 0);
        if (ready != GL_ALREADY_SIGNALED && ready != GL_CONDITION_SATISFIED)
            break;
        glDeleteSync(frameFences_.front());
        frameFences_.pop_front();
    }
    bool idle = reuseFrame && !captureRequested && nativeFbo_ && w == width_ && h == height_;
    // Presentation cadence (compositor repaints): its period sets the frame divisor.
    if (lastPresent_.time_since_epoch().count()) {
        double period = std::chrono::duration<double, std::milli>(now - lastPresent_).count();
        if (period > 4 && period < 40)
            presentPeriodMs_ += (period - presentPeriodMs_) * .05;
    }
    lastPresent_ = now;
    bool behind = frameFences_.size() >= 2;
    if (!idle) {
        int divisor = std::clamp(int(std::lround(1000. / presentPeriodMs_ / std::max(1., double(targetFPS)))), 1, 4);
        // Only an explicit target slower than the display paces (30 at 60 Hz: every second
        // presentation, evenly). Automatic steps were tried and measured unreliable: queue
        // depth at render time reflects scheduling phase as much as overload, and a slower
        // cadence lets the GPU clock down (FEEL-2026-10.md).
        paceInterval = divisor;
    } else
        presentsSinceNew_ = paceInterval; // motion after idle starts at once
    bool paced = !idle && !captureRequested && frames && ++presentsSinceNew_ < paceInterval;
    auto logPresent = [&](int kind) {
        presentLog.push_back({std::chrono::duration<double, std::milli>(now.time_since_epoch()).count(), double(kind),
                              camera.yaw, camera.pitch, camera.position.x, camera.position.y, camera.position.z});
        while (presentLog.size() > 1200)
            presentLog.pop_front();
    };
    if (frames && (captureRequested || behind || idle || paced)) {
        logPresent(idle ? 2 : 1);
        GLState guard;
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_RASTERIZER_DISCARD);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        present(target, w, h, frameTyping_);
        if (idle) {
            ++idleFrames_; // Nothing changed: not a queue-pressure signal.
            return;
        }
        ++skipped_;
        if (!captureRequested && !paced)
            ++backlog_; // an intentional cadence repeat is not queue pressure
        return;
    }
    presentsSinceNew_ = 0;
    logPresent(0);
    auto started = std::chrono::steady_clock::now();
    if (lastFrame_.time_since_epoch().count())
        frameMs_ = std::chrono::duration<double, std::milli>(started - lastFrame_).count();
    lastFrame_ = started;
    frameCamera_ = camera;
    frameWorld_ = world_->id;
    frameTyping_ = typing;
    GLState state;
    init();
    if (!width_ || w != width_ || h != height_ || resolution_.requested != renderScale ||
        resolution_.targetFPS != targetFPS)
        resolution_.reset(renderScale, targetFPS, intelQuality, w * h);
    else if (dynamicResolution && std::all_of(gpu_.begin(), gpu_.end(), [](auto &timer) { return timer.available; })) {
        double ms = 0;
        for (auto &timer : gpu_)
            ms += timer.milliseconds;
        // Timers omit the enclosing compositor. Reused frames are direct evidence
        // that its remaining GPU budget was insufficient; leave extra headroom.
        resolution_.sample(queuePressure_ ? std::max(ms, 1000. / targetFPS) : ms, frameMs_ / 1000.);
    }
    resolution_.minimum = graphics["scale_min"];
    resolution_.maximum = graphics["scale_max"];
    if (!renderScale) resolution_.scale = std::clamp(resolution_.scale, resolution_.minimum, resolution_.maximum);
    targets(w, h);
    glLineWidth(1);
    gpu_[6].begin();
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
    glBindFramebuffer(GL_FRAMEBUFFER, samples_ == 1 ? resolve_ : msaa_);
    glViewport(0, 0, scaledWidth_, scaledHeight_);
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
    uniform(sky_, "skyMode", graphics["sky"] == "gradient" ? 1 : 2);
    uniform(sky_, "zenith", vec(sky["zenith"]));
    uniform(sky_, "horizon", vec(sky["horizon"]));
    uniform(sky_, "ground", vec(sky["ground"]));
    auto forward = camera.forward(), right = camera.right(), up = camera.up();
    glUniform2f(location(sky_, "lensShift"), camera.shiftX, camera.shiftY);
    uniform(sky_, "forward", forward);
    uniform(sky_, "right", right);
    uniform(sky_, "up", up);
    glUniform1f(location(sky_, "tanFov"), std::tan(camera.fov * .01745329252f / 2));
    glUniform1f(location(sky_, "aspect"), float(w) / h);
    glUniform1f(location(sky_, "rotation"), rotation);
    glBindVertexArray(screenVao_);
    if (!externalWorldFbo && graphics["sky"] != "off")
        glDrawArrays(GL_TRIANGLES, 0, 3);
    gpu_[6].end();
    gpu_[0].begin();
    glEnable(GL_DEPTH_TEST);
    const GLuint world = worldProgram(); // preset-specialised when compiled
    glUseProgram(world);
    auto vp = camera.projection(float(w) / h) * camera.view();
    glUniformMatrix4fv(location(world, "vp"), 1, GL_FALSE, vp.v.data());
    uniform(world, "albedo", 0);
    uniform(world, "lightmap", 1);
    auto ink = effectiveOutline(profile, graphics);
    bool drawInk = !externalWorldFbo && graphics["outline"].get<bool>() && ink.value("enabled", false);
    uniform(world, "inkEnabled", drawInk);
    uniform(world, "normalMap", 2);
    uniform(world, "mrMap", 3);
    uniform(world, "aoMap", 4);
    uniform(world, "emissiveMap", 5);
    uniform(world, "eye", camera.position);
    uniform(world, "details", materialDetails);
    uniform(world, "normalEnabled", graphics["normal_maps"].get<bool>());
    uniform(world, "specularEnabled", graphics["specular"].get<bool>());
    uniform(world, "aoEnabled", graphics["ambient_occlusion"].get<bool>());
    uniform(world, "reflectionEnabled", graphics["reflections"].get<bool>());
    uniform(world, "fogMode", graphics["fog"] == "off" ? 0 : graphics["fog"] == "linear" ? 1 : 2);
    uniform(world, "effects", effects);
    uniform(world, "environmentMap", 6);
    uniform(world, "excludeUnlit", fog["excludeUnlit"].get<bool>());
    uniform(world, "fogColor", vec(fog["color"]));
    uniform(world, "sunDirection", vec(sun["direction"]));
    uniform(world, "sunColor", vec(sun["color"]));
    glUniform1f(location(world, "fogDensity"), fog["density"].get<float>());
    glUniform1f(location(world, "heightFalloff"), fog["heightFalloff"].get<float>());
    glUniform1f(location(world, "baseHeight"), fog["baseHeight"].get<float>());
    glUniform1f(location(world, "sunIntensity"), sun["intensity"].get<float>());
    glUniform1f(location(world, "cubeLevels"), cubeLevels_);
    glUniform1f(location(world, "envRotation"), rotation);
    glUniform1f(location(world, "envIntensity"), 1);
    glBindSampler(0, repeat_);
    glBindSampler(1, clamp_);
    auto visible = world::visibleAreas(*world_, camera.position);
    bool regions = graphics["visibility_culling"].get<bool>();
    // Standard per-batch frustum test: world AABB (transformed for edited
    // objects) against the six clip planes of vp (Gribb/Hartmann).
    std::array<std::array<float, 4>, 6> planes;
    for (int i = 0; i < 6; ++i)
        for (int c = 0; c < 4; ++c)
            planes[i][c] = vp.v[c * 4 + 3] + (i % 2 ? -1.f : 1.f) * vp.v[c * 4 + i / 2];
    auto inFrustum = [&](size_t i) {
        auto lo = batches_[i].lo, hi = batches_[i].hi;
        if (auto edited = transforms.find(batches_[i].node); edited != transforms.end()) {
            Vec3 a = edited->second.point(lo), b = a;
            for (int corner = 1; corner < 8; ++corner) {
                auto q = edited->second.point({corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z});
                a = {std::min(a.x, q.x), std::min(a.y, q.y), std::min(a.z, q.z)};
                b = {std::max(b.x, q.x), std::max(b.y, q.y), std::max(b.z, q.z)};
            }
            lo = a, hi = b;
        }
        for (auto &p : planes)
            if (p[0] * (p[0] > 0 ? hi.x : lo.x) + p[1] * (p[1] > 0 ? hi.y : lo.y) + p[2] * (p[2] > 0 ? hi.z : lo.z) +
                    p[3] < 0)
                return false;
        return true;
    };
    auto regionVisible = [&](size_t i) {
        return !regions || transforms.contains(batches_[i].node) || world::nodeVisible(*world_, batches_[i].node, visible);
    };
    auto keep = [&](size_t i) { return regionVisible(i) && inFrustum(i); };
    std::vector<size_t> order;
    culledBatches_ = frustumCulled_ = 0;
    for (size_t i = 0; i < batches_.size(); ++i)
        if (!regionVisible(i))
            ++culledBatches_;
        else if (!inFrustum(i))
            ++frustumCulled_;
        else
            order.push_back(i);
    submittedBatches_ = order.size();
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        bool ta = world_->materials[batches_[a].material].alphaMode == 2,
             tb = world_->materials[batches_[b].material].alphaMode == 2;
        if (ta != tb)
            return !ta;
        return ta && length(batches_[a].center - camera.position) > length(batches_[b].center - camera.position);
    });
    bool depthOnly = false;
    // Per-pass constants are set once per program; per batch only the material
    // range, its textures and (for edited objects) the model/normal matrices.
    struct ProgramState {
        int native = -1;
        bool identity = false;
    };
    std::array<std::pair<GLuint, ProgramState>, 2> programState{{{world, {}}, {depthProgram_, {}}}};
    int tone = profile["toneMapping"] == "aces" ? 0 : profile["toneMapping"] == "reinhard" ? 1 : 2;
    float exposure = profile["exposure"].get<float>();
    bool haze = graphics["alpha_haze"].get<bool>();
    auto drawBatch = [&](size_t index, bool nativeOutput) {
        const auto &b = batches_[index];
        const auto &m = world_->materials[b.material];
        if (m.alphaMode == 2 && !haze)
            return;
        auto shader = depthOnly ? depthProgram_ : world;
        auto &state = programState[depthOnly].second;
        glUseProgram(shader);
        if (state.native < 0) {
            glUniformMatrix4fv(location(shader, "vp"), 1, GL_FALSE, vp.v.data());
            uniform(shader, "albedo", 0);
            uniform(shader, "nativeTone", tone);
            uniform(shader, "nativeExposure", exposure);
        }
        if (state.native != int(nativeOutput)) {
            uniform(shader, "nativeOutput", nativeOutput);
            state.native = nativeOutput;
        }
        if (m.alphaMode == 2) {
            glEnable(GL_BLEND);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
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
        glBindBufferRange(GL_UNIFORM_BUFFER, materialBinding, materialUbo_, GLintptr(b.material) * materialStride_,
                          sizeof(MaterialStd140));
        for (int i = 0; i < (depthOnly ? 1 : 5); ++i) {
            const auto &slot = m.slots[i];
            bool present = slot.texture >= 0;
            int unit = i == 0 ? 0 : i + 1;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, present ? (i == 0 || i == 4 ? colorTextures_ : textures_)[slot.texture] : 0);
            glBindSampler(unit, present ? (i == 0 || i == 4 ? samplers_ : dataSamplers_)[slot.texture] : repeat_);
        }
        if (!depthOnly) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m.lightmap >= 0 ? textures_[m.lightmap] : 0);
            glBindSampler(1, m.lightmap >= 0 ? dataSamplers_[m.lightmap] : clamp_);
        }
        // World vertices are pre-transformed; only edited objects carry a model.
        auto edited = transforms.find(b.node);
        if (edited != transforms.end() || !state.identity) {
            auto model = edited != transforms.end() ? edited->second : Mat4::identity();
            auto normals = normalMatrix(model);
            glUniformMatrix4fv(location(shader, "model"), 1, GL_FALSE, model.v.data());
            glUniformMatrix3fv(location(shader, "normalMatrix"), 1, GL_FALSE, normals.data());
            state.identity = edited == transforms.end();
        }
        glBindVertexArray(b.vao);
        if (b.indexed)
            glDrawElements(GL_TRIANGLES, b.count, GL_UNSIGNED_INT, nullptr);
        else
            glDrawArrays(GL_TRIANGLES, 0, b.count);
    };
    for (auto index : order)
        if (!externalWorldFbo && world_->materials[batches_[index].material].alphaMode != 2)
            drawBatch(index, false);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindSampler(0, clamp_);
    auto identity = Mat4::identity();
    cpu_[0] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    gpu_[0].end();
    gpu_[2].begin();
    auto bloomStarted = std::chrono::steady_clock::now();
    {
        int w = scaledWidth_, h = scaledHeight_;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_);
        if (!externalWorldFbo && samples_ > 1)
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        gpu_[2].end();
        gpu_[4].begin();
        // Only opaque world colour feeds bloom; client buffers are sampled later.
        glDisable(GL_DEPTH_TEST);
        glUseProgram(bloom_);
        uniform(bloom_, "image", 0);
        glBindVertexArray(screenVao_);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, clamp_);
        glViewport(0, 0, std::max(1, w / 2), std::max(1, h / 2));
        glUniform1f(location(bloom_, "threshold"), effects ? bloom["threshold"].get<float>() : 1e20f);
        float radius = bloom["radius"];
        if (externalWorldFbo || !effects || graphics["bloom"] == "off" || bloom["strength"].get<float>() <= 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
        } else
            for (int pass = 0; pass < (graphics["bloom"] == "low" ? 1 : 3); pass++) {
                glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[pass == 1 ? 1 : 0]);
                glBindTexture(GL_TEXTURE_2D, pass == 0 ? resolveTexture_ : bloomTexture_[pass == 1 ? 0 : 1]);
                uniform(bloom_, "extract", pass == 0);
                glUniform2f(location(bloom_, "stepSize"),
                            pass == 2   ? 0
                            : pass == 0 ? 1.f / w
                                        : radius / w,
                            pass == 1   ? 0
                            : pass == 0 ? 1.f / h
                                        : radius / h);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
    }
    gpu_[4].end();
    gpu_[7].begin();
    float inkWidth = drawInk ? (graphics["outline_quality"]=="fast"?1.f:ink.at("widthPx").get<float>())*logicalScale*scaledWidth_/w : 0;
    if (drawInk) {
        if (!inkFbo_) {
            glGenFramebuffers(1,&inkFbo_);
            glBindFramebuffer(GL_FRAMEBUFFER,inkFbo_);
            for (auto texture : {&inkNormal_, &inkDepth_}) {
                glGenTextures(1,texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,*texture);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
                glTexImage2D(GL_TEXTURE_2D,0,texture==&inkNormal_?GL_RG32UI:GL_RG16F,scaledWidth_,scaledHeight_,0,texture==&inkNormal_?GL_RG_INTEGER:GL_RG,texture==&inkNormal_?GL_UNSIGNED_INT:GL_HALF_FLOAT,nullptr);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER,inkFbo_);glViewport(0,0,scaledWidth_,scaledHeight_);
        glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glBindVertexArray(screenVao_);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,inkNormal_,0);
        glUseProgram(inkNormals_);uniform(inkNormals_,"image",0);uniform(inkNormals_,"depths",1);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,resolveTexture_);glBindSampler(0,0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,resolvedDepth_);glBindSampler(1,0);
        glDrawArrays(GL_TRIANGLES,0,3);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,inkDepth_,0);
        glUseProgram(inkProgram_);uniform(inkProgram_,"packedData",0);
        uniform(inkProgram_,"radius",std::min(9,std::max(1,int(std::ceil(inkWidth*.5f)))));
        for(auto name:{"depthThreshold","normalThreshold"}) glUniform1f(location(inkProgram_,name),ink.at(name).get<float>());
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inkNormal_);glBindSampler(0,0);
        glDrawArrays(GL_TRIANGLES,0,3);
    }
    gpu_[7].end();
    gpu_[5].begin();
    glBindFramebuffer(GL_FRAMEBUFFER, nativeFbo_);
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    if (externalWorldFbo) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, externalWorldFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, nativeFbo_);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, nativeFbo_);
        glClearDepthf(1); glClear(GL_DEPTH_BUFFER_BIT);
        glDepthFunc(GL_LESS); glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        depthOnly = true;
        for (auto i : order)
            if (world_->materials[batches_[i].material].alphaMode != 2) drawBatch(i, false);
        depthOnly = false;
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    } else {
    glUseProgram(upscale_);
    uniform(upscale_, "image", 0);
    uniform(upscale_, "depthImage", 1);
    uniform(upscale_, "bloomMap", 2);
    uniform(upscale_, "toneMapping",
            profile["toneMapping"] == "aces"       ? 0
            : profile["toneMapping"] == "reinhard" ? 1
                                                   : 2);
    glUniform1f(location(upscale_, "exposure"), profile["exposure"].get<float>());
    glUniform1f(location(upscale_, "bloomStrength"),
                effects && graphics["bloom"] != "off" ? profile["bloom"]["strength"].get<float>() : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, bloomTexture_[0]);
    glBindSampler(2, clamp_);
    glUniform1f(location(upscale_, "sharpness"),
                resolution_.scale < 1 ? graphics["sharpen"].get<float>() : 0.f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glBindSampler(0, clamp_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, resolvedDepth_);
    glBindSampler(1, 0);
    glBindVertexArray(screenVao_);
    fsrActive_ = upscaler == "fsr1" && resolution_.scale < 1 && fsrPrograms();
    if (!fsrActive_)
        glDrawArrays(GL_TRIANGLES, 0, 3);
    else {
        fsrTargets(w, h); // may allocate on unit 0: rebind the resolved world
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, resolveTexture_);
        // 1. Bloom + tone map + encode at internal resolution (no sharpening):
        //    FSR works on perceptual colour.
        glUniform1f(location(upscale_, "sharpness"), 0.f);
        glBindFramebuffer(GL_FRAMEBUFFER, fsrInputFbo_);
        glViewport(0, 0, scaledWidth_, scaledHeight_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // 2. EASU to an output-sized intermediate.
        glBindFramebuffer(GL_FRAMEBUFFER, fsrEasuFbo_);
        glViewport(0, 0, w, h);
        glUseProgram(easu_);
        auto con = easuConstants(scaledWidth_, scaledHeight_, w, h);
        for (int i = 0; i < 4; ++i)
            glUniform4uiv(location(easu_, std::array{"con0", "con1", "con2", "con3"}[i]), 1, con.data() + 4 * i);
        uniform(easu_, "image", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fsrInputTex_);
        glBindSampler(0, clamp_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // 3. RCAS into the native target, with the world's nearest depth.
        //    sharpen 0..1 maps to 2..0 stops (0 stops = RCAS maximum).
        glBindFramebuffer(GL_FRAMEBUFFER, nativeFbo_);
        glUseProgram(rcas_);
        float stops = 2.f * (1.f - std::clamp(graphics["sharpen"].get<float>(), 0.f, 1.f)), sharp = std::exp2(-stops);
        GLuint rcas[4]{};
        std::memcpy(rcas, &sharp, 4);
        glUniform4uiv(location(rcas_, "con"), 1, rcas);
        uniform(rcas_, "image", 0);
        uniform(rcas_, "depthImage", 1);
        glBindTexture(GL_TEXTURE_2D, fsrEasuTex_);
        glBindSampler(0, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    }
    gpu_[5].end();
    gpu_[8].begin();
    if(drawInk){
        glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);
        glUseProgram(inkComposite_);uniform(inkComposite_,"edges",0);uniform(inkComposite_,"colour",vec(ink.at("colour")));
        glUniform2f(location(inkComposite_,"internalPerOutput"),float(scaledWidth_)/w,float(scaledHeight_)/h);
        glUniform1f(location(inkComposite_,"halfWidth"),inkWidth*.5f);
        glUniform1f(location(inkComposite_,"outputPerInternal"),float(w)/scaledWidth_);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inkDepth_);glBindSampler(0,0);
        glBindVertexArray(screenVao_);glDrawArrays(GL_TRIANGLES,0,3);
        glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    }
    gpu_[8].end();
    glDepthFunc(GL_LESS);
    glUseProgram(native_);
    glUniformMatrix4fv(location(native_, "vp"), 1, GL_FALSE, vp.v.data());
    glUniformMatrix4fv(location(native_, "model"), 1, GL_FALSE, identity.v.data());
    uniform(native_, "albedo", 0);
    glActiveTexture(GL_TEXTURE0);
    cpu_[2] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bloomStarted).count();
    gpu_[1].begin();
    auto surfacesStarted = std::chrono::steady_clock::now();
    struct TransparentDraw {
        float depth;
        size_t index;
        bool window;
    };
    std::vector<TransparentDraw> transparent;
    for (size_t i = 0; i < surfaces.size(); ++i)
        transparent.push_back({dot(surfaces[i].center - camera.position, camera.forward()), i, true});
    if (!worldOnly && graphics["alpha_haze"].get<bool>())
        for (size_t i = 0; i < batches_.size(); ++i)
            if (keep(i) && world_->materials[batches_[i].material].alphaMode == 2) {
                auto &b = batches_[i];
                auto center = transforms.contains(b.node) ? transforms.at(b.node).point(b.center) : b.center;
                transparent.push_back({dot(center - camera.position, camera.forward()), i, false});
            }
    std::stable_sort(transparent.begin(), transparent.end(),
                     [](const auto &a, const auto &b) { return a.depth > b.depth; });
    // Native target is display-encoded after reconstruction. Blend client buffers
    // premultiplied, in depth order with world glass/haze, against opaque depth.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    for (const auto &item : transparent) {
        if (!item.window) {
            drawBatch(item.index, true);
            continue;
        }
        const auto &s = surfaces[item.index];
        glUseProgram(native_);
        glUniform1f(location(native_, "opacity"), s.opacity);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDepthMask(GL_TRUE); // Discarded transparent holes leave world depth intact.
        glBindSampler(0, clamp_);
        Vec3 right = s.right(), up = s.up(), normal = s.normal();
        auto quad = [&](float width, float height, Vec3 center, bool border) {
            std::vector<world::Vertex> vertices;
            auto rectangle = [&](float x0, float y0, float x1, float y1) {
                for (auto xy : {std::pair{x0, y0}, std::pair{x1, y0}, std::pair{x1, y1}, std::pair{x0, y0},
                                std::pair{x1, y1}, std::pair{x0, y1}}) {
                    world::Vertex v{};
                    v.position = center + right * ((xy.first - .5f) * width) + up * ((.5f - xy.second) * height);
                    v.normal = normal;
                    v.uv[0] = s.uvRect[0] + xy.first * (s.uvRect[2] - s.uvRect[0]);
                    v.uv[1] = s.uvRect[1] + xy.second * (s.uvRect[3] - s.uvRect[1]);
                    vertices.push_back(v);
                }
            };
            if (border) {
                float dx = .035f / width, dy = .035f / height;
                rectangle(0, 0, 1, dy);
                rectangle(0, 1 - dy, 1, 1);
                rectangle(0, dy, dx, 1 - dy);
                rectangle(1 - dx, dy, 1, 1 - dy);
            } else
                rectangle(0, 0, 1, 1);
            uniform(native_, "hasTexture", !border && s.texture != 0);
            uniform(native_, "color",
                    border ? (s.focused ? Vec3{.08f, .9f, .45f} : Vec3{.1f, .4f, .55f}) : Vec3{1, 1, 1});
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, border ? 0 : s.texture);
            glBindSampler(0, !border && s.mipmapped ? mipClamp_ : clamp_);
            glBindVertexArray(quadVao_);
            glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(world::Vertex), vertices.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        };
        quad(s.width + .07f, s.height + .07f, s.center - normal * .015f, true);
        quad(s.width, s.height, s.center, false);
    }
    glUseProgram(native_);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (!worldOnly) worldOverlays(camera, surfaces, w, h);
    // Editor primitives are plain vertices under the same GL guard. No seat or
    // compositor callbacks occur while the render pass draws handles/path strokes.
    glDisable(GL_DEPTH_TEST);
    uniform(native_, "hasTexture", 0);
    std::map<std::array<float, 3>, std::vector<world::Vertex>> strokes;
    if (debug) {
        uniform(native_, "color", Vec3{.2f, 1, .3f});
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
            uniform(native_, "color", Vec3{color[0], color[1], color[2]});
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
        uniform(native_, "color", Vec3{color[0], color[1], color[2]});
        glBindVertexArray(quadVao_);
        glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(world::Vertex), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    }
    cpu_[1] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - surfacesStarted).count();
    gpu_[1].end();
    gpu_[3].begin();
    auto outputStarted = std::chrono::steady_clock::now();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    present(target, w, h, typing);
    gpu_[3].end();
    cpu_[3] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - outputStarted).count();
    renderMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    ++frames;
    frameFences_.push_back(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    glFlush();
}
void Renderer::present(GLuint target, int w, int h, bool typing) {
    if (worldOnly) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, nativeFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
        glBlitFramebuffer(0,0,w,h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        return;
    }
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
    glUniform1f(location(tone_, "exposure"), profile["exposure"].get<float>());
    glUniform1f(location(tone_, "bloomStrength"),
                effects && graphics["bloom"] != "off" ? profile["bloom"]["strength"].get<float>() : 0);
    glUniform2f(location(tone_, "pointer"), pointerX, pointerY);
    uniform(tone_, "scope", scopeVignette ? 1 : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTexture_[0]);
    glBindSampler(1, clamp_);
    glUniform2f(location(tone_, "resolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, nativeTexture_);
    glBindSampler(0, clamp_);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    for (auto &label : labels) {
        float scale = std::clamp(w / 1920.f, 1.f, 2.f);
        auto caption = label.text;
        float limit = std::min(440.f * scale, w - 32.f);
        auto measure = [&] {
            float n = 0;
            for (unsigned char c : caption)
                n += glyphs_[c >= 32 && c < 127 ? c : '?'].advance * scale;
            return n;
        };
        bool truncated = false;
        while (!caption.empty() && measure() > limit - 40 * scale) {
            caption.pop_back();
            truncated = true;
        }
        if (truncated)
            caption += "...";
        float width = measure(), x = std::clamp(label.x * w, 16.f, std::max(16.f, w - width - 16));
        float y = std::clamp(label.y * h, 16.f, std::max(16.f, h - 30 * scale));
        panel(x - 6 * scale, y - 3 * scale, width + 12 * scale, 24 * scale);
        text(x, y, caption, label.color, w, h, scale);
    }
    for (auto &sprite : icons_)
        icon(sprite, w, h);
    drawReticle(w, h, typing);
    debugHUD(w, h);
}
void Renderer::drawReticle(int w, int h, bool typing) {
    if (!reticle_ || !reticle.visible || pointerX < 0 || pointerY < 0)
        return;
    auto state = typing ? int(Reticle::Typing) : reticle.state;
    auto color = typing ? Vec3{.5f, 1.f, .65f} : reticle.color;
    float px = std::max(1.f, logicalScale);
    float len = reticle.size * .5f * px, gap = (reticle.gap + reticle.spread) * px, thick = reticle.thickness * px;
    if (state == Reticle::Target)
        gap *= .6f; // tightens on a window that takes the click
    if (state == Reticle::Typing) {
        len = 7 * px;
        gap = 0;
        thick = std::max(1.f, px);
    }
    len = std::max(len, gap + thick);
    float cx = pointerX * w, cy = pointerY * h, extent = len + gap + 8 * px + thick;
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(cx - extent), int(cy - extent), int(2 * extent) + 1, int(2 * extent) + 1);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(reticle_);
    glUniform2f(location(reticle_, "centre"), cx, cy);
    uniform(reticle_, "color", color);
    uniform(reticle_, "style", reticle.style);
    uniform(reticle_, "state", state);
    uniform(reticle_, "outline", reticle.outline ? 1 : 0);
    glUniform1f(location(reticle_, "len"), len);
    glUniform1f(location(reticle_, "thick"), thick);
    glUniform1f(location(reticle_, "gap"), gap);
    glUniform1f(location(reticle_, "opacity"), reticle.opacity);
    glUniform1f(location(reticle_, "px"), px);
    glBindVertexArray(screenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    if (!reticle.hint.empty() && !typing) {
        float scale = std::clamp(w / 1920.f, 1.f, 2.f), width = 0;
        for (unsigned char c : reticle.hint)
            width += glyphs_[c >= 32 && c < 127 ? c : '?'].advance * scale;
        text(cx - width / 2, cy + extent, reticle.hint, reticle.color * reticle.opacity, w, h, scale);
    }
}
void Renderer::panel(float x, float y, float width, float height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(x), int(y), int(width), int(height));
    glClearColor(.018f, .025f, .035f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}
void Renderer::debugHUD(int w, int h) {
    if (!debug)
        return;
    float scale = std::clamp(w / 1920.f, .8f, 2.f);
    auto gpu = [&](int index) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        const std::map<int, std::string> workerPass{{0,"world_shading_shared"},{2,"msaa_resolve"},{7,"outline"},
                                                   {4,"bloom"},{5,"upscale_sharpen"},{6,"sky"}};
        auto worker = offloadState.value("worker", world::Json(nullptr));
        if (offloadState.value("activeRenderer", "") == "offload" && worker.is_object() && workerPass.contains(index)) {
            auto cost = worker.value("gpuEffectsMs", world::Json::object()).value(workerPass.at(index), world::Json(nullptr));
            if (cost.is_number()) out << cost.get<double>() << " ms (worker)";
            else out << "N/A (worker)";
        } else if (gpu_[index].available)
            out << gpu_[index].milliseconds + (index==7 ? gpu_[8].milliseconds : 0.) << " ms";
        else
            out << "N/A";
        return out.str();
    };
    auto enabled = [&](const char *key) { return graphics[key].get<bool>() ? "on" : "off"; };
    std::ostringstream message;
    message << std::fixed << std::setprecision(2) << "HYPRUNE / " << graphics["quality"].get<std::string>()
            << " / GPU timing\n"
            << "FPS " << deliveredFPS_ << "   FRAME " << frameMs_ << " ms   target " << targetFPS << "\nCPU update "
            << updateMs << "  capture " << captureMs << "  render " << renderMs_ << "  IPC " << ipcMs << "\nScale "
            << resolution_.scale << "  " << scaledWidth_ << "x" << scaledHeight_ << " / native " << w << "x" << h
            << "\nSky " << graphics["sky"].get<std::string>() << "                       " << gpu(6)
            << "\nWorld shading (shared cost)     " << gpu(0) << "\n  normal " << enabled("normal_maps")
            << " | specular " << enabled("specular") << " | IBL " << enabled("reflections") << "\n  AO "
            << enabled("ambient_occlusion") << " | fog " << graphics["fog"].get<std::string>() << "\n  anisotropy " << graphics["anisotropy"] << "x / baked shadows retained"
            << "\nMSAA " << samples_ << "x resolve                 " << gpu(2) << "\nBloom "
            << graphics["bloom"].get<std::string>() << "                       " << gpu(4)
            << "\nRenderer " << offloadState.value("activeRenderer", "in-process") << "  " << offloadState.value("powerW", world::Json(nullptr)).dump() << " W"
            << "\nUpscale " << (fsrActive_ ? "FSR 1 EASU+RCAS   " : "+ sharpen         ") << "      " << gpu(5) << "\nNative windows / haze " << enabled("alpha_haze") << "   " << gpu(1)
            << "\nInk edges " << graphics["outline_quality"].get<std::string>() << " " << enabled("outline") << "          " << gpu(7)
            << "\nVisibility " << submittedBatches_ << " drawn / " << culledBatches_ << " culled (" << frustumCulled_ << " frustum)"
            << "\nOutput + markers                " << gpu(3) << "\nSSR / runtime shadows: unsupported"
            << "\nRendered " << frames << " / reused " << skipped_;
    if (offloadState.value("activeRenderer", "") == "offload") {
        auto worker = offloadState.value("worker", world::Json::object());
        message << "\n" << offloadState.value("profile", "unknown") << " / " << offloadState.value("capW", 0.)
                << " W cap / " << offloadState.value("targetFPS", 0) << " FPS worker"
                << "\nTransfer " << worker.value("transferGpuMs", world::Json(nullptr)).dump()
                << " ms / local depth " << gpu_[5].milliseconds << " ms";
    }
    auto content = message.str();
    float rows = std::count(content.begin(), content.end(), '\n') + 1;
    scale = std::min(scale, std::max(.5f, (h - 48.f) / (rows * 22.f + 24.f)));
    panel(14 * scale, 14 * scale, std::min(float(w) - 28 * scale, 740 * scale), (rows * 22 + 24) * scale);
    text(26 * scale, 24 * scale, message.str(), {.79f, .9f, .96f}, w, h, scale);
}

void Renderer::drawLoading(GLuint target, int w, int h) {
    GLState guard;
    glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(0,0,w,h);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(.012f,.018f,.03f,1);glClear(GL_COLOR_BUFFER_BIT);
    if(textTexture_) text(w*.4f,h*.5f,"LOADING WORLD / SHADERS",{.6f,.8f,1},w,h,1.5f);
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
void Renderer::text(float x, float y, const std::string &value, Vec3 color, int w, int h, float scale) {
    if (!textTexture_)
        return;
    float left = x;
    std::vector<float> vertices;
    vertices.reserve(value.size() * 24);
    for (auto character : value) {
        auto c = static_cast<unsigned char>(character);
        if (c == '\n') {
            x = left;
            y += 21 * scale;
            continue;
        }
        if (c < 32 || c >= 127)
            c = '?';
        auto g = glyphs_[c];
        float x0 = x + g.left * scale, y0 = y + (16 - g.top) * scale, x1 = x0 + g.width * scale,
              y1 = y0 + g.height * scale;
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
        x += g.advance * scale;
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
        uniform(native_, "hasTexture", 0);
        auto linear = color("routeColor");
        auto decode = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
        linear = {decode(linear.x), decode(linear.y), decode(linear.z)};
        uniform(native_, "color", linear);
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
    if (showInteractionHints)
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
    if (showInteractionHints && !toolPrompt.is_null())
        marker(position(toolPrompt["position"]), toolPrompt["text"], false, true);
    world::Json projected = world::Json::array();
    for (auto &label : labels)
        projected.push_back({{"x", label.x}, {"y", label.y}, {"text", label.text}});
    overlayDiagnostics = {{"frame", frames + 1},   {"heading", camera.yaw},
                          {"pitch", camera.pitch}, {"areaMarkers", showAreaMarkers},
                          {"interactionHints", showInteractionHints},
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
    auto cost = [&](std::initializer_list<int> indices) -> world::Json {
        double sum = 0;
        for (int index : indices) {
            if (!gpu_[index].available)
                return nullptr;
            sum += gpu_[index].milliseconds;
        }
        return sum;
    };
    gpu = {cost({0, 6}), cost({1}), cost({2, 4, 5, 7, 8}), cost({3})};
    world::Json effects = {{"world_shading_shared", cost({0})}, {"sky", cost({6})},
                           {"msaa_resolve", cost({2})},         {"bloom", cost({4})},
                           {"upscale_sharpen", cost({5})},      {"native_surfaces_geometry", cost({1})},
                           {"output_markers", cost({3})}, {"outline", cost({7,8})}};
    return {{"renderedFrames", frames},
            {"visibility", {{"submitted",submittedBatches_},{"culled",culledBatches_},{"frustumCulled",frustumCulled_},{"total",batches_.size()}}},
            {"shaderCache", {{"hits", ProgramCache::hits}, {"misses", ProgramCache::misses}, {"rejected", ProgramCache::rejected}, {"compileMs", ProgramCache::compileMs}, {"warmupMs", warmMs_}, {"warmupMaxStepMs", warmMaxMs_}, {"warming", warmStep_ >= 0}}},
            {"deliveredFPS", deliveredFPS_},
            {"graphics", graphics},
            {"gpuEffectsMs", effects},
            {"renderScale", resolution_.scale},
            {"internalSize", {scaledWidth_, scaledHeight_}},
            {"nativeSize", {width_, height_}},
            {"targetFPS", targetFPS},
            {"frameMs", frameMs_},
            {"cpuRenderMs", renderMs_},
            {"cpuUpdateMs", updateMs},
            {"upload",
             {{"pending", bool(pending_) || warmStep_ >= 0},
              {"steps", uploadSteps_},
              {"bytes", uploadBytes_},
              {"gpuBytes", pending_ ? pending_->gpuBytes : gpuBytes_},
              {"srgbDecode", srgbDecode_},
              {"cpuMs", uploadMs_},
              {"maxStepMs", uploadMaxMs_},
              {"maxJobMs", uploadMaxJobMs_},
              {"slowestJob", uploadSlowestJob_},
              {"completedJobs", pending_ ? pending_->next : 0},
              {"jobs", pending_ ? pending_->jobs.size() : 0}}},
            {"captureMs", captureMs},
            {"gpuCaptureMs", gpuCaptureMs >= 0 ? world::Json(gpuCaptureMs) : world::Json(nullptr)},
            {"ipcMs", ipcMs},
            {"cpuPassMs", cpu},
            {"gpuPassMs", gpu},
            {"debug", debug},
            {"reusedFrames", skipped_},
            {"idleFrames", idleFrames_},
            {"upscaler", {{"requested", upscaler}, {"active", fsrActive_ ? "fsr1" : "bilinear"},
                          {"error", fsrError_.empty() ? world::Json(nullptr) : world::Json(fsrError_)}}},
            {"inFlight", frameFences_.size()},
            {"paceInterval", paceInterval},
            {"presentHz", 1000. / presentPeriodMs_},
            {"renderer", device},
            {"offload", offloadState},
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
    if (warmFence_) glDeleteSync(warmFence_);
    warmFence_ = nullptr; warmStep_ = -1;
    glDeleteFramebuffers(1, &warmFbo_); glDeleteTextures(1, &warmTexture_); warmFbo_ = warmTexture_ = 0;
    cancelPrepare();
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
    deleteProgram(textProgram_);
    glDeleteTextures(1, &textTexture_);
    glDeleteVertexArrays(1, &textVao_);
    glDeleteBuffers(1, &textBuffer_);
    textProgram_ = textTexture_ = textVao_ = textBuffer_ = 0;
    loadedFont_.clear();
    glDeleteVertexArrays(1, &collisionVao_);
    glDeleteBuffers(1, &collisionVbo_);
    collisionVao_ = collisionVbo_ = 0;
    collisionCount_ = 0;
    deleteProgram(scene_);
    deleteProgram(inkNormals_); deleteProgram(inkProgram_); deleteProgram(inkComposite_); inkNormals_=inkProgram_=inkComposite_=0;
    deleteProgram(depthProgram_); depthProgram_ = 0;
    deleteProgram(native_);
    deleteProgram(tone_);
    deleteProgram(upscale_);
    upscale_ = 0;
    deleteProgram(bloom_);
    deleteProgram(sky_);
    for (auto &[key, variant] : sceneVariants_)
        deleteProgram(variant);
    sceneVariants_.clear();
    deleteProgram(easu_);
    deleteProgram(rcas_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteVertexArrays(1, &screenVao_);
    glDeleteSamplers(1, &repeat_);
    glDeleteSamplers(1, &mipClamp_);
    glDeleteSamplers(1, &clamp_);
    deleteProgram(reticle_);
    native_ = scene_ = tone_ = bloom_ = sky_ = quadVbo_ = quadVao_ = screenVao_ = repeat_ = clamp_ = mipClamp_ = 0;
}
} // namespace hyprune::render
