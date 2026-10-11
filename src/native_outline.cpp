#include "abiotic/native_outline.hpp"
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
constexpr Addr OwnerOff=0x20, MeshOff=0x320, OutlineOff=0x9D8;
constexpr Addr ChildPropertiesCandidate=0x50, NextCandidate=0x18;
constexpr Addr NameCandidate=0x20, FieldClassCandidate=0x08, OffsetCandidate=0x44;
constexpr Addr BoolByteOffsetCandidate=0x70, BoolByteMaskCandidate=0x71;
constexpr unsigned MaxClasses=8, MaxFieldsPerClass=512;
constexpr int MaxOffset=0x4000;
std::string named(UE5& ue,Addr addr,Addr offset){
    const auto id=ue.memory().get<std::uint32_t>(addr+offset);
    if(!id||!*id)return {};
    const auto name=ue.name(*id);
    if(name.empty()||name.size()>100)return {};
    for(unsigned char c:name)if(c<32||c>126)return {};
    return name;
}
bool directComponent(UE5& ue,Addr actor,Addr obj,const char* className){
    return validPtr(obj)&&ue.memory().pointer(obj+OwnerOff)==actor&&
        ue.saneObject(obj)&&ue.className(ue.classOf(obj))==className;
}
std::string candidateType(UE5& ue,Addr property){
    const Addr fieldClass=ue.memory().pointer(property+FieldClassCandidate);
    if(!fieldClass)return {};
    const auto name=named(ue,fieldClass,0);
    if(name.ends_with("Property"))return name;
    return {};
}
bool selected(const std::string& name){
    constexpr std::array<const char*,14> targets{
      "OutlineMask","RegisteredComponents","ComponentEnabled",
      "bRenderCustomDepth","CustomDepthStencilValue","CustomDepthStencilWriteMask",
      "CustomDepthStencilWriteMask_DEPRECATED",
      "bRenderInMainPass","bRenderInDepthPass","OverlayMaterial",
      "OverrideMaterials","bVisible","bHiddenInGame","bOwnerNoSee"
    };
    for(const auto* target:targets)if(name==target)return true;
    return false;
}
void properties(UE5& ue,std::ostringstream& out,Addr instance,const char* label){
    out<<label<<" reflected-property candidates:\n";
    Addr cls=ue.classOf(instance);
    std::unordered_set<Addr> seenClasses;
    unsigned found=0;
    for(unsigned depth=0;depth<MaxClasses&&validPtr(cls)&&seenClasses.insert(cls).second;++depth){
        const auto classLabel=ue.className(cls);
        if(classLabel.empty())break;
        out<<"  class "<<classLabel<<"\n";
        Addr field=ue.memory().pointer(cls+ChildPropertiesCandidate);
        std::unordered_set<Addr> seenFields;
        unsigned scanned=0;
        while(validPtr(field)&&scanned<MaxFieldsPerClass&&seenFields.insert(field).second){
            const auto name=named(ue,field,NameCandidate);
            if(name.empty()){out<<"    STOP: undecodable field name\n";break;}
            ++scanned;
            if(selected(name)){
                ++found;
                const auto typ=candidateType(ue,field);
                const auto off=ue.memory().get<std::int32_t>(field+OffsetCandidate);
                out<<"    "<<name<<" typeCandidate="<<(typ.empty()?"unknown":typ);
                if(!off||*off<0||*off>=MaxOffset){
                    out<<" offset rejected\n";
                }else{
                    out<<" instance+0x"<<std::hex<<*off<<std::dec;
                    std::array<std::uint8_t,16> bytes{};
                    if(ue.memory().read(instance+static_cast<Addr>(*off),bytes.data(),bytes.size())){
                        out<<" raw16=";
                        for(auto b:bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b)<<" ";
                        out<<std::dec<<std::setfill(' ');
                        if(typ=="ArrayProperty"){
                            Addr data{};std::int32_t count{},capacity{};
                            std::memcpy(&data,bytes.data(),8);
                            std::memcpy(&count,bytes.data()+8,4);
                            std::memcpy(&capacity,bytes.data()+12,4);
                            out<<" headerCandidate=("<<(validPtr(data)?"ptr-valid":"ptr-invalid")
                               <<", count="<<count<<", cap="<<capacity<<")";
                        }
                        if(typ=="BoolProperty"){
                            const auto byteOff=ue.memory().get<std::uint8_t>(field+BoolByteOffsetCandidate);
                            const auto byteMask=ue.memory().get<std::uint8_t>(field+BoolByteMaskCandidate);
                            if(byteOff&&byteMask&&*byteOff<16&&*byteMask){
                                const auto value=bytes[*byteOff];
                                out<<" boolCandidate(offset="<<unsigned(*byteOff)<<",mask=0x"
                                   <<std::hex<<unsigned(*byteMask)<<std::dec<<",set="
                                   <<((value&*byteMask)?"yes":"no")<<")";
                            }else out<<" boolLayout-unverified";
                        }
                    }else out<<" read-failed";
                    out<<" [metadata/layout CANDIDATES]\n";
                }
            }
            const Addr next=ue.memory().pointer(field+NextCandidate);
            if(!validPtr(next)||next==field||named(ue,next,NameCandidate).empty())break;
            field=next;
        }
        out<<"    fields-scanned="<<scanned<<"\n";
        cls=ue.memory().pointer(cls+offsets::Super);
    }
    out<<"  matching properties="<<found<<"\n";
}
}
std::filesystem::path nativeOutlineReportPath(){
    return settingsPath().parent_path()/L"abiotic_native_outline.txt";
}
NativeOutlineReport inspectNativeOutline(UE5& ue,Addr actor,const std::string& actorClass){
    NativeOutlineReport result{};
    std::ostringstream out;
    out<<"Abiotic ESP v1.4 native outline research (READ ONLY)\n";
    out<<"Actor class: "<<actorClass<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIP: invalid or non-hostile actor\n\n";
        result.text=out.str();return result;
    }
    const Addr mesh=ue.memory().pointer(actor+MeshOff);
    const Addr outline=ue.memory().pointer(actor+OutlineOff);
    const bool meshOk=directComponent(ue,actor,mesh,"SkeletalMeshComponent");
    const bool outlineOk=directComponent(ue,actor,outline,"OutlineComponent_C");
    out<<"Mesh validation: "<<(meshOk?"OK":"FAIL")<<"\n"
       <<"Outline validation: "<<(outlineOk?"OK":"FAIL")<<"\n";
    if(!meshOk||!outlineOk){
        out<<"SKIP: no speculative field reads on unverified components\n\n";
        result.text=out.str();return result;
    }
    properties(ue,out,outline,"OutlineComponent_C");
    properties(ue,out,mesh,"SkeletalMeshComponent");
    out<<"NOTICE: offsets from UE reflection metadata and bool masks are candidates.\n"
       <<"CustomDepth or stencil property presence alone does not prove the game's\n"
       <<"OutlineComponent_C drives a working native highlight effect.\n"
       <<"No process writes, material changes, injections or render hooks.\n\n";
    result.success=true;result.text=out.str();return result;
}
bool appendNativeOutlineReport(const NativeOutlineReport& report){
    std::ofstream out(nativeOutlineReportPath(),std::ios::app);
    if(!out)return false;
    out<<report.text;
    return out.good();
}
}
