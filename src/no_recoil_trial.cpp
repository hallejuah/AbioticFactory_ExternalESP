#include "abiotic/no_recoil_trial.hpp"
#include <Windows.h>
#include <cmath>
#include <cstring>
namespace abiotic {
namespace {
constexpr Addr RecoilOffset=0x1A00;
constexpr std::uint64_t MaxDurationMs=1500;
constexpr unsigned MaxWrites=90;
bool verified(UE5& ue,const Process& process,const NoRecoilTrial& trial){
    if(process.pid()!=trial.pid||!process.alive()||
       !ue.saneObject(trial.character)||
       ue.className(ue.classOf(trial.character))!="Abiotic_PlayerCharacter_C")
        return false;
    const Addr item=ue.memory().pointer(trial.character+0x1A50);
    return item==trial.rifle&&ue.saneObject(item)&&
        ue.className(ue.classOf(item))=="Abiotic_Weapon_QuillRifle_C";
}
bool writeIfEqual(const Process& process,Addr addr,double expected,double value){
    if(!process.alive()||!validPtr(addr)||!std::isfinite(expected))return false;
    HANDLE h=OpenProcess(PROCESS_VM_READ|PROCESS_VM_WRITE|
                          PROCESS_VM_OPERATION|PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE,process.pid());
    if(!h)return false;
    double current{};
    SIZE_T count{};
    const bool match=ReadProcessMemory(h,reinterpret_cast<LPCVOID>(addr),
                        &current,sizeof(current),&count)&&count==sizeof(current)&&
                        std::memcmp(&current,&expected,sizeof(current))==0;
    bool ok=false;
    if(match){
        count=0;
        ok=WriteProcessMemory(h,reinterpret_cast<LPVOID>(addr),
                              &value,sizeof(value),&count)&&count==sizeof(value);
    }
    CloseHandle(h);
    return ok;
}
}
bool startNoRecoilTrial(UE5& ue,const Process& process,NoRecoilTrial& trial,
                        std::uint64_t now,std::string& reason){
    if(trial.active){reason="already active";return false;}
    Addr world=0,camera=0;
    if(!ue.readChain(world,camera)||!ue.anchorCheck(world)){
        reason="world verification failed";return false;
    }
    const Addr gi=process.pointer(world+offsets::WorldGI);
    TArray players{};
    if(!ue.saneObject(gi)||!ue.array(gi+offsets::GIPlayers,256,players)){
        reason="local players invalid";return false;
    }
    const Addr player=process.pointer(players.data);
    const Addr controller=process.pointer(player+offsets::LPController);
    if(!ue.saneObject(player)||!ue.saneObject(controller)||
       process.pointer(controller+offsets::ControllerCamera)!=camera){
        reason="controller verification failed";return false;
    }
    const Addr character=process.pointer(controller+0xF78);
    if(!ue.saneObject(character)||
       ue.className(ue.classOf(character))!="Abiotic_PlayerCharacter_C"||
       process.pointer(controller+0x2D8)!=character){
        reason="character verification failed";return false;
    }
    const Addr rifle=process.pointer(character+0x1A50);
    if(!ue.saneObject(rifle)||
       ue.className(ue.classOf(rifle))!="Abiotic_Weapon_QuillRifle_C"){
        reason="equip Quill Rifle first";return false;
    }
    // Only arm when the baseline equals zero. Thus a zero-write cannot
    // require restoring a nonzero preexisting impulse if the trial stops.
    const auto initial=process.get<double>(character+RecoilOffset);
    if(!initial||*initial!=0.0){
        reason="recoil must be idle (zero) before enabling";return false;
    }
    trial={character,rifle,process.pid(),now+MaxDurationMs,0,true};
    reason="armed for 1.5s; F1 again stops early";
    return true;
}
bool tickNoRecoilTrial(UE5& ue,const Process& process,NoRecoilTrial& trial,
                       std::uint64_t now,std::string& reason){
    if(!trial.active)return false;
    if(now>=trial.deadline||trial.writes>=MaxWrites){
        stopNoRecoilTrial(trial,reason);return true;
    }
    if(!verified(ue,process,trial)){
        stopNoRecoilTrial(trial,reason);
        reason="stopped: process/character/weapon changed";return true;
    }
    const auto current=process.get<double>(trial.character+RecoilOffset);
    if(!current||!std::isfinite(*current)||*current<0||*current>10.0){
        stopNoRecoilTrial(trial,reason);
        reason="stopped: invalid recoil value";return true;
    }
    if(*current==0.0)return false;
    // Compare-before-write, exact double. No blanket memory patching.
    if(!writeIfEqual(process,trial.character+RecoilOffset,*current,0.0)){
        stopNoRecoilTrial(trial,reason);
        reason="stopped: compare/write failed";return true;
    }
    const auto observed=process.get<double>(trial.character+RecoilOffset);
    if(!observed||*observed!=0.0){
        stopNoRecoilTrial(trial,reason);
        reason="stopped: write was not retained";return true;
    }
    ++trial.writes;
    return false;
}
void stopNoRecoilTrial(NoRecoilTrial& trial,std::string& reason){
    const auto n=trial.writes;
    trial.active=false;
    // Baseline was exactly zero, and every write sets only this field to
    // zero. Do not overwrite a later engine-owned recoil value on exit:
    // that value might be a new live shot, not the trial's change.
    reason="finished; zero baseline preserved; zero-writes="+std::to_string(n);
}
}
