#include "abiotic/scanner.hpp"
#include "abiotic/settings.hpp"
#include <fstream>
#include <map>
#include <unordered_set>
#include <sstream>
#include <algorithm>
namespace abiotic {
namespace {
bool sameSignature(const std::vector<std::pair<Addr,TArray>>& a,const std::vector<std::pair<Addr,TArray>>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){
        if(a[i].first!=b[i].first||a[i].second.data!=b[i].second.data||
           a[i].second.count!=b[i].second.count||a[i].second.capacity!=b[i].second.capacity)return false;
    }return true;
}
}
bool Scanner::identityChanged() const {
    Addr world=0,manager=0;
    if(!ue_.readChain(world,manager)||world!=current_.world||manager!=current_.cameraManager)return true;
    TArray levels{};
    if(!ue_.array(world+offsets::WorldLevels,512,levels))return true;
    std::vector<Addr> ptrs(levels.count);
    if(!ue_.memory().read(levels.data,ptrs.data(),ptrs.size()*sizeof(Addr)))return true;
    return ptrs!=current_.levels;
}
bool Scanner::refresh(std::string& error){
    const auto& mem=ue_.memory();
    Addr world=0,manager=0;
    if(!ue_.readChain(world,manager)){error="Camera/world chain unavailable";return false;}
    if(!ue_.anchorCheck(world)){error="FNamePool anchor validation failed";return false;}
    TArray arr{};
    if(!ue_.array(world+offsets::WorldLevels,512,arr)){error="UWorld Levels array invalid";return false;}
    std::vector<Addr> levels(arr.count);
    if(!mem.read(arr.data,levels.data(),levels.size()*sizeof(Addr))){error="Cannot read Levels";return false;}
    auto persistent=mem.pointer(world+offsets::WorldPersistent);
    if(!persistent||std::find(levels.begin(),levels.end(),persistent)==levels.end()){
        error="PersistentLevel not in Levels";return false;
    }
    Snapshot next;next.world=world;next.cameraManager=manager;next.levels=levels;
    if(!ue_.readCamera(manager,next.camera)){error="CameraCache invalid";return false;}
    std::vector<std::pair<Addr,TArray>> start,finish;
    std::unordered_set<Addr> seen;
    std::unordered_map<Addr,std::string> classes;
    for(Addr level:levels){
        if(!validPtr(level)){error="Invalid level pointer";return false;}
        TArray actorArr{};
        if(!ue_.array(level+offsets::LevelActors,250000,actorArr,true)){
            error="Actor array invalid (streaming)";return false;
        }
        start.push_back({level,actorArr});
        for(int offset=0;offset<actorArr.count;offset+=4096){
            int take=std::min(4096,actorArr.count-offset);
            std::vector<Addr> pointers(take);
            if(!mem.read(actorArr.data+(Addr)offset*8,pointers.data(),(size_t)take*8))continue;
            for(Addr actor:pointers){
                if(!validPtr(actor)||!seen.insert(actor).second)continue;
                Addr cls=ue_.classOf(actor);if(!cls)continue;
                auto it=classes.find(cls);
                if(it==classes.end())it=classes.emplace(cls,ue_.className(cls)).first;
                Category cat=categorize(it->second);
                if(cat==Category::Unknown)continue;
                next.actors.push_back({actor,it->second,cat});
                if(next.actors.size()>=5000)break;
            }
        }
    }
    // Atomically commit only if Level Actor TArrays stayed unchanged through scanning.
    for(Addr level:levels){
        TArray a{};if(!ue_.array(level+offsets::LevelActors,250000,a,true)){error="Level changed mid-scan";return false;}
        finish.push_back({level,a});
    }
    if(!sameSignature(start,finish)){error="Actor lists changed mid-scan; retry later";return false;}
    next.generation=current_.generation+1;
    current_=std::move(next);signature_=std::move(start);
    // Overwrite on each successful scan: current late-game class families,
    // including NPC_ candidates that the classifier still excludes.
    {
        std::map<std::string,int> counts;
        for(const auto& actor:current_.actors)
            if(actor.name.rfind("NPC_",0)==0)++counts[actor.name];
        std::map<std::string,bool> unknownNpc;
        for(const auto& [classPtr,className]:classes){
            (void)classPtr;
            if(className.rfind("NPC_",0)==0&&categorize(className)==Category::Unknown)
                unknownNpc[className]=true;
        }
        std::ofstream file(settingsPath().parent_path()/L"abiotic_hostile_classes.txt",
                           std::ios::trunc);
        if(file){
            file<<"Abiotic ESP v2.0 NPC class coverage (latest successful scan)\n"
                <<"recognized NPC class counts:\n";
            for(const auto& [name,count]:counts)file<<"  "<<name<<" count="<<count<<"\n";
            file<<"unclassified NPC_ class candidates (NOT auto-labelled hostile):\n";
            for(const auto& [name,present]:unknownNpc){(void)present;file<<"  "<<name<<"\n";}
            file<<"Note: an NPC_ prefix alone does not establish hostility.\n";
        }
    }
    error.clear();return true;
}
}
