#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct BoneProbeResult { bool success{}; std::string text; };
BoneProbeResult inspectBoneTransforms(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path boneProbePath();
bool appendBoneProbe(const BoneProbeResult&);
}
