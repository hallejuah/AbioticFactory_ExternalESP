#pragma once
#include "model.hpp"
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <atomic>
namespace abiotic {
class Process {
public:
    Process()=default;
    ~Process();
    Process(const Process&)=delete;
    Process& operator=(const Process&)=delete;
    void attach();
    void detach();
    bool read(Addr address, void* output, size_t size) const;
    template<class T> std::optional<T> get(Addr address) const {
        T value{}; if(!read(address,&value,sizeof(T))) return std::nullopt; return value;
    }
    Addr pointer(Addr address) const {auto v=get<Addr>(address);return v && validPtr(*v)?*v:0;}
    DWORD pid() const {return pid_;}
    Addr base() const {return base_;}
    const std::filesystem::path& exe() const {return exe_;}
    bool alive() const;
    std::uint64_t readFailures() const {return readFailures_.load(std::memory_order_relaxed);}
    void resetCounters(){readFailures_.store(0,std::memory_order_relaxed);}
private:
    HANDLE handle_{nullptr};
    DWORD pid_{};Addr base_{};
    std::filesystem::path exe_;
    mutable std::atomic<std::uint64_t> readFailures_{0};
};
}
