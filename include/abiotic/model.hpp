#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
namespace abiotic {
using Addr = std::uint64_t;
struct Vec3 { double x{}, y{}, z{}; };
struct Camera { Vec3 pos{}, rotation{}; float fov{}; };
enum class Category { Hostile, NarrativeNpc, Container, Dropped, MicroLoot, ResourceNode, Unknown };
struct Actor { Addr address{}; std::string name; Category category{Category::Unknown}; };
struct Life { float hp{}, armor{}; bool dead{}; bool valid{}; bool armorValid{}; };
struct ScreenPoint { double x{}, y{}, distance{}; bool visible{}; };
struct Row { Actor actor; ScreenPoint screen; Life life; bool hasLife{}; };
struct Snapshot { Camera camera; std::vector<Actor> actors; std::vector<Addr> levels; Addr world{}; Addr cameraManager{}; std::uint64_t generation{}; };
struct Config {
    bool active{true}, hideDead{true}, showHp{true}, showBars{true}, showArmor{true};
    bool hostile{true}, narrative{true}, container{true}, dropped{true}, micro{true}, resource{true};
    bool focusOnly{true};
    int skeletonMode{0}; // 0=off, 1=numbered points, 2=points and tentative links
    int maxLabels{45}, fontSize{11}, barWidth{62}, barHeight{7};
    int maxDistance{3500}, lootDistance{1800};
    int scanIntervalSeconds{12};
    bool enabled(Category c) const {
        switch(c) {case Category::Hostile:return hostile;case Category::NarrativeNpc:return narrative;
        case Category::Container:return container;case Category::Dropped:return dropped;
        case Category::MicroLoot:return micro;case Category::ResourceNode:return resource;
        default:return false;}
    }
};
inline bool validPtr(Addr a) {return a>=0x10000 && a<0x0000800000000000ULL && !(a&7);}
inline bool verifiedLife(const std::string& n) {
    return n=="NPC_Monster_Pest_C" || n=="NPC_Monster_Peccary_C" ||
           n=="NPC_Monster_Carbuncle_C" || n=="NPC_Robot_Security_C";
}
inline float baseHP(const std::string& n) {
    if(n=="NPC_Monster_Pest_C" || n=="NPC_Monster_Carbuncle_C") return 25;
    if(n=="NPC_Monster_Peccary_C") return 80;
    if(n=="NPC_Robot_Security_C") return 180;
    return 0;
}
inline const char* categoryName(Category c) {
    switch(c) {case Category::Hostile:return "Hostile";case Category::NarrativeNpc:return "NPC";
        case Category::Container:return "Container";case Category::Dropped:return "Dropped";
        case Category::MicroLoot:return "Micro loot";case Category::ResourceNode:return "Resource";
        default:return "Unknown";}
}
}
