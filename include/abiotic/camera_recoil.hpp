#pragma once
#include "ue5.hpp"
#include <cstdint>
#include <string>
#include <filesystem>
namespace abiotic {
struct CameraRecoilSession {
    Addr character{}, rifle{}, cameraManager{};
    DWORD pid{};
    std::uint64_t startMs{}, nextMs{};
    unsigned samples{};
    bool active{};
    std::string csv;
};
bool startCameraRecoilCapture(UE5&,const Process&,CameraRecoilSession&,
                              std::uint64_t,std::string&);
bool stepCameraRecoilCapture(UE5&,const Process&,CameraRecoilSession&,
                             std::uint64_t,unsigned,std::string&);
void cancelCameraRecoilCapture(CameraRecoilSession&);
bool saveCameraRecoilCapture(const CameraRecoilSession&);
std::filesystem::path cameraRecoilReportPath();
}
