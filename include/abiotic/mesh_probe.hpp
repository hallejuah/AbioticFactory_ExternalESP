#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace abiotic {
// Manual, bounded read-only diagnostic for actor-owned UE objects.
// This is NOT a verified source of skeletal transforms or a silhouette renderer.
struct ComponentProbeResult {
    bool success{};
    std::size_t fieldsChecked{};
    std::size_t ownedObjects{};
    std::size_t meshCandidates{};
    std::string report;
};
ComponentProbeResult probeActorComponents(UE5& ue, Addr actor, const std::string& actorClass);
std::filesystem::path meshProbeReportPath();
bool appendProbeReport(const ComponentProbeResult& result);
}
