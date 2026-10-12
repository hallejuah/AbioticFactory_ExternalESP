#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct RenderPipelineReport {bool success{};std::string text;};
RenderPipelineReport inspectRenderPipeline(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path renderPipelineReportPath();
bool appendRenderPipelineReport(const RenderPipelineReport& report);
}
