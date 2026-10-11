#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
// Opt-in read-only research. This does NOT enable the game's outline system.
struct OutlineProbeResult {
    bool success{};
    std::string report;
};
OutlineProbeResult inspectOutlineComponent(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path outlineProbePath();
bool appendOutlineProbe(const OutlineProbeResult& result);
}
