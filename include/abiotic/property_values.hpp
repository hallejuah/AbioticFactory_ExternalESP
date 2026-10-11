#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct PropertyValuesResult { bool success{}; std::string report; };
PropertyValuesResult inspectPropertyValues(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path propertyValuesReportPath();
bool appendPropertyValuesReport(const PropertyValuesResult& result);
}
