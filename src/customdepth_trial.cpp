#include "abiotic/customdepth_trial.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <sstream>
#include <unordered_set>
namespace abiotic {
namespace {
constexpr Addr MeshOffset=0x320,OutlineOffset=0x9D8,OwnerOffset=0x20;
constexpr Addr PropertyChain=0x50,FieldNext=0x18,FieldName=0x20;
constexpr Addr FieldClass=0x08,InternalOffset=0x44,BoolMetadata=0x70;
constexpr std::uint8_t ExpectedMask=0x40;
constexpr Addr ExpectedInstanceOffset=0x269;
std::string fname(UE5& ue,Addr field,Addr offset){
    const auto id=ue.memory().get<std::uint32_t>(field+offset);
    return id?ue.name(*id):std::string{};
}
bool validate(UE5& ue,Addr actor,Addr& mesh,std::string& reason){
    if(!ue.saneObject(actor)){reason="invalid actor";return false;}
    const auto actorName=fname(ue,ue.classOf(actor),offsets::UObjectName);
    if(actorName!="NPC_Monster_Pest_C"){reason="not a Pest";return false;}
    mesh=ue.memory().pointer(actor+MeshOffset);
    const Addr outline=ue.memory().pointer(actor+OutlineOffset);
    if(!ue.saneObject(mesh)||ue.memory().pointer(mesh+OwnerOffset)!=actor||
       ue.className(ue.classOf(mesh))!="SkeletalMeshComponent"){
        reason="mesh class/Outer mismatch";return false;
    }
    if(!ue.saneObject(outline)||ue.memory().pointer(outline+OwnerOffset)!=actor||
       ue.className(ue.classOf(outline))!="OutlineComponent_C"){
        reason="outline class/Outer mismatch";return false;
    }
    Addr cls=ue.classOf(mesh);
    bool found=false;
    std::unordered_set<Addr> visited;
    for(unsigned depth=0;depth<8&&validPtr(cls)&&visited.insert(cls).second;++depth){
        if(ue.className(cls)=="PrimitiveComponent"){
            Addr field=ue.memory().pointer(cls+PropertyChain);
            std::unordered_set<Addr> fields;
            for(unsigned i=0;i<256&&validPtr(field)&&fields.insert(field).second;++i){
                if(fname(ue,field,FieldName)=="bRenderCustomDepth"){
                    const Addr fc=ue.memory().pointer(field+FieldClass);
                    const auto off=ue.memory().get<std::int32_t>(field+InternalOffset);
                    const auto size=ue.memory().get<std::uint8_t>(field+BoolMetadata);
                    const auto byteOff=ue.memory().get<std::uint8_t>(field+BoolMetadata+1);
                    const auto byteMask=ue.memory().get<std::uint8_t>(field+BoolMetadata+2);
                    const auto fieldMask=ue.memory().get<std::uint8_t>(field+BoolMetadata+3);
                    found=validPtr(fc)&&fname(ue,fc,0)=="BoolProperty"&&off&&
                          *off==ExpectedInstanceOffset&&size&&*size==1&&
                          byteOff&&*byteOff==0&&byteMask&&*byteMask==ExpectedMask&&
                          fieldMask&&*fieldMask==ExpectedMask;
                    break;
                }
                const Addr next=ue.memory().pointer(field+FieldNext);
                if(!validPtr(next)||next==field)break;
                field=next;
            }
            break;
        }
        cls=ue.memory().pointer(cls+offsets::Super);
    }
    if(!found){reason="FBoolProperty metadata mismatch";return false;}
    return true;
}
bool writeChecked(const Process& process,Addr address,std::uint8_t expected,
                  std::uint8_t desired,std::string& reason){
    if(!validPtr(address)||!process.alive()){reason="process/address invalid";return false;}
    HANDLE handle=OpenProcess(PROCESS_VM_READ|PROCESS_VM_WRITE|PROCESS_QUERY_LIMITED_INFORMATION,
                              FALSE,process.pid());
    if(!handle){reason="cannot obtain VM_WRITE access";return false;}
    std::uint8_t current{};
    SIZE_T count{};
    bool ok=ReadProcessMemory(handle,reinterpret_cast<LPCVOID>(address),&current,1,&count)&&
            count==1&&current==expected;
    if(!ok)reason="compare-before-write mismatch";
    if(ok){
        count=0;
        ok=WriteProcessMemory(handle,reinterpret_cast<LPVOID>(address),&desired,1,&count)&&count==1;
        if(!ok)reason="WriteProcessMemory failed";
    }
    CloseHandle(handle);
    return ok;
}
}
bool startCustomDepthTrial(UE5& ue,const Process& process,Addr actor,
                           CustomDepthTrial& trial,std::string& log){
    if(trial.active){log="trial already active";return false;}
    Addr mesh{};
    if(!validate(ue,actor,mesh,log))return false;
    const Addr address=mesh+ExpectedInstanceOffset;
    const auto original=process.get<std::uint8_t>(address);
    if(!original){log="byte read failed";return false;}
    if((*original&ExpectedMask)!=0){log="CustomDepth bit already set; refusing overwrite";return false;}
    // This one-byte operation does NOT call UE SetRenderCustomDepth or update scene proxies.
    if(!writeChecked(process,address,*original,static_cast<std::uint8_t>(*original|ExpectedMask),log))
        return false;
    const auto confirmed=process.get<std::uint8_t>(address);
    if(!confirmed||(*confirmed&ExpectedMask)==0){
        std::string recover;
        writeChecked(process,address,static_cast<std::uint8_t>(*original|ExpectedMask),*original,recover);
        log="write not confirmed; attempted immediate restoration: "+recover;return false;
    }
    trial={actor,mesh,address,*original,process.pid(),GetTickCount64()+5000,true};
    log="trial ON for max 5s; renderer effect not guaranteed";return true;
}
bool restoreCustomDepthTrial(UE5& ue,const Process& process,
                             CustomDepthTrial& trial,std::string& log){
    if(!trial.active)return true;
    if(process.pid()!=trial.pid||!process.alive()){
        log="process changed/closed; cannot restore; restart game if needed";
        trial.active=false;return false;
    }
    Addr mesh{};
    if(!validate(ue,trial.actor,mesh,log)||mesh!=trial.mesh){
        log="component identity changed; refusing stale address restoration";
        trial.active=false;return false;
    }
    const auto current=process.get<std::uint8_t>(trial.byteAddress);
    if(!current){log="restore read failed; retry pending";return false;}
    // Preserve changes to unrelated bits that the game may make during the trial.
    const std::uint8_t desired=static_cast<std::uint8_t>(
        (*current & ~ExpectedMask)|(trial.original & ExpectedMask));
    if(!writeChecked(process,trial.byteAddress,*current,desired,log))return false;
    const auto verified=process.get<std::uint8_t>(trial.byteAddress);
    if(!verified||((*verified^trial.original)&ExpectedMask)){
        log="restore verification failed; retry pending";return false;
    }
    trial.active=false;log="CustomDepth bit restored and verified";return true;
}
}
