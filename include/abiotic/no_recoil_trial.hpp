#pragma once
#include "ue5.hpp"
#include <cstdint>
#include <string>
namespace abiotic {
struct NoRecoilTrial {
    Addr character{}, rifle{};
    DWORD pid{};
    std::uint64_t deadline{};
    unsigned writes{};
    bool active{};
};
bool startNoRecoilTrial(UE5&,const Process&,NoRecoilTrial&,std::uint64_t,std::string&);
bool tickNoRecoilTrial(UE5&,const Process&,NoRecoilTrial&,std::uint64_t,std::string&);
void stopNoRecoilTrial(NoRecoilTrial&,std::string&);
}
