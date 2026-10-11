#pragma once
#include "ue5.hpp"
#include <array>
#include <vector>
namespace abiotic {
// Experimental world-space projection. No mesh silhouette or bone connections.
struct BoneDebugPoint { int index{}; ScreenPoint screen{}; };
std::vector<BoneDebugPoint> projectPestBones(UE5& ue,Addr actor,const Camera& camera,int width,int height);
}
