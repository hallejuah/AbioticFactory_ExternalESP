#include "abiotic/property_chain.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>
namespace abiotic {
namespace {
constexpr Addr OwnerOff=0x20, MeshOff=0x320, OutlineOff=0x9D8;
constexpr Addr SuperOff=offsets::Super;
constexpr Addr ClassChildPropertiesCandidate=0x50;
constexpr Addr FieldNameCandidate=0x20;
constexpr Addr FieldNextCandidate=0x18;
constexpr unsigned MaxClasses=7, MaxFields=24;
constexpr int MaxPropertyOffset=0x10000;
// Hypotheses inferred from the v0.8 report; NOT verified UE reflection layout.
bool verifiedComponent(UE5& ue,Addr actor,Addr component,const char* name){
    return validPtr(component)&&ue.memory().pointer(component+OwnerOff)==actor
       &&ue.saneObject(component)&&ue.className(ue.classOf(component))==name;
}
std::string fieldName(UE5& ue,Addr field){
    const auto index=ue.memory().get<std::uint32_t>(field+FieldNameCandidate);
    if(!index||!*index)return {};
    const std::string name=ue.name(*index);
    if(name.empty()||name.size()>96)return {};
    for(unsigned char ch:name){
        if(ch<32||ch>126)return {};
    }
    return name;
}
void walk(UE5& ue,std::ostringstream& out,Addr classPtr){
    auto field=ue.memory().pointer(classPtr+ClassChildPropertiesCandidate);
    if(!validPtr(field)){out<<"    no plausible child-properties pointer\n";return;}
    std::unordered_set<Addr> visited;
    unsigned printed=0;
    for(unsigned n=0;n<MaxFields && validPtr(field)&&visited.insert(field).second;++n){
        // A successfully decoded FName on multiple nodes strengthens, but does
        // not prove, the guessed FField layout.
        const auto name=fieldName(ue,field);
        if(name.empty()){
            out<<"    STOP: invalid FName at candidate field 0x"<<std::hex<<field<<std::dec<<"\n";
            break;
        }
        out<<"    ["<<n<<"] "<<name<<" @0x"<<std::hex<<field<<std::dec;
        // Offset_Internal is often stored within FProperty, but layout unknown:
        // display bounded candidate ints without treating any as authoritative.
        for(Addr off: {Addr(0x44),Addr(0x48),Addr(0x4C),Addr(0x50)}){
            const auto value=ue.memory().get<std::int32_t>(field+off);
            if(value&&*value>=0&&*value<MaxPropertyOffset)
                out<<" candidate[+0x"<<std::hex<<off<<"]="<<std::dec<<*value;
        }
        out<<"\n";
        ++printed;
        const Addr next=ue.memory().pointer(field+FieldNextCandidate);
        if(!validPtr(next)||next==field){
            out<<"    END: no plausible next pointer\n";
            break;
        }
        // Validate next's FName before following the guessed chain.
        if(fieldName(ue,next).empty()){
            out<<"    STOP: next pointer lacks a decodable name (0x"<<std::hex<<next<<std::dec<<")\n";
            break;
        }
        field=next;
    }
    out<<"    candidate names traversed: "<<printed<<" (limit "<<MaxFields<<")\n";
}
void inspect(UE5& ue,std::ostringstream& out,const char* title,Addr object){
    out<<title<<" property candidates:\n";
    Addr cls=ue.classOf(object);
    std::unordered_set<Addr> visited;
    for(unsigned i=0;i<MaxClasses && validPtr(cls)&&visited.insert(cls).second;++i){
        auto name=ue.className(cls);
        if(name.empty()){out<<"  class name invalid; stopping\n";break;}
        out<<"  "<<name<<" [candidate class+0x50]\n";
        walk(ue,out,cls);
        cls=ue.memory().pointer(cls+SuperOff);
    }
}
}
std::filesystem::path propertyChainPath(){
    return settingsPath().parent_path()/L"abiotic_property_chain.txt";
}
PropertyChainReport probePropertyChains(UE5& ue,Addr actor,const std::string& actorClass){
    PropertyChainReport result{};
    std::ostringstream out;
    out<<"Abiotic ESP v0.9 candidate FProperty chain probe\nActor class: "<<actorClass<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIP: not a verified hostile actor\n\n";result.text=out.str();return result;
    }
    const auto mesh=ue.memory().pointer(actor+MeshOff);
    const auto outline=ue.memory().pointer(actor+OutlineOff);
    if(!verifiedComponent(ue,actor,mesh,"SkeletalMeshComponent")||
       !verifiedComponent(ue,actor,outline,"OutlineComponent_C")){
        out<<"SKIP: mesh and outline classes / owners do not validate\n\n";
        result.text=out.str();return result;
    }
    inspect(ue,out,"OutlineComponent_C",outline);
    inspect(ue,out,"SkeletalMeshComponent",mesh);
    out<<"CAUTION: class+0x50, field+0x18 (Next), +0x20 (FName), and\n"
       <<"candidate integer offsets are hypotheses. Traversed names are NOT\n"
       <<"validated reflection property types or instance field offsets.\n"
       <<"No game memory was modified and no silhouette was drawn.\n\n";
    result.success=true;result.text=out.str();return result;
}
bool appendPropertyChain(const PropertyChainReport& report){
    std::ofstream out(propertyChainPath(),std::ios::app);
    if(!out)return false;
    out<<report.text;
    return out.good();
}
}
