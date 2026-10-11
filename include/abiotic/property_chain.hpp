#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
namespace abiotic {
struct PropertyChainReport {bool success{};std::string text;};
PropertyChainReport probePropertyChains(UE5& ue,Addr actor,const std::string& actorClass);
std::filesystem::path propertyChainPath();
bool appendPropertyChain(const PropertyChainReport& result);
}
