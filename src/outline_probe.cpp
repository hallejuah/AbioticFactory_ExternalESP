#include "abiotic/outline_probe.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace abiotic {
namespace {
constexpr Addr OuterOffset = 0x20;   // UObject::OuterPrivate, confirmed for this build
constexpr Addr SkeletalField = 0x320; // Found on Pest and Carbuncle only
constexpr Addr OutlineField = 0x9D8; // Found on Pest and Carbuncle only
constexpr size_t ComponentBytes = 0x300;
constexpr size_t MaxObjectClassLookups = 20;

bool namedComponent(UE5& ue,Addr object,Addr actor,const char* expected){
    if(!validPtr(object) || ue.memory().pointer(object+OuterOffset)!=actor || !ue.saneObject(object))
        return false;
    return ue.className(ue.classOf(object))==expected;
}

void inspectOne(UE5& ue,std::ostringstream& out,const char* label,Addr component,
                Addr actor,Addr mesh,Addr outline){
    std::array<std::uint8_t,ComponentBytes> raw{};
    out<<label<<" object 0x"<<std::hex<<component<<std::dec<<"\n";
    if(!ue.memory().read(component,raw.data(),raw.size())){
        out<<"  Read failed; no properties analyzed\n";
        return;
    }
    std::unordered_set<Addr> seen;
    size_t checks=0, matched=0;
    for(size_t offset=0;offset+sizeof(Addr)<=raw.size();offset+=8){
        Addr value{};
        std::memcpy(&value,raw.data()+offset,sizeof(value));
        if(value==mesh || value==outline || value==actor){
            out<<"  +0x"<<std::hex<<offset<<" = 0x"<<value<<std::dec
               <<(value==mesh?" [REF TO SKELETAL MESH]":
                  value==outline?" [REF TO OUTLINE]":" [REF TO ACTOR]")<<"\n";
            ++matched;
            continue;
        }
        if(!validPtr(value) || !seen.insert(value).second || checks>=MaxObjectClassLookups)
            continue;
        ++checks;
        // Only dereference plausible UObject pointers with validated class data.
        if(!ue.saneObject(value))continue;
        auto cls=ue.className(ue.classOf(value));
        if(cls.empty())continue;
        const Addr owner=ue.memory().pointer(value+OuterOffset);
        out<<"  +0x"<<std::hex<<offset<<" = 0x"<<value<<std::dec
           <<" UObject class "<<cls
           <<(owner==actor?" [owned by actor]":"")<<"\n";
        ++matched;
    }
    out<<"  linked UObject/reference slots: "<<matched
       <<" (bounded class checks: "<<checks<<")\n";
}
}
std::filesystem::path outlineProbePath(){
    return settingsPath().parent_path()/L"abiotic_outline_probe.txt";
}
OutlineProbeResult inspectOutlineComponent(UE5& ue,Addr actor,const std::string& actorClass){
    OutlineProbeResult result{};
    std::ostringstream out;
    out<<"Abiotic External ESP outline research v0.7\n"
       <<"Actor class: "<<actorClass<<"\n"
       <<"Actor: 0x"<<std::hex<<actor<<std::dec<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIPPED: actor is not a verified hostile UObject\n\n";
        result.report=out.str();return result;
    }
    const Addr mesh=ue.memory().pointer(actor+SkeletalField);
    const Addr outline=ue.memory().pointer(actor+OutlineField);
    const bool meshOk=namedComponent(ue,mesh,actor,"SkeletalMeshComponent");
    const bool outlineOk=namedComponent(ue,outline,actor,"OutlineComponent_C");
    out<<"SkeletalMeshComponent actor+0x320: "<<(meshOk?"verified":"NOT verified")<<"\n"
       <<"OutlineComponent_C actor+0x9D8: "<<(outlineOk?"verified":"NOT verified")<<"\n";
    if(!meshOk||!outlineOk){
        out<<"Component layout must be validated separately for this actor type.\n\n";
        result.report=out.str();return result;
    }
    result.success=true;
    inspectOne(ue,out,"OutlineComponent_C",outline,actor,mesh,outline);
    inspectOne(ue,out,"SkeletalMeshComponent",mesh,actor,mesh,outline);
    out<<"NOTE: report contains only typed reference candidates in the first 0x300 bytes;\n"
       <<"it does NOT identify Blueprint properties, stencil values, custom depth,\n"
       <<"mesh geometry, bone transforms, visibility masks or exact pixel outlines.\n"
       <<"Game state was not changed.\n\n";
    result.report=out.str();
    return result;
}
bool appendOutlineProbe(const OutlineProbeResult& result){
    std::ofstream output(outlineProbePath(),std::ios::app);
    if(!output)return false;
    output<<result.report;
    return output.good();
}
}
