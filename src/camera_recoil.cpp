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
void cancelCameraRecoilCapture(CameraRecoilSession& s){s.active=false;}
bool saveCameraRecoilCapture(const CameraRecoilSession& s){
    if(!s.samples)return false;
    std::ofstream out(cameraRecoilReportPath(),std::ios::app);
    if(!out)return false;
    out<<s.csv;
    return out.good();
}
}
