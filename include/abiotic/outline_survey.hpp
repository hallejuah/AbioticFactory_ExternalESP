#pragma once
#include "ue5.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
namespace abiotic {
struct OutlineObservation {
    Addr outline{};
    std::string actorType;
    std::int32_t mask{};
    std::int32_t registeredCount{};
    std::uint8_t enabledByte{};
    bool depthOn{};
    bool depthKnown{};
};
struct OutlineSurveyState {
    Addr world{};
    std::size_t cursor{};
    std::uint64_t actorListSignature{};
    std::uint64_t pass{};
    std::unordered_map<Addr,OutlineObservation> observations;
};
struct OutlineSurveyReport {
    bool success{};
    std::string text;
    std::size_t nextIndex{};
    std::size_t total{};
};
OutlineSurveyReport surveyOutlineActivation(UE5& ue,
    const Snapshot& snapshot,OutlineSurveyState& state);
std::filesystem::path outlineSurveyPath();
bool appendOutlineSurvey(const OutlineSurveyReport&);
}
