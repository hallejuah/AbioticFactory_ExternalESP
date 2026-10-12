#include "abiotic/weapon_research.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace abiotic {
namespace {
constexpr Addr Outer=0x20,ChildProperties=0x50,Super=0x40;
constexpr Addr Next=0x18,Name=0x20,FieldClass=0x08,Offset=0x44;
constexpr unsigned MaxClasses=8,MaxFields=384,MaxCandidates=96;
bool weaponTerm(const std::string& value){
    const std::string_view s(value);
    for(const auto word:{"Weapon","weapon","Gun","gun","Needle","needle",
                         "Fire","fire","Recoil","recoil","Spread","spread",
                         "Shoot","shoot","Equip","equip","Pawn","Character",
                         "Inventory","inventory","Held","held","Item","item",
                         "Reload","reload","Ammo","ammo","Bullet","bullet",
                         "Attack","attack","Hand","hand","Wield","wield"})
        if(s.find(word)!=std::string_view::npos)return true;
    return false;
}
void reflectedHints(UE5& ue,std::ostringstream& out,Addr obj,const char* label){
    out<<label<<" property-name hints (candidate offsets, no writes):\n";
    Addr cls=ue.classOf(obj);
    std::unordered_set<Addr> seenClasses;
    unsigned hits=0;
    for(unsigned depth=0;depth<MaxClasses&&validPtr(cls)&&seenClasses.insert(cls).second;++depth){
        const auto cname=ue.className(cls);
        if(cname.empty())break;
        Addr field=ue.memory().pointer(cls+ChildProperties);
        std::unordered_set<Addr> seenFields;
        for(unsigned i=0;i<MaxFields&&validPtr(field)&&seenFields.insert(field).second;++i){
            const auto id=ue.memory().get<std::uint32_t>(field+Name);
            if(!id)break;
            const auto name=ue.name(*id);
            if(name.empty())break;
            if(weaponTerm(name)){
                const auto off=ue.memory().get<std::int32_t>(field+Offset);
                Addr fcls=ue.memory().pointer(field+FieldClass);
                std::string type{};
                if(validPtr(fcls)){
                    auto typeId=ue.memory().get<std::uint32_t>(fcls);
                    if(typeId)type=ue.name(*typeId);
                }
                out<<"  "<<cname<<"."<<name<<" type="<<type;
                if(off&&*off>=0&&*off<0x8000){
                    out<<" offsetCandidate=0x"<<std::hex<<*off<<std::dec;
                    if(type=="ObjectProperty"||type=="ClassProperty"||
                       type=="WeakObjectProperty"){
                        Addr value=ue.memory().pointer(obj+static_cast<Addr>(*off));
                        if(ue.saneObject(value))
                            out<<" objectClass="<<ue.className(ue.classOf(value));
                        else out<<" object=null/unknown";
                    }
                }else out<<" invalidOffset";
                out<<"\n";
                ++hits;
                if(hits>=64){out<<"  MATCH LIMIT\n";return;}
            }
            field=ue.memory().pointer(field+Next);
        }
        cls=ue.memory().pointer(cls+Super);
    }
    if(!hits)out<<"  no matching reflected properties\n";
}
void directObjects(UE5& ue,std::ostringstream& out,Addr owner,const char* label){
    out<<label<<" owned UObject candidate references:\n";
    std::array<Addr,0x1800/sizeof(Addr)> data{};
    if(!ue.memory().read(owner,data.data(),sizeof(data))){
        out<<"  memory unavailable\n";return;
    }
    std::unordered_set<Addr> visited;
    unsigned examined=0,hits=0;
    for(size_t i=0;i<data.size()&&examined<MaxCandidates;++i){
        Addr ptr=data[i];
        if(!validPtr(ptr)||!visited.insert(ptr).second)continue;
        ++examined;
        if(!ue.saneObject(ptr))continue;
        const auto cn=ue.className(ue.classOf(ptr));
        const bool owned=ue.memory().pointer(ptr+Outer)==owner;
        if(weaponTerm(cn)&&owned){
            out<<"  owner+0x"<<std::hex<<(i*8)<<std::dec<<" class="<<cn<<"\n";
            if(++hits>=16)break;
        }
    }
    out<<"  sampledPointers="<<examined<<" matches="<<hits<<"\n";
}
}
std::filesystem::path needleRifleReportPath(){
    return settingsPath().parent_path()/L"abiotic_needle_rifle_research.txt";
}
WeaponResearchResult inspectNeedleRifle(UE5& ue){
    WeaponResearchResult result{};
    std::ostringstream out;
    out<<"Abiotic ESP v2.2 Needle Rifle EQUIPMENT research (READ ONLY)\n";
    Addr world=0,camera=0;
    if(!ue.readChain(world,camera)||!ue.anchorCheck(world)){
        result.report="Weapon research: world/controller chain failed\n\n";
        return result;
    }
    const Addr gi=ue.memory().pointer(world+offsets::WorldGI);
    TArray players{};
    if(!ue.saneObject(gi)||!ue.array(gi+offsets::GIPlayers,256,players)){
        result.report="Weapon research: local player array invalid\n\n";return result;
    }
    const Addr lp=ue.memory().pointer(players.data);
    const Addr controller=ue.memory().pointer(lp+offsets::LPController);
    if(!ue.saneObject(lp)||!ue.saneObject(controller)||
       ue.memory().pointer(controller+offsets::ControllerCamera)!=camera){
        result.report="Weapon research: controller identity validation failed\n\n";
        return result;
    }
    // All four controller properties were observed pointing to the same
    // local Abiotic_PlayerCharacter_C in v2.1. Revalidate two independent
    // fields before entering the character's potentially large object layout.
    const Addr character=ue.memory().pointer(controller+0xF78);
    const Addr pawn=ue.memory().pointer(controller+0x2D8);
    const bool characterVerified=ue.saneObject(character)&&
        ue.className(ue.classOf(character))=="Abiotic_PlayerCharacter_C"&&
        character==pawn;
    out<<"v2.2 character identity: MyPlayerCharacter==Pawn="<<
         (characterVerified?"yes":"no")<<"\n";
    out<<"LocalPlayer class="<<ue.className(ue.classOf(lp))<<"\n";
    out<<"PlayerController class="<<ue.className(ue.classOf(controller))<<"\n";
    reflectedHints(ue,out,controller,"Controller");
    directObjects(ue,out,controller,"Controller");
    reflectedHints(ue,out,lp,"LocalPlayer");
    if(characterVerified){
        out<<"PlayerCharacter class="<<ue.className(ue.classOf(character))<<"\n";
        reflectedHints(ue,out,character,"PlayerCharacter");
        directObjects(ue,out,character,"PlayerCharacter");
    }else{
        out<<"SKIP: character identity validation failed; no character scan\n";
    }
    out<<"Next: compare snapshots with Needle Rifle and another held item.\n"
       <<"The inspected values DO NOT yet identify the needle rifle conclusively.\n"
       <<"No rapid-fire/recoil patch, function invocation or memory write.\n\n";
    result.success=true;result.report=out.str();return result;
}
bool appendNeedleRifleReport(const WeaponResearchResult& report){
    std::ofstream file(needleRifleReportPath(),std::ios::app);
    if(!file)return false;
    file<<report.report;return file.good();
}
}
