#include "abiotic/outline_survey.hpp"
#include "abiotic/settings.hpp"
#include <algorithm>
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
constexpr std::size_t PageSize=80, MaxActorBytes=0x1000, MaxComponents=12;
constexpr unsigned MaxOwnedLookups=8000;
std::uint64_t signature(const std::vector<Actor>& actors){
    // Order-sensitive hash: a re-ordered scanner list resets pagination.
    std::uint64_t hash=14695981039346656037ULL;
    for(const auto& actor:actors){
        hash^=actor.address;
        hash*=1099511628211ULL;
    }
    hash^=static_cast<std::uint64_t>(actors.size());
    return hash;
}
bool owns(UE5& ue,Addr actor,Addr object){
    return validPtr(object)&&ue.saneObject(object)&&
           ue.memory().pointer(object+Owner)==actor;
}
struct Entry {Addr actor{},outline{},mesh{};std::string type;};
void sample(UE5& ue,std::ostringstream& os,const Entry& e,
            OutlineSurveyState& state,unsigned& unusual,unsigned& changed){
    const auto mask=ue.memory().get<std::int32_t>(e.outline+OutlineMask);
    const auto enabled=ue.memory().get<std::uint8_t>(e.outline+Enabled);
    const auto list=ue.memory().get<TArray>(e.outline+Components);
    if(!mask||!enabled||!list){
        os<<"  owner="<<e.type<<" state=READ_FAILED\n";
        return;
    }
    if(list->count<0||list->count>4096||list->capacity<list->count||
       list->capacity>4096){
        os<<"  owner="<<e.type<<" registeredHeader=INVALID\n";return;
    }
    const auto depth=e.mesh?ue.memory().get<std::uint8_t>(e.mesh+MeshDepth):
                            std::optional<std::uint8_t>{};
    const OutlineObservation observation{
        e.outline,e.type,*mask,list->count,*enabled,
        depth?((*depth&0x40)!=0):false,depth.has_value()
    };
    auto previous=state.observations.find(e.actor);
    const bool different=previous!=state.observations.end()&&(
        previous->second.outline!=observation.outline||
        previous->second.actorType!=observation.actorType||
        previous->second.mask!=observation.mask||
        previous->second.registeredCount!=observation.registeredCount||
        previous->second.enabledByte!=observation.enabledByte||
        (previous->second.depthKnown&&observation.depthKnown&&
         previous->second.depthOn!=observation.depthOn));
    if(different){
        ++changed;
        os<<"  CHANGE "<<e.type
          <<" mask "<<previous->second.mask<<"->"<<observation.mask
          <<" registered "<<previous->second.registeredCount<<"->"<<observation.registeredCount
          <<" enabled 0x"<<std::hex<<unsigned(previous->second.enabledByte)
          <<"->0x"<<unsigned(observation.enabledByte)<<std::dec<<"\n";
    }
    if(*mask!=0||list->count>0){
        ++unusual;
        os<<"  CANDIDATE ";
    }else os<<"  ";
    os<<"owner="<<e.type<<" OutlineMask="<<*mask
      <<" enabledByte=0x"<<std::hex<<unsigned(*enabled)<<std::dec
      <<" registeredCount="<<list->count<<" registeredCapacity="<<list->capacity;
    if(depth)os<<" meshCustomDepth="<<((*depth&0x40)?"on":"off");
    if(list->count>0&&list->count<=static_cast<int>(MaxComponents)&&
       validPtr(list->data)){
        std::array<Addr,MaxComponents> refs{};
        if(ue.memory().read(list->data,refs.data(),
             static_cast<std::size_t>(list->count)*sizeof(Addr))){
            os<<" registeredTypes=[";
            for(int i=0;i<list->count;++i){
                if(i)os<<",";
                const auto item=refs[static_cast<std::size_t>(i)];
                os<<(ue.saneObject(item)?ue.className(ue.classOf(item)):"invalid");
            }
            os<<"]";
        }else os<<" registeredTypes=READ_FAILED";
    }
    os<<"\n";
    // Limit history to scanner-sized bounded working set.
    if(state.observations.size()<5000||previous!=state.observations.end())
        state.observations[e.actor]=observation;
}
}
std::filesystem::path outlineSurveyPath(){
    return settingsPath().parent_path()/L"abiotic_outline_survey.txt";
}
OutlineSurveyReport surveyOutlineActivation(UE5& ue,
    const Snapshot& snapshot,OutlineSurveyState& state){
    OutlineSurveyReport result{};
    std::ostringstream os;
    const auto& actors=snapshot.actors;
    os<<"Abiotic ESP v1.9 paginated outline activation survey (READ ONLY)\n";
    if(!validPtr(snapshot.world)||actors.empty()){
        os<<"No valid world or actors to scan\n\n";
        result.text=os.str();return result;
    }
    const auto sig=signature(actors);
    if(state.world!=snapshot.world){
        state=OutlineSurveyState{};
        state.world=snapshot.world;
        os<<"RESET: world identity changed\n";
    }
    if(state.actorListSignature!=sig){
        state.cursor=0;
        state.actorListSignature=sig;
        os<<"RESET: cached actor list or ordering changed\n";
    }
    if(state.cursor>=actors.size()){
        state.cursor=0;
        ++state.pass;
        os<<"WRAP: starting next pass to detect state changes\n";
    }
    const std::size_t first=state.cursor;
    const std::size_t last=std::min(first+PageSize,actors.size());
    os<<"pass="<<state.pass<<" cachedActors="<<actors.size()
      <<" indexRange=["<<first<<","<<last<<")"
      <<" remainingAfter="<<(actors.size()-last)<<"\n";
    std::unordered_set<Addr> seen;
    unsigned scanned=0,matched=0,unusual=0,changed=0,lookups=0;
    for(std::size_t ix=first;ix<last;++ix){
        const auto& actor=actors[ix];
        if(!validPtr(actor.address)||!seen.insert(actor.address).second)continue;
        if(!ue.saneObject(actor.address))continue;
        ++scanned;
        std::array<Addr,MaxActorBytes/sizeof(Addr)> ptrs{};
        if(!ue.memory().read(actor.address,ptrs.data(),sizeof(ptrs)))continue;
        Addr mesh{};
        std::array<Addr,8> outlines{};
        int count=0;
        for(const Addr object:ptrs){
            if(lookups>=MaxOwnedLookups)break;
            if(!validPtr(object)||!owns(ue,actor.address,object))continue;
            ++lookups;
            const auto cls=ue.className(ue.classOf(object));
            if(cls=="SkeletalMeshComponent"&&!mesh)mesh=object;
            if(cls=="OutlineComponent_C"&&count<static_cast<int>(outlines.size())){
                bool duplicate=false;
                for(int i=0;i<count;++i)if(outlines[static_cast<std::size_t>(i)]==object)duplicate=true;
                if(!duplicate)outlines[static_cast<std::size_t>(count++)]=object;
            }
        }
        for(int i=0;i<count;++i){
            ++matched;
            sample(ue,os,Entry{actor.address,outlines[static_cast<std::size_t>(i)],
                 mesh,actor.name},state,unusual,changed);
        }
        if(lookups>=MaxOwnedLookups){
            os<<"LIMIT reached: owned lookups. Next page resumes at index "<<(ix+1)<<"\n";
            state.cursor=ix+1;break;
        }
    }
    if(lookups<MaxOwnedLookups)state.cursor=last;
    os<<"SUMMARY actorsChecked="<<scanned<<" outlines="<<matched
      <<" nonzeroMaskOrRegistered="<<unusual<<" stateChanges="<<changed
      <<" ownedLookups="<<lookups<<" nextIndex="<<state.cursor
      <<" total="<<actors.size()<<"\n";
    if(state.cursor==actors.size())
        os<<"PASS COMPLETE: next F12 wraps to index 0 for repeat comparison\n";
    os<<"NOTE: only categorized cached actors; list changes reset pagination.\n"
      <<"History is in-process only, reset on game-world change or ESP restart.\n"
      <<"No game writes, renderer hooks or UE calls.\n\n";
    result.success=true;result.text=os.str();
    result.nextIndex=state.cursor;result.total=actors.size();
    return result;
}
bool appendOutlineSurvey(const OutlineSurveyReport& report){
    std::ofstream file(outlineSurveyPath(),std::ios::app);
    if(!file)return false;
    file<<report.text;return file.good();
}
}
