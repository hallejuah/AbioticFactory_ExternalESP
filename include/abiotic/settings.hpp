#pragma once
#include "model.hpp"
#include <filesystem>
namespace abiotic {
std::filesystem::path settingsPath();
void loadSettings(Config& c);
void saveSettings(const Config& c);
}
