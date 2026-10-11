#include "abiotic/bone_points.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace abiotic {
namespace {
constexpr Addr MeshField=0x320, OwnerField=0x20;
constexpr Addr ComponentSpaceArray=0x988;
constexpr Addr ComponentWorldCandidate=0x1D0; // UNVERIFIED SceneComponent ComponentToWorld hypothesis
struct Quaternion { double x{},y{},z{},w{}; };
struct Transform {
    Quaternion q;
    Vec3 t;
    double padding{};
    Vec3 scale;
    double tail{};
};
static_assert(sizeof(Transform)==96);
struct BoneArray { Addr data{}; std::int32_t count{},capacity{}; };
static_assert(sizeof(BoneArray)==16);
bool reasonable(const Transform& a){
    auto finite=[](double v){return std::isfinite(v);};
    if(!finite(a.q.x)||!finite(a.q.y)||!finite(a.q.z)||!finite(a.q.w)||
       !finite(a.t.x)||!finite(a.t.y)||!finite(a.t.z)||
       !finite(a.scale.x)||!finite(a.scale.y)||!finite(a.scale.z))return false;
    const double n=a.q.x*a.q.x+a.q.y*a.q.y+a.q.z*a.q.z+a.q.w*a.q.w;
    return n>0.95&&n<1.05&&
       std::abs(a.scale.x)<20&&std::abs(a.scale.y)<20&&std::abs(a.scale.z)<20&&
       std::abs(a.t.x)<5e7&&std::abs(a.t.y)<5e7&&std::abs(a.t.z)<5e7;
}
Vec3 rotate(const Quaternion& q,Vec3 v){
    const Vec3 u{q.x,q.y,q.z};
    const Vec3 cross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    const Vec3 t{2*cross.x,2*cross.y,2*cross.z};
    const Vec3 cross2{u.y*t.z-u.z*t.y,u.z*t.x-u.x*t.z,u.x*t.y-u.y*t.x};
    return {v.x+q.w*t.x+cross2.x,v.y+q.w*t.y+cross2.y,v.z+q.w*t.z+cross2.z};
}
}
std::vector<BoneDebugPoint> projectPestBones(UE5& ue,Addr actor,const Camera& cam,int width,int height){
    std::vector<BoneDebugPoint> out;
    // This feature is deliberately restricted until other enemy mesh layouts are validated.
    if(!ue.saneObject(actor)||width<=0||height<=0)return out;
    const Addr mesh=ue.memory().pointer(actor+MeshField);
    if(!mesh||ue.memory().pointer(mesh+OwnerField)!=actor||
       !ue.saneObject(mesh)||ue.className(ue.classOf(mesh))!="SkeletalMeshComponent")return out;
    const auto hdr=ue.memory().get<BoneArray>(mesh+ComponentSpaceArray);
    if(!hdr||hdr->count!=10||hdr->capacity<10||hdr->capacity>32||!validPtr(hdr->data))return out;
    const auto world=ue.memory().get<Transform>(mesh+ComponentWorldCandidate);
    if(!world||!reasonable(*world))return out;
    // Guard the unverified ComponentToWorld offset: its translation should
    // be close to the known root-component world position for a small Pest.
    Vec3 actorWorld{};
    if(!ue.actorPos(actor,actorWorld))return out;
    const double dx=world->t.x-actorWorld.x,dy=world->t.y-actorWorld.y,dz=world->t.z-actorWorld.z;
    if(dx*dx+dy*dy+dz*dz>400.0*400.0)return out;
    std::array<Transform,10> bones{};
    if(!ue.memory().read(hdr->data,bones.data(),sizeof(bones)))return out;
    for(int i=0;i<10;++i){
        const auto& b=bones[static_cast<size_t>(i)];
        if(!reasonable(b))return {}; // Do not draw a partial invalid pose.
        const Vec3 scaled{b.t.x*world->scale.x,b.t.y*world->scale.y,b.t.z*world->scale.z};
        const Vec3 rotated=rotate(world->q,scaled);
        const Vec3 p{world->t.x+rotated.x,world->t.y+rotated.y,world->t.z+rotated.z};
        auto screen=project(cam,p,width,height);
        if(screen.visible)out.push_back({i,screen});
    }
    return out;
}
}
