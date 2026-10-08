#include "interaction/InputFile.hpp"
#include "interaction/Tools.hpp"
#include "sim/Camera.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace hyprune;
using namespace interaction;
int main() {
    auto defaults=validateInput(inputDefaults());
    assert(inputConflicts(defaults).empty());
    unsigned checked=0;
    for(auto &[mode,actions]:defaults["modes"].items()) for(auto &[action,bindings]:actions.items()) for(auto &v:bindings) {
        Input input;auto t=parseBinding(v);auto events=input.event(mode,t.key,t.mods,true,100);
        assert(events.size()==1 && events[0].name==action && events[0].pressed==!t.hold);
        if(t.hold){assert(input.tick(1099).empty());assert(input.tick(1100)[0].name==action);assert(input.tick(1200).empty());}
        if(t.key!="Wheel" && t.key!="MouseMove") {auto release=input.event("other",t.key,0,false,1300);assert(release.size()==1 && !release[0].pressed);}
        checked++;
    }
    Input multi;multi.event("world","k",0,true,0);multi.event("world","w",Shift,true,1);
    assert(multi.event("world","k",0,false,2)[0].pressed);assert(!multi.event("focus","w",0,false,3)[0].pressed);
    multi.event("world","Tab",0,true,10);assert(!multi.event("world","Tab",0,true,20)[0].pressed);
    auto next=defaults;next["longHoldMs"]=1500;multi.apply(next);assert(multi.tick(1509).empty());assert(multi.tick(1510).size()==1);
    multi.event("menu","Tab",0,false,1600);
    assert(multi.event("focus","Tab",Super,true,2000)[0].name=="exit");
    assert(multi.event("world","Return",Super,true,2001).empty());
    for(auto key:{"WheelUp","WheelDown"}){Input i;assert(i.event("world",key,Super|Shift,true,0,1)[0].name=="rotate");assert(i.event("world",key,Super,true,0,1)[0].name=="distance");}
    next=defaults;next["modes"]["world"]["debug"]={"e"};assert(inputConflicts(validateInput(next)).size()==1);
    for(auto bad:{"Super+Super+e","garbage-key","Wheel[L]","Button9999"}){bool rejected=false;try{parseBinding(bad);}catch(...){rejected=true;}assert(rejected);}
    auto dir=std::filesystem::temp_directory_path()/("hyprune-m3b-unit-"+std::to_string(getpid()));std::filesystem::create_directories(dir);
    setenv("HYPRUNE_INPUT_FILE",(dir/"input.json").c_str(),1);
    {InputFile file;assert(file.current==defaults);next=defaults;next["longHoldMs"]=1200;atomicJSON(file.path,next);assert(file.poll()==next);assert(file.current==next);auto invalid=next;invalid["longHoldMs"]=0;atomicJSON(file.path,invalid);assert(!file.poll());assert(file.current==next && !file.error.empty());file.save(defaults);assert(readJSON(file.path)==defaults);}
    std::filesystem::remove_all(dir);
    Tools tools;tools.slot=2;assert(tools.submode()=="place");tools.wheel(1);assert(tools.submode()=="lock");tools.slot=3;
    for(int i=0;i<6;i++){tools.point("org.test.world","home",{float(i),0,0});tools.point("org.test.world","home",{float(i),0,-2});auto lines=pathLines(tools.paths.back());assert(!lines.empty());tools.finishPath();tools.wheel(1);}
    auto saved=tools.saved();Tools restored;restored.restore(saved);assert(restored.saved()==saved);
    auto move=gizmo({0,0,-3},"move"),scale=gizmo({0,0,-3},"scale"),rotate=gizmo({0,0,-3},"rotate");assert(move.size()==3 && scale.size()==4 && scale.back().uniform && rotate.size()==120);assert(pickHandle(move,{0,0,0},normalized(Vec3{.5,0,-3})).has_value());
    Transform transform;transform.position={5,0,0};transform.scale={2,2,2};assert(length(transform.matrix({1,0,0}).point({1,0,0})-Vec3{5,0,0})<.0001);
    sim::Camera camera;camera.position={0,1.7,0};sim::setCrouch(camera,true,{});assert(camera.eyeHeight==1 && std::abs(camera.position.y-1)<.001);sim::setCrouch(camera,false,{});assert(camera.eyeHeight==1.7f && std::abs(camera.position.y-1.7)<.001);
    std::cout<<"PASS M3B: "<<checked<<" default variants, mode ownership, hold/repeat/aggregate releases, wheel chords, config validation/watch/save, six path modes, persistence, gizmo and crouch\n";
}
