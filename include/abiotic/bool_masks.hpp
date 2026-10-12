#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct BoolMaskReport { bool success{}; std::string text; };
BoolMaskReport inspectBoolMasks(UE5& ue,Addr actor,const std::string& className);
std::filesystem::path boolMaskReportPath();
bool appendBoolMaskReport(const BoolMaskReport& report);
}
