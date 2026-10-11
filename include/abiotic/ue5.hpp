#pragma once
#include "process.hpp"
#include <unordered_set>
namespace abiotic {
namespace offsets {
constexpr Addr GWorld=0x084FB578, NameBlocks=0x082C0350;
constexpr Addr WorldGI=0x1D8, WorldLevels=0x178, WorldPersistent=0x30;
constexpr Addr GIPlayers=0x38, LPController=0x30, ControllerCamera=0x350;
constexpr Addr LevelActors=0xA0, UObjectClass=0x10, UObjectName=0x18, Super=0x40;
constexpr Addr RootComponent=0x1A8, Position=0x1F0, CameraCache=0x13A0;
constexpr Addr Health=0x8F8, Armor=0x8FC, Death=0xA48;
}
struct TArray {Addr data{};std::int32_t count{}, capacity{};};
class UE5 {
public:
    explicit UE5(const Process& p):p_(p) {}
    void clearCaches();
    std::string name(std::uint32_t id);
    std::string className(Addr classPtr);
    Addr classOf(Addr object) const;
    bool saneObject(Addr object) const;
    bool array(Addr address,int maxSize,TArray& out,bool allowEmpty=false) const;
    bool readChain(Addr& world,Addr& cameraManager) const;
    bool readCamera(Addr cameraManager,Camera& camera) const;
    bool actorPos(Addr actor,Vec3& pos) const;
    Life life(Addr actor,bool robot) const;
    bool anchorCheck(Addr world) ;
    const Process& memory() const {return p_;}
private:
    const Process& p_;
    std::unordered_map<std::uint32_t,std::string> names_;
    std::unordered_map<Addr,std::string> classes_;
};
ScreenPoint project(const Camera& camera,const Vec3& pos,int width,int height);
Category categorize(const std::string& name);
}
