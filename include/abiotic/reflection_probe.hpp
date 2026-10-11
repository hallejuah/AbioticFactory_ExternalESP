#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct ReflectionProbeResult { bool success{}; std::string report; };
ReflectionProbeResult inspectReflection(UE5& ue, Addr actor, const std::string& actorClass);
std::filesystem::path reflectionReportPath();
bool appendReflectionReport(const ReflectionProbeResult& result);
}
