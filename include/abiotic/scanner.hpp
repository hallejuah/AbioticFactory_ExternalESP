#pragma once
#include "ue5.hpp"
#include <string>
namespace abiotic {
class Scanner {
public:
    explicit Scanner(UE5& ue):ue_(ue){}
    bool refresh(std::string& error);
    Snapshot& data(){return current_;}
    const Snapshot& data()const{return current_;}
    bool identityChanged() const;
    void reset(){current_=Snapshot{};signature_.clear();}
private:
    UE5& ue_;
    Snapshot current_;
    std::vector<std::pair<Addr,TArray>> signature_;
};
}
