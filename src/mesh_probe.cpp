#include "abiotic/mesh_probe.hpp"
#include "abiotic/settings.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
namespace abiotic {
namespace {
constexpr Addr OuterOffset=0x20; // UObject::OuterPrivate, verified in this build.
constexpr Addr SuperOffset=offsets::Super;
// Inspect only the beginning of a known, validated enemy actor; do not
// blindly iterate process memory. Pointers MUST resolve to actor-owned UObjects.
constexpr size_t ProbeBytes=0x1000;
constexpr size_t MaxOwned=48;
bool meshInheritance(UE5& ue,Addr cls,std::string& chain) {
    std::unordered_set<Addr> seen;
    for(int depth=0;depth<16 && validPtr(cls)&&seen.insert(cls).second;++depth){
        const auto name=ue.className(cls);
        if(name.empty())break;
        if(!chain.empty())chain+=" <- ";
        chain+=name;
        if(name=="SkeletalMeshComponent"||name=="SkinnedMeshComponent"||
           name=="MeshComponent"||name=="StaticMeshComponent")return true;
        cls=ue.memory().pointer(cls+SuperOffset);
    }
    return false;
}
}
std::filesystem::path meshProbeReportPath(){
    return settingsPath().parent_path()/L"abiotic_mesh_probe.txt";
}
ComponentProbeResult probeActorComponents(UE5& ue,Addr actor,const std::string& actorClass){
    ComponentProbeResult result{};
    std::ostringstream out;
    out<<"Abiotic External ESP mesh probe v0.6\n";
    out<<"Actor class: "<<actorClass<<"\n";
    out<<"Actor address: 0x"<<std::hex<<actor<<std::dec<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"ERROR: not a validated hostile UObject; cancelled\n";
        result.report=out.str();return result;
    }
    std::array<std::uint8_t,ProbeBytes> bytes{};
    if(!ue.memory().read(actor,bytes.data(),bytes.size())){
        out<<"ERROR: failed bounded actor read\n";
        result.report=out.str();return result;
    }
    std::unordered_set<Addr> seen;
    for(size_t off=0;off+sizeof(Addr)<=bytes.size();off+=sizeof(Addr)){
        Addr candidate{};
        std::memcpy(&candidate,bytes.data()+off,sizeof(candidate));
        if(!validPtr(candidate)||candidate==actor||!seen.insert(candidate).second)continue;
        ++result.fieldsChecked;
        // Must be a valid UObject directly owned by the selected actor.
        if(ue.memory().pointer(candidate+OuterOffset)!=actor)continue;
        if(!ue.saneObject(candidate))continue;
        ++result.ownedObjects;
        auto cls=ue.classOf(candidate);
        std::string inheritance;
        const bool mesh=meshInheritance(ue,cls,inheritance);
        if(mesh)++result.meshCandidates;
        out<<"  actor+0x"<<std::hex<<off<<" => object 0x"<<candidate<<std::dec
           <<" class: "<<(inheritance.empty()?"?":inheritance)
           <<(mesh?" [MESH CLASS CANDIDATE]":"")<<"\n";
        if(result.ownedObjects>=MaxOwned){
            out<<"Stopped: owned object limit reached\n";break;
        }
    }
    out<<"Pointer candidates validated: "<<result.fieldsChecked<<"\n";
    out<<"Direct actor-owned objects: "<<result.ownedObjects<<"\n";
    out<<"Mesh-class candidates: "<<result.meshCandidates<<"\n";
    out<<"NOTE: This is a heuristic field scan, NOT a complete component enumeration.\n";
    out<<"No skeletal transforms, actual mesh silhouette, or occlusion information was read.\n\n";
    result.success=true;
    result.report=out.str();return result;
}
bool appendProbeReport(const ComponentProbeResult& result){
    std::ofstream out(meshProbeReportPath(),std::ios::app);
    if(!out)return false;
    out<<result.report;
    return out.good();
}
}
