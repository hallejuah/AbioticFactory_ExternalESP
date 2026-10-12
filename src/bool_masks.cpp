#include "abiotic/bool_masks.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace abiotic {
namespace {
constexpr Addr OwnerOff=0x20, MeshOff=0x320, OutlineOff=0x9D8;
constexpr Addr PropertyChain=0x50, FieldNext=0x18, FieldName=0x20;
constexpr Addr FieldClass=0x08, OffsetInternal=0x44;
constexpr Addr MetadataStart=0x60;
constexpr int MaxPropertyOffset=0x4000;
constexpr unsigned MaxClasses=8, MaxFields=512;
std::string nameAt(UE5& ue,Addr object,Addr displacement){
    auto id=ue.memory().get<std::uint32_t>(object+displacement);
    if(!id||!*id)return {};
    auto n=ue.name(*id);
    if(n.empty()||n.size()>96)return {};
    for(unsigned char c:n)if(c<32||c>126)return {};
    return n;
}
bool knownComponent(UE5& ue,Addr actor,Addr component,const char* expected){
    return validPtr(component)&&ue.saneObject(component)&&
        ue.memory().pointer(component+OwnerOff)==actor&&
        ue.className(ue.classOf(component))==expected;
}
bool target(const std::string& n){
    constexpr std::array<const char*,11> names{
        "ComponentEnabled","bRenderCustomDepth","bRenderInMainPass",
        "bRenderInDepthPass","bOwnerNoSee","bOnlyOwnerSee",
        "bVisible","bHiddenInGame","bCastDynamicShadow",
        "bReceivesDecals","bUseAsOccluder"
    };
    for(const auto* candidate:names)if(n==candidate)return true;
    return false;
}
void inspect(UE5& ue,std::ostringstream& out,Addr instance,const char* label){
    out<<label<<" bool field probes:\n";
    Addr cls=ue.classOf(instance);
    unsigned hits=0;
    std::unordered_set<Addr> classes;
    for(unsigned depth=0;depth<MaxClasses&&validPtr(cls)&&classes.insert(cls).second;++depth){
        const auto ownerClass=ue.className(cls);
        if(ownerClass.empty())break;
        Addr node=ue.memory().pointer(cls+PropertyChain);
        std::unordered_set<Addr> visited;
        for(unsigned i=0;i<MaxFields&&validPtr(node)&&visited.insert(node).second;++i){
            const auto name=nameAt(ue,node,FieldName);
            if(name.empty())break;
            if(target(name)){
                ++hits;
                const auto classPtr=ue.memory().pointer(node+FieldClass);
                const auto propertyType=validPtr(classPtr)?nameAt(ue,classPtr,0):std::string{};
                const auto offset=ue.memory().get<std::int32_t>(node+OffsetInternal);
                out<<"  "<<ownerClass<<"::"<<name<<" type="
                   <<(propertyType.empty()?"unknown":propertyType);
                if(propertyType!="BoolProperty"){
                    out<<" [not confirmed BoolProperty; skip metadata]\n";
                }else if(!offset||*offset<0||*offset>=MaxPropertyOffset){
                    out<<" [rejected Offset_Internal candidate]\n";
                }else{
                    const Addr byteAddr=instance+static_cast<Addr>(*offset);
                    const auto rawByte=ue.memory().get<std::uint8_t>(byteAddr);
                    out<<" instance+0x"<<std::hex<<*offset<<std::dec;
                    if(rawByte)out<<" rawByte=0x"<<std::hex<<std::setw(2)<<std::setfill('0')
                                  <<unsigned(*rawByte)<<std::dec<<std::setfill(' ');
                    else out<<" rawByte=READ_FAILED";
                    // Byte layout of FBoolProperty is still unverified in this build.
                    // Dump the raw 48-byte region with offsets; do NOT infer a mask
                    // from memory alone or present any candidate as writable.
                    std::array<std::uint8_t,48> bytes{};
                    if(ue.memory().read(node+MetadataStart,bytes.data(),bytes.size())){
                        out<<"\n    FBoolProperty metadata RAW candidate +0x60..+0x8f:";
                        for(size_t j=0;j<bytes.size();++j){
                            if(j%16==0)out<<"\n      +0x"<<std::hex
                                <<std::setw(2)<<std::setfill('0')<<(MetadataStart+j)
                                <<std::dec<<std::setfill(' ')<<": ";
                            out<<std::hex<<std::setw(2)<<std::setfill('0')
                               <<unsigned(bytes[j])<<' ';
                        }
                        out<<std::dec<<std::setfill(' ')<<"\n";
                        for(size_t candidate:{size_t(0x70),size_t(0x78)}){
                            const size_t idx=candidate-MetadataStart;
                            const unsigned fieldSize=bytes[idx], byteOffset=bytes[idx+1];
                            const unsigned byteMask=bytes[idx+2], fieldMask=bytes[idx+3];
                            const bool candidatePlausible=
                                (fieldSize==1||fieldSize==2||fieldSize==4||fieldSize==8)&&
                                byteOffset<fieldSize&&byteMask!=0&&fieldMask!=0;
                            out<<"    layout hypothesis at +0x"<<std::hex<<candidate
                               <<std::dec<<": fieldSize="<<fieldSize
                               <<" byteOffset="<<byteOffset<<" byteMask=0x"
                               <<std::hex<<byteMask<<" fieldMask=0x"<<fieldMask
                               <<std::dec<<" plausible="<<(candidatePlausible?"yes":"no")
                               <<" [NEVER use for writes]\n";
                        }
                    }else out<<"\n    metadata read FAILED\n";
                }
            }
            const Addr next=ue.memory().pointer(node+FieldNext);
            if(!validPtr(next)||next==node||nameAt(ue,next,FieldName).empty())break;
            node=next;
        }
        cls=ue.memory().pointer(cls+offsets::Super);
    }
    out<<"  matched target names="<<hits<<"\n";
}
}
std::filesystem::path boolMaskReportPath(){
    return settingsPath().parent_path()/L"abiotic_bool_masks.txt";
}
BoolMaskReport inspectBoolMasks(UE5& ue,Addr actor,const std::string& className){
    BoolMaskReport result{};
    std::ostringstream out;
    out<<"Abiotic ESP v1.5 FBoolProperty metadata probe — READ ONLY\n"
       <<"Actor class: "<<className<<"\n";
    if(!ue.saneObject(actor)||categorize(className)!=Category::Hostile){
        out<<"SKIP: invalid or non-hostile actor\n\n";
        result.text=out.str();return result;
    }
    const Addr mesh=ue.memory().pointer(actor+MeshOff);
    const Addr outline=ue.memory().pointer(actor+OutlineOff);
    if(!knownComponent(ue,actor,mesh,"SkeletalMeshComponent")||
       !knownComponent(ue,actor,outline,"OutlineComponent_C")){
        out<<"SKIP: component pointer/type/Outer verification failed\n\n";
        result.text=out.str();return result;
    }
    inspect(ue,out,outline,"OutlineComponent_C");
    inspect(ue,out,mesh,"SkeletalMeshComponent");
    out<<"No field layout, bit mask or write address is confirmed solely by this report.\n"
       <<"No Process VM_WRITE, no writes, no injection, no hooks.\n\n";
    result.success=true;result.text=out.str();return result;
}
bool appendBoolMaskReport(const BoolMaskReport& r){
    std::ofstream out(boolMaskReportPath(),std::ios::app);
    if(!out)return false;
    out<<r.text;
    return out.good();
}
}
