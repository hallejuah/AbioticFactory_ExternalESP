#include "abiotic/property_values.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace abiotic {
namespace {
constexpr Addr Owner=0x20, Mesh=0x320, Outline=0x9D8;
constexpr Addr Children=0x50, Next=0x18, FieldName=0x20;
constexpr Addr FieldClass=0x08, OffsetCandidate=0x44;
constexpr unsigned MaxFieldNodes=64, MaxClassNodes=7;
constexpr std::uint32_t MaxInstanceOffset=0x4000;
bool component(UE5& ue,Addr actor,Addr obj,const char* expected){
    return validPtr(obj) && ue.memory().pointer(obj+Owner)==actor &&
           ue.saneObject(obj) && ue.className(ue.classOf(obj))==expected;
}
std::string nameAt(UE5& ue,Addr object,Addr offset){
    auto id=ue.memory().get<std::uint32_t>(object+offset);
    if(!id || !*id)return {};
    std::string name=ue.name(*id);
    if(name.empty() || name.size()>90)return {};
    for(unsigned char c:name)if(c<32 || c>126)return {};
    return name;
}
std::string typeCandidate(UE5& ue,Addr field){
    // +0x08 is the probable FFieldClass pointer; its FName position is not
    // confirmed for this game build. Never treat guessed names as verified.
    const auto cls=ue.memory().pointer(field+FieldClass);
    if(!cls)return "unknown";
    for(Addr off : {Addr(0x00),Addr(0x08),Addr(0x10),Addr(0x18)}){
        std::string n=nameAt(ue,cls,off);
        if(n.ends_with("Property")){
            std::ostringstream out;
            out<<n<<" [FFieldClass name candidate +0x"<<std::hex<<off<<std::dec<<"]";
            return out.str();
        }
    }
    return "unknown";
}
void dumpBytes(UE5& ue,std::ostringstream& out,Addr address){
    std::array<std::uint8_t,16> bytes{};
    if(!ue.memory().read(address,bytes.data(),bytes.size())){
        out<<"read FAILED\n";return;
    }
    out<<"raw16=";
    for(auto value:bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(value)<<" ";
    out<<std::dec<<std::setfill(' ');
    // Report an ARRAY-HEADER HYPOTHESIS only for explicitly targeted
    // transform / component collections; pointer & counts are not trusted.
    Addr data{};std::int32_t count{},capacity{};
    std::memcpy(&data,bytes.data(),8);
    std::memcpy(&count,bytes.data()+8,4);
    std::memcpy(&capacity,bytes.data()+12,4);
    out<<" arrayHeaderCandidate=("<<(validPtr(data)?"plausiblePtr":"noValidatedPtr")
       <<", count="<<count<<", capacity="<<capacity<<")\n";
}
void scan(UE5& ue,std::ostringstream& out,Addr instance,const char* className,
          const std::array<const char*,4>& sought){
    out<<className<<" targeted properties:\n";
    Addr cls=ue.classOf(instance);
    std::unordered_set<Addr> visitedClasses;
    unsigned found=0;
    for(unsigned depth=0;depth<MaxClassNodes && validPtr(cls) &&
        visitedClasses.insert(cls).second;++depth){
        const auto classLabel=ue.className(cls);
        if(classLabel.empty())break;
        Addr node=ue.memory().pointer(cls+Children);
        std::unordered_set<Addr> visitedFields;
        for(unsigned i=0;i<MaxFieldNodes && validPtr(node) &&
            visitedFields.insert(node).second;++i){
            const auto field=nameAt(ue,node,FieldName);
            if(field.empty())break;
            bool selected=false;
            for(const char* item:sought)if(field==item)selected=true;
            if(selected){
                const auto off=ue.memory().get<std::int32_t>(node+OffsetCandidate);
                out<<"  "<<field<<" / owner class "<<classLabel
                   <<" / type "<<typeCandidate(ue,node);
                if(!off || *off<0 || *off>=static_cast<int>(MaxInstanceOffset)){
                    out<<" / rejected offset candidate\n";
                }else{
                    out<<" / instance+0x"<<std::hex<<*off<<std::dec<<" [offset CANDIDATE]\n    ";
                    dumpBytes(ue,out,instance+static_cast<Addr>(*off));
                }
                ++found;
            }
            const Addr next=ue.memory().pointer(node+Next);
            if(!validPtr(next) || next==node || nameAt(ue,next,FieldName).empty())break;
            node=next;
        }
        cls=ue.memory().pointer(cls+offsets::Super);
    }
    out<<"Matched targeted names: "<<found<<"\n";
}
}
std::filesystem::path propertyValuesReportPath(){
    return settingsPath().parent_path()/L"abiotic_property_values.txt";
}
PropertyValuesResult inspectPropertyValues(UE5& ue,Addr actor,const std::string& actorClass){
    PropertyValuesResult result{};
    std::ostringstream out;
    out<<"Abiotic ESP v1.0 read-only property values research\n"
       <<"Actor class: "<<actorClass<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIP: invalid or unsupported actor\n\n";result.report=out.str();return result;
    }
    const Addr mesh=ue.memory().pointer(actor+Mesh);
    const Addr outline=ue.memory().pointer(actor+Outline);
    if(!component(ue,actor,mesh,"SkeletalMeshComponent") ||
       !component(ue,actor,outline,"OutlineComponent_C")){
        out<<"SKIP: component classes and owner do not validate\n\n";
        result.report=out.str();return result;
    }
    scan(ue,out,outline,"OutlineComponent_C",
         {"OutlineMask","RegisteredComponents","ComponentEnabled",""});
    scan(ue,out,mesh,"SkeletalMeshComponent",
         {"CachedBoneSpaceTransforms","CachedComponentSpaceTransforms","AnimScriptInstance",""});
    out<<"WARNING: instance offsets (+0x44) and FFieldClass interpretations are\n"
       <<"unverified candidates. Raw bytes must NOT be interpreted as confirmed\n"
       <<"property values or TArray headers without additional validation.\n"
       <<"No array buffers or pose transforms were dereferenced. No writes.\n\n";
    result.success=true;result.report=out.str();return result;
}
bool appendPropertyValuesReport(const PropertyValuesResult& r){
    std::ofstream out(propertyValuesReportPath(),std::ios::app);
    if(!out)return false;
    out<<r.report;
    return out.good();
}
}
