#include "abiotic/reflection_probe.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace abiotic {
namespace {
// Class field locations are UNVERIFIED UE5 hypotheses; never mark them confirmed.
constexpr Addr OuterOffset=0x20;
constexpr Addr MeshField=0x320;
constexpr Addr OutlineField=0x9D8;
constexpr std::array<Addr,2> CandidateHeads{0x48,0x50};
constexpr std::array<Addr,3> CandidateNames{0x20,0x28,0x30};
constexpr std::array<Addr,2> CandidateNext{0x20,0x28};
constexpr unsigned MaxClassDepth=8;
constexpr unsigned MaxFieldDepth=12;
constexpr unsigned MaxFieldRead=0x50;
bool classIs(UE5& ue,Addr component,Addr actor,const char* type){
    return validPtr(component)&&ue.memory().pointer(component+OuterOffset)==actor
        &&ue.saneObject(component)&&ue.className(ue.classOf(component))==type;
}
std::string saneName(UE5& ue,Addr obj,Addr offset){
    auto id=ue.memory().get<std::uint32_t>(obj+offset);
    if(!id||*id==0)return {};
    auto name=ue.name(*id);
    if(name.empty()||name.size()>100)return {};
    for(unsigned char c:name)if(c<32||c>126)return {};
    return name;
}
void exploreFields(UE5& ue,std::ostringstream& out,Addr cls,Addr headOff){
    const auto head=ue.memory().pointer(cls+headOff);
    if(!head){out<<"    class+0x"<<std::hex<<headOff<<std::dec<<": no pointer\n";return;}
    out<<"    class+0x"<<std::hex<<headOff<<" = 0x"<<head<<std::dec<<" [UNVERIFIED FField head candidate]\n";
    std::unordered_set<Addr> visited;
    Addr cur=head;
    for(unsigned idx=0;idx<MaxFieldDepth && validPtr(cur)&&visited.insert(cur).second;++idx){
        std::array<std::uint8_t,MaxFieldRead> raw{};
        if(!ue.memory().read(cur,raw.data(),raw.size())){out<<"      field read failed\n";break;}
        out<<"      node "<<idx<<" 0x"<<std::hex<<cur<<std::dec;
        bool any=false;
        for(Addr nameOff:CandidateNames){
            auto n=saneName(ue,cur,nameOff);
            if(!n.empty()){out<<" [name? +0x"<<std::hex<<nameOff<<std::dec<<" = "<<n<<"]";any=true;}
        }
        out<<"\n";
        // Never follow an unvalidated next pointer; an accidental FName match
        // does not prove a node layout. Report next-pointer *candidates* only.
        for(Addr nextOff:CandidateNext){
            Addr nxt{};
            std::memcpy(&nxt,raw.data()+nextOff,sizeof(nxt));
            if(validPtr(nxt)&&nxt!=cur)
                out<<"        +0x"<<std::hex<<nextOff<<" -> 0x"<<nxt<<std::dec<<" [next? unverified]\n";
        }
        if(!any)out<<"        No trustworthy field name decoded; stop\n";
        break;
    }
}
void inspectClass(UE5& ue,std::ostringstream& out,const char* title,Addr component){
    Addr cls=ue.classOf(component);
    std::unordered_set<Addr> visited;
    out<<title<<" reflection candidates:\n";
    for(unsigned depth=0;depth<MaxClassDepth&&validPtr(cls)&&visited.insert(cls).second;++depth){
        const auto name=ue.className(cls);
        out<<"  class["<<depth<<"] "<<(name.empty()?"?":name)<<" at 0x"
           <<std::hex<<cls<<std::dec<<"\n";
        for(auto off:CandidateHeads)exploreFields(ue,out,cls,off);
        cls=ue.memory().pointer(cls+offsets::Super);
    }
}
}
std::filesystem::path reflectionReportPath(){
    return settingsPath().parent_path()/L"abiotic_reflection_probe.txt";
}
ReflectionProbeResult inspectReflection(UE5& ue,Addr actor,const std::string& actorClass){
    ReflectionProbeResult result{};
    std::ostringstream out;
    out<<"Abiotic External ESP reflection research v0.8\nActor class: "<<actorClass
       <<"\nActor address: 0x"<<std::hex<<actor<<std::dec<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIPPED: actor invalid or unsupported\n\n";
        result.report=out.str();return result;
    }
    const auto mesh=ue.memory().pointer(actor+MeshField);
    const auto outline=ue.memory().pointer(actor+OutlineField);
    const bool meshOk=classIs(ue,mesh,actor,"SkeletalMeshComponent");
    const bool outlineOk=classIs(ue,outline,actor,"OutlineComponent_C");
    out<<"Mesh actor+0x320: "<<(meshOk?"verified":"NOT verified")<<"\n"
       <<"Outline actor+0x9D8: "<<(outlineOk?"verified":"NOT verified")<<"\n";
    if(!meshOk||!outlineOk){
        out<<"No reflection attempts performed on unverified components.\n\n";
        result.report=out.str();return result;
    }
    inspectClass(ue,out,"OutlineComponent_C",outline);
    inspectClass(ue,out,"SkeletalMeshComponent",mesh);
    out<<"NOTE: class+0x48/+0x50 and candidate FField name/next offsets are\n"
       <<"unverified exploratory hypotheses, not decoded Blueprint properties.\n"
       <<"No mesh vertices, bones, stencil state, or exact silhouette is inferred.\n"
       <<"This diagnostic does not modify the game process.\n\n";
    result.success=true;result.report=out.str();return result;
}
bool appendReflectionReport(const ReflectionProbeResult& result){
    std::ofstream file(reflectionReportPath(),std::ios::app);
    if(!file)return false;
    file<<result.report;
    return file.good();
}
}
