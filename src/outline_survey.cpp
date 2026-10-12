#include "abiotic/outline_survey.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
namespace abiotic {
namespace {
constexpr Addr Owner=0x20, OutlineMask=0xB0, Components=0xB8, Enabled=0xC8;
constexpr Addr MeshDepth=0x269;
constexpr int MaxActors=80,MaxFields=0x1000,MaxComponents=12,MaxClassLookups=8000;
struct Entry {Addr actor{},outline{},mesh{};std::string actorType;};
bool owns(UE5& ue,Addr parent,Addr obj){
    return validPtr(obj)&&ue.memory().pointer(obj+Owner)==parent&&ue.saneObject(obj);
}
void output(UE5& ue,std::ostringstream& os,const Entry& entry){
    const auto mask=ue.memory().get<std::int32_t>(entry.outline+OutlineMask);
    const auto enabled=ue.memory().get<std::uint8_t>(entry.outline+Enabled);
    const auto components=ue.memory().get<TArray>(entry.outline+Components);
    os<<"  owner="<<entry.actorType
      <<" OutlineMask="<<(mask?std::to_string(*mask):"READ_FAIL")
      <<" enabledByte=";
    if(enabled)os<<"0x"<<std::hex<<unsigned(*enabled)<<std::dec;
    else os<<"READ_FAIL";
    if(components){
        os<<" registeredCount="<<components->count
          <<" registeredCapacity="<<components->capacity;
        if(components->count>0 && components->count<=MaxComponents&&
           components->capacity>=components->count&&components->capacity<=64&&
           validPtr(components->data)){
            std::array<Addr,MaxComponents> data{};
            if(ue.memory().read(components->data,data.data(),static_cast<size_t>(components->count)*8)){
                os<<" registeredTypes=[";
                for(int i=0;i<components->count;++i){
                    if(i)os<<",";
                    const Addr p=data[static_cast<size_t>(i)];
                    if(ue.saneObject(p))os<<ue.className(ue.classOf(p));
                    else os<<"invalid";
                }
                os<<"]";
            }else os<<" REGISTERED_READ_FAILED";
        }
    }else os<<" registeredHeader=READ_FAIL";
    if(entry.mesh){
        const auto depth=ue.memory().get<std::uint8_t>(entry.mesh+MeshDepth);
        if(depth)os<<" meshCustomDepth="<<((*depth&0x40)?"on":"off");
    }
    os<<"\n";
}
}
std::filesystem::path outlineSurveyPath(){
    return settingsPath().parent_path()/L"abiotic_outline_survey.txt";
}
OutlineSurveyReport surveyOutlineActivation(UE5& ue,const std::vector<Actor>& actors){
    OutlineSurveyReport result{};
    std::ostringstream os;
    os<<"Abiotic ESP v1.8 manual bounded OutlineComponent activation survey (read-only)\n";
    os<<"Cached actors="<<actors.size()<<"; scanning at most "<<MaxActors
      <<" known actors, max 0x1000 bytes of direct actor references each\n";
    std::unordered_set<Addr> seen;
    int scanned=0,matched=0,activeCandidates=0,lookups=0;
    for(const auto& actor:actors){
        if(scanned>=MaxActors||lookups>=MaxClassLookups)break;
        if(!validPtr(actor.address)||!seen.insert(actor.address).second)continue;
        if(!ue.saneObject(actor.address))continue;
        ++scanned;
        std::array<Addr,MaxFields/sizeof(Addr)> refs{};
        if(!ue.memory().read(actor.address,refs.data(),sizeof(refs)))continue;
        Addr mesh=0;
        std::array<Addr,8> outlines{};
        int outlinesFound=0;
        for(size_t i=0;i<refs.size()&&lookups<MaxClassLookups;++i){
            const Addr ptr=refs[i];
            if(!validPtr(ptr)||!owns(ue,actor.address,ptr))continue;
            ++lookups;
            const auto cls=ue.className(ue.classOf(ptr));
            if(cls=="SkeletalMeshComponent"&&!mesh)mesh=ptr;
            if(cls=="OutlineComponent_C"&&outlinesFound<static_cast<int>(outlines.size())){
                bool duplicate=false;
                for(int j=0;j<outlinesFound;++j)if(outlines[j]==ptr)duplicate=true;
                if(!duplicate)outlines[static_cast<size_t>(outlinesFound++)]=ptr;
            }
        }
        for(int j=0;j<outlinesFound;++j){
            Entry e{actor.address,outlines[static_cast<size_t>(j)],mesh,actor.name};
            ++matched;
            const auto mask=ue.memory().get<std::int32_t>(e.outline+OutlineMask);
            const auto list=ue.memory().get<TArray>(e.outline+Components);
            if((mask&&*mask!=0)||(list&&list->count>0))++activeCandidates;
            output(ue,os,e);
        }
    }
    os<<"actorsScanned="<<scanned<<" outlineInstances="<<matched
      <<" nonzeroMaskOrRegistered="<<activeCandidates
      <<" validatedOwnedLookups="<<lookups<<"\n";
    os<<"LIMITATION: This is a sampled scan of scanner-known categorized actors,\n"
      <<"NOT the game's entire UObject array. A zero result cannot prove no\n"
      <<"active outlines exist. Values are memory observations, not render-thread\n"
      <<"state. No game-memory writes or render hooks.\n\n";
    result.success=true;result.text=os.str();return result;
}
bool appendOutlineSurvey(const OutlineSurveyReport& report){
    std::ofstream file(outlineSurveyPath(),std::ios::app);
    if(!file)return false;
    file<<report.text;return file.good();
}
}
