#pragma once
#include "ue5.hpp"
#include <cstdint>
#include <string>
namespace abiotic {
struct CustomDepthTrial {
    Addr actor{},mesh{},byteAddress{};
    std::uint8_t original{};
    DWORD pid{};
    ULONGLONG deadline{};
    bool active{};
};
bool startCustomDepthTrial(UE5& ue,const Process& process,Addr actor,
                           CustomDepthTrial& trial,std::string& log);
bool restoreCustomDepthTrial(UE5& ue,const Process& process,
                             CustomDepthTrial& trial,std::string& log);
}
