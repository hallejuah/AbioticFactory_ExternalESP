#include "abiotic/bone_probe.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace abiotic {
namespace {
constexpr Addr OwnerOff=0x20, MeshOff=0x320;
constexpr Addr BoneSpaceOff=0x978, ComponentSpaceOff=0x988;
constexpr int MaxBones=32;
struct ArrayHeader { Addr data{}; std::int32_t count{}, capacity{}; };
static_assert(sizeof(ArrayHeader)==16);
template<class Scalar> struct Quaternion { Scalar x{},y{},z{},w{}; };
template<class Scalar> struct Triple { Scalar x{},y{},z{}; };
template<class Scalar> struct TransformShape {
    Quaternion<Scalar> q;
    Triple<Scalar> translation;
    Scalar padding{};
    Triple<Scalar> scale;
    Scalar tail{};
};
static_assert(sizeof(TransformShape<float>)==48);
static_assert(sizeof(TransformShape<double>)==96);

template<class Scalar> bool plausible(const TransformShape<Scalar>& t){
    const auto finite=[](Scalar x){return std::isfinite(static_cast<double>(x));};
    if(!finite(t.q.x)||!finite(t.q.y)||!finite(t.q.z)||!finite(t.q.w)||
       !finite(t.translation.x)||!finite(t.translation.y)||!finite(t.translation.z)||
       !finite(t.scale.x)||!finite(t.scale.y)||!finite(t.scale.z))return false;
    const double norm=double(t.q.x)*t.q.x+double(t.q.y)*t.q.y+
                      double(t.q.z)*t.q.z+double(t.q.w)*t.q.w;
    return norm>0.80&&norm<1.20&&
           std::abs(double(t.translation.x))<1e7&&
           std::abs(double(t.translation.y))<1e7&&
           std::abs(double(t.translation.z))<1e7&&
           std::abs(double(t.scale.x))<100&&
           std::abs(double(t.scale.y))<100&&
           std::abs(double(t.scale.z))<100;
}
template<class Scalar> void display(std::ostringstream& out,const TransformShape<Scalar>& t){
    out<<std::fixed<<std::setprecision(4)
       <<" q=("<<t.q.x<<","<<t.q.y<<","<<t.q.z<<","<<t.q.w<<")"
       <<" t=("<<t.translation.x<<","<<t.translation.y<<","<<t.translation.z<<")"
       <<" scale=("<<t.scale.x<<","<<t.scale.y<<","<<t.scale.z<<")";
}
void inspectArray(UE5& ue,std::ostringstream& out,Addr mesh,Addr offset,const char* label){
    out<<label<<" [mesh+0x"<<std::hex<<offset<<std::dec<<"]\n";
    const auto header=ue.memory().get<ArrayHeader>(mesh+offset);
    if(!header){out<<"  header read failed\n";return;}
    out<<"  count="<<header->count<<" capacity="<<header->capacity
       <<" pointer-valid="<<(validPtr(header->data)?"yes":"no")<<"\n";
    if(!validPtr(header->data)||header->count<=0||header->count>MaxBones||
       header->capacity<header->count||header->capacity>MaxBones){
        out<<"  array bounds validation failed; no element reads\n";return;
    }
    const int n=std::min(header->count,12);
    // Try candidate element layouts independently. Only the first n elements
    // are read, with strict size caps. No choice is labeled as confirmed.
    for(int variant=0;variant<2;++variant){
        const std::size_t stride=variant==0?48:96;
        std::array<std::uint8_t,12*96> data{};
        const std::size_t bytes=static_cast<std::size_t>(n)*stride;
        if(!ue.memory().read(header->data,data.data(),bytes)){
            out<<"  "<<stride<<"-byte hypothesis: buffer read failed\n";continue;
        }
        int passes=0;
        out<<"  "<<stride<<"-byte FTransform CANDIDATE:\n";
        for(int i=0;i<n;++i){
            if(variant==0){
                TransformShape<float> t{};
                std::memcpy(&t,data.data()+static_cast<std::size_t>(i)*stride,sizeof(t));
                if(plausible(t)){++passes;out<<"    ["<<i<<"]";display(out,t);out<<"\n";}
            }else{
                TransformShape<double> t{};
                std::memcpy(&t,data.data()+static_cast<std::size_t>(i)*stride,sizeof(t));
                if(plausible(t)){++passes;out<<"    ["<<i<<"]";display(out,t);out<<"\n";}
            }
        }
        out<<"  plausibility="<<passes<<"/"<<n<<" (not proof of layout)\n";
    }
}
}
std::filesystem::path boneProbePath(){
    return settingsPath().parent_path()/L"abiotic_bone_transforms.txt";
}
BoneProbeResult inspectBoneTransforms(UE5& ue,Addr actor,const std::string& actorClass){
    BoneProbeResult r{};
    std::ostringstream out;
    out<<"Abiotic ESP v1.1 bounded bone transform probe\n"
       <<"Actor class: "<<actorClass<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIP: invalid hostile UObject\n\n";r.text=out.str();return r;
    }
    const Addr mesh=ue.memory().pointer(actor+MeshOff);
    if(!validPtr(mesh)||ue.memory().pointer(mesh+OwnerOff)!=actor ||
       !ue.saneObject(mesh)||ue.className(ue.classOf(mesh))!="SkeletalMeshComponent"){
        out<<"SKIP: unverified skeletal mesh or owner\n\n";r.text=out.str();return r;
    }
    // Direct offsets were suggested by v1.0 metadata, not yet verified on all NPC classes.
    inspectArray(ue,out,mesh,BoneSpaceOff,"CachedBoneSpaceTransforms");
    inspectArray(ue,out,mesh,ComponentSpaceOff,"CachedComponentSpaceTransforms");
    out<<"NOTE: both 48-byte float and 96-byte double transform layouts are\n"
       <<"speculative. No bone names, parent hierarchy, mesh geometry, or\n"
       <<"world-space silhouette is established by this report.\n"
       <<"No writes, no injection, no persistent memory buffers.\n\n";
    r.success=true;r.text=out.str();return r;
}
bool appendBoneProbe(const BoneProbeResult& r){
    std::ofstream out(boneProbePath(),std::ios::app);
    if(!out)return false;
    out<<r.text;
    return out.good();
}
}
