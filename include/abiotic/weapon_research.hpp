#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct WeaponResearchResult { bool success{}; std::string report; };
WeaponResearchResult inspectNeedleRifle(UE5& ue);
std::filesystem::path needleRifleReportPath();
bool appendNeedleRifleReport(const WeaponResearchResult&);
}
