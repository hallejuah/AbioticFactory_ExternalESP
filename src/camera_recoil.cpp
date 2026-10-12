#include "abiotic/camera_recoil.hpp"
#include "abiotic/settings.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace abiotic {
std::filesystem::path cameraRecoilReportPath(){
    return settingsPath().parent_path()/L"abiotic_camera_recoil.csv";
}
bool startCameraRecoilCapture(UE5& ue,const Process& process,CameraRecoilSession& s,
                              std::uint64_t now,std::string& reason){
    if(s.active){reason="capture already active";return false;}
    Addr world=0,mgr=0;
    if(!ue.readChain(world,mgr)||!ue.anchorCheck(world)){
        reason="world/camera validation failed";return false;
    }
    const Addr gi=process.pointer(world+offsets::WorldGI);
    TArray players{};
    if(!ue.saneObject(gi)||!ue.array(gi+offsets::GIPlayers,256,players)){
        reason="local player array invalid";return false;
    }
    const Addr lp=process.pointer(players.data);
    if(!ue.saneObject(lp)){reason="local player invalid";return false;}
    const Addr controller=process.pointer(lp+offsets::LPController);
    if(!ue.saneObject(controller)||
       process.pointer(controller+offsets::ControllerCamera)!=mgr){
        reason="controller identity invalid";return false;
    }
    const Addr character=process.pointer(controller+0xF78);
    if(!ue.saneObject(character)||
       ue.className(ue.classOf(character))!="Abiotic_PlayerCharacter_C"||
       process.pointer(controller+0x2D8)!=character){
        reason="character identity invalid";return false;
    }
    const Addr rifle=process.pointer(character+0x1A50);
    if(!ue.saneObject(rifle)||
       ue.className(ue.classOf(rifle))!="Abiotic_Weapon_QuillRifle_C"){
        reason="equip Quill Rifle";return false;
    }
    Camera view{};
    if(!ue.readCamera(mgr,view)){reason="camera unavailable";return false;}
    s={};
    s.character=character;s.rifle=rifle;s.cameraManager=mgr;
    s.pid=process.pid();s.startMs=now;s.nextMs=now;s.active=true;
    s.csv="\n# v2.8 camera recoil read-only ~20ms, 2s max\n"
          "elapsed_ms,camera_pitch,camera_yaw,camera_roll,last_fire,"
          "aim_recoil,controller_track,recoil_track,fire_delay,"
          "trial_write_count\n";
    reason="capture started (2s)";
    return true;
}
void cancelCameraRecoilCapture(CameraRecoilSession& s){s.active=false;}
bool saveCameraRecoilCapture(const CameraRecoilSession& s){
    if(!s.samples)return false;
    std::ofstream out(cameraRecoilReportPath(),std::ios::app);
    if(!out)return false;
    out<<s.csv;
    return out.good();
}
}
