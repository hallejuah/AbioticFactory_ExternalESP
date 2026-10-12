#include "abiotic/render_pipeline.hpp"
#include "abiotic/settings.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace abiotic {
namespace {
constexpr Addr Owner=0x20,MeshOff=0x320,OutlineOff=0x9D8;
constexpr Addr OutlineMask=0xB0,RegisteredComponents=0xB8,ComponentEnabled=0xC8;
constexpr Addr CustomDepthByte=0x269,StencilValue=0x298,OverlayMaterial=0x530;
constexpr unsigned MaxActorBytes=0x1000,MaxPointerCandidates=160;
bool owned(UE5& ue,Addr actor,Addr object,const char* type){
    return validPtr(object)&&ue.saneObject(object)&&
      ue.memory().pointer(object+Owner)==actor&&
      ue.className(ue.classOf(object))==type;
}
bool interesting(const std::string& s){
    return s.find("Outline")!=std::string::npos ||
           s.find("PostProcess")!=std::string::npos ||
           s.find("Stencil")!=std::string::npos ||
           s.find("Highlight")!=std::string::npos ||
           s.find("Mesh")!=std::string::npos ||
           s.find("Material")!=std::string::npos;
}
void reportUObject(UE5& ue,std::ostringstream& out,Addr obj,Addr offset){
    const Addr cls=ue.classOf(obj);
    const auto clsName=ue.className(cls);
    if(!interesting(clsName))return;
    out<<"    actor+0x"<<std::hex<<offset<<std::dec
       <<" -> "<<clsName<<" owned-by-actor=yes\n";
}
}
std::filesystem::path renderPipelineReportPath(){
    return settingsPath().parent_path()/L"abiotic_render_pipeline.txt";
}
RenderPipelineReport inspectRenderPipeline(UE5& ue,Addr actor,const std::string& actorClass){
    RenderPipelineReport result{};
    std::ostringstream out;
    out<<"Abiotic ESP v1.7 Render Pipeline Research (READ ONLY)\n"
       <<"Actor: "<<actorClass<<"\n";
    if(!ue.saneObject(actor)||categorize(actorClass)!=Category::Hostile){
        out<<"SKIP: actor invalid or not hostile\n\n";
        result.text=out.str();return result;
    }
    const Addr mesh=ue.memory().pointer(actor+MeshOff);
    const Addr outline=ue.memory().pointer(actor+OutlineOff);
    if(!owned(ue,actor,mesh,"SkeletalMeshComponent")||
       !owned(ue,actor,outline,"OutlineComponent_C")){
        out<<"SKIP: expected mesh/outline classes or owner do not match\n\n";
        result.text=out.str();return result;
    }
    out<<"Known component ownership: mesh=OK, outline=OK\n";
    const auto mask=ue.memory().get<std::int32_t>(outline+OutlineMask);
    const auto enabled=ue.memory().get<std::uint8_t>(outline+ComponentEnabled);
    const auto depth=ue.memory().get<std::uint8_t>(mesh+CustomDepthByte);
    const auto stencil=ue.memory().get<std::int32_t>(mesh+StencilValue);
    const Addr overlay=ue.memory().pointer(mesh+OverlayMaterial);
    out<<"  OutlineMask candidate: "<<(mask?std::to_string(*mask):"read-failed")<<"\n";
    out<<"  ComponentEnabled raw byte: ";
    if(enabled)out<<"0x"<<std::hex<<unsigned(*enabled)<<std::dec;
    else out<<"read-failed";
    out<<"\n  Mesh bRenderCustomDepth byte: ";
    if(depth)out<<"0x"<<std::hex<<unsigned(*depth)<<std::dec
                 <<" (bit 0x40="<<((*depth&0x40)?"set":"clear")<<")";
    else out<<"read-failed";
    out<<"\n  CustomDepthStencilValue candidate: "
       <<(stencil?std::to_string(*stencil):"read-failed")<<"\n";
    out<<"  OverlayMaterial pointer: "<<(overlay?"nonzero":"null/invalid")<<"\n";
    const auto components=ue.memory().get<TArray>(outline+RegisteredComponents);
    if(components){
        out<<"  RegisteredComponents TArray candidate count="<<components->count
           <<" capacity="<<components->capacity<<" dataValid="
           <<(validPtr(components->data)?"yes":"no")<<"\n";
        if(components->count>=1&&components->count<=16&&
            components->capacity>=components->count&&components->capacity<=64&&
            validPtr(components->data)){
            std::array<Addr,16> pointers{};
            const auto bytes=static_cast<size_t>(components->count)*sizeof(Addr);
            if(ue.memory().read(components->data,pointers.data(),bytes)){
                for(int i=0;i<components->count;++i){
                    auto obj=pointers[static_cast<size_t>(i)];
                    out<<"    entry "<<i<<" ";
                    if(!ue.saneObject(obj))out<<"invalid UObject\n";
                    else out<<"class="<<ue.className(ue.classOf(obj))
                            <<" directlyOwned="<<(ue.memory().pointer(obj+Owner)==actor?"yes":"no")<<"\n";
                }
            }else out<<"    array buffer not readable\n";
        }
    }else out<<"  RegisteredComponents read failed\n";
    out<<"Actor owned component hints (bounded actor pointer scan):\n";
    std::array<Addr,MaxActorBytes/sizeof(Addr)> candidates{};
    if(ue.memory().read(actor,candidates.data(),sizeof(candidates))){
        unsigned examined=0,matched=0;
        std::unordered_set<Addr> visited;
        for(size_t i=0;i<candidates.size();++i){
            const Addr obj=candidates[i];
            if(!validPtr(obj)||!visited.insert(obj).second)continue;
            if(++examined>MaxPointerCandidates){out<<"    candidate limit reached\n";break;}
            if(!ue.saneObject(obj)||ue.memory().pointer(obj+Owner)!=actor)continue;
            const auto name=ue.className(ue.classOf(obj));
            if(interesting(name)){
                ++matched;
                out<<"    actor+0x"<<std::hex<<(i*sizeof(Addr))<<std::dec
                   <<" -> "<<name<<"\n";
            }
        }
        out<<"  pointers-examined="<<examined<<" interesting-owned="<<matched<<"\n";
    }else out<<"    actor memory read failed\n";
    out<<"Conclusion limits: current flags are NOT proof of render-thread state.\n"
       <<"A native outline may require a post-process material, scene proxy\n"
       <<"re-registration, or an engine function call. None is performed here.\n"
       <<"No writes, no injection, no engine calls.\n\n";
    result.success=true;result.text=out.str();return result;
}
bool appendRenderPipelineReport(const RenderPipelineReport& r){
    std::ofstream out(renderPipelineReportPath(),std::ios::app);
    if(!out)return false;
    out<<r.text;
    return out.good();
}
}
