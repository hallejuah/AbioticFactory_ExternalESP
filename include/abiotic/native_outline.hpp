#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct NativeOutlineReport {bool success{};std::string text;};
NativeOutlineReport inspectNativeOutline(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path nativeOutlineReportPath();
bool appendNativeOutlineReport(const NativeOutlineReport& report);
}
