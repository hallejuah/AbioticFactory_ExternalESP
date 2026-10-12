#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
#include <cstdint>
namespace abiotic {
struct WeaponResearchResult { bool success{}; std::string report; };
WeaponResearchResult inspectNeedleRifle(UE5& ue);
std::filesystem::path needleRifleReportPath();
bool appendNeedleRifleReport(const WeaponResearchResult&);
struct WeaponSampleSession {
    Addr character{}, item{};
    std::uint64_t startMs{}, nextMs{};
    unsigned samples{};
    bool active{};
    std::string csv;
};
bool startWeaponSampling(UE5&, WeaponSampleSession&, std::uint64_t nowMs, std::string& reason);
bool stepWeaponSampling(UE5&, WeaponSampleSession&, std::uint64_t nowMs, std::string& summary);
void cancelWeaponSampling(WeaponSampleSession&);
std::filesystem::path weaponSamplesPath();
bool appendWeaponSamples(const WeaponSampleSession&);
}
