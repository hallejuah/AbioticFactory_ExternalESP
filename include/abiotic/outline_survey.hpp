#pragma once
#include "ue5.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace abiotic {
struct OutlineSurveyReport { bool success{}; std::string text; };
OutlineSurveyReport surveyOutlineActivation(UE5& ue,const std::vector<Actor>& actors);
std::filesystem::path outlineSurveyPath();
bool appendOutlineSurvey(const OutlineSurveyReport&);
}
