#include "abiotic/process.hpp"
#include <TlHelp32.h>
#include <bcrypt.h>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>
#pragma comment(lib,"bcrypt.lib")
namespace abiotic {
namespace {
constexpr auto GameName=L"AbioticFactor-Win64-Shipping.exe";
constexpr auto RequiredSha="b5fac7c4d5b8a678c58731e4e44bc96f7f598748a92906740b2cfab743b177c1";
struct Handle {HANDLE h;explicit Handle(HANDLE h_):h(h_){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
std::string hashFile(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE alg=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    std::vector<UCHAR> object;
    std::vector<UCHAR> digest;
    auto cleanup=[&]{if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);};
    try {
        if(!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)))throw std::runtime_error("BCrypt SHA256 init");
        ULONG len=0,done=0;
        if(!BCRYPT_SUCCESS(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&len,sizeof(len),&done,0)))throw std::runtime_error("SHA object len");
        object.resize(len);
        if(!BCRYPT_SUCCESS(BCryptGetProperty(alg,BCRYPT_HASH_LENGTH,(PUCHAR)&len,sizeof(len),&done,0)))throw std::runtime_error("SHA digest len");
        digest.resize(len);
        if(!BCRYPT_SUCCESS(BCryptCreateHash(alg,&hash,object.data(),(ULONG)object.size(),nullptr,0,0)))throw std::runtime_error("SHA create hash");
        std::ifstream in(path,std::ios::binary);
        if(!in)throw std::runtime_error("Cannot open game executable for SHA256 verification");
        std::vector<char> buf(1024*1024);
        while(in) {
            in.read(buf.data(),buf.size());
            auto n=in.gcount();
            if(n>0 && !BCRYPT_SUCCESS(BCryptHashData(hash,(PUCHAR)buf.data(),(ULONG)n,0)))throw std::runtime_error("SHA hash read");
        }
        if(in.bad())throw std::runtime_error("Game executable read failed");
        if(!BCRYPT_SUCCESS(BCryptFinishHash(hash,digest.data(),(ULONG)digest.size(),0)))throw std::runtime_error("SHA final");
        std::ostringstream os; for(auto b:digest)os<<std::hex<<std::setfill('0')<<std::setw(2)<<(int)b;
        cleanup();return os.str();
    }catch(...){cleanup();throw;}
}
}
Process::~Process(){detach();}
void Process::detach(){if(handle_){CloseHandle(handle_);handle_=nullptr;}pid_=0;base_=0;exe_.clear();resetCounters();}
void Process::attach(){
    detach();
    Handle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(processes.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Process snapshot failed");
    PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);
    DWORD found=0;
    if(Process32FirstW(processes.h,&pe))do{
        if(_wcsicmp(pe.szExeFile,GameName)==0){if(found)throw std::runtime_error("Multiple matching game processes");found=pe.th32ProcessID;}
    }while(Process32NextW(processes.h,&pe));
    if(!found)throw std::runtime_error("Abiotic Factor process not found");
    Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,found));
    if(modules.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Module snapshot failed");
    MODULEENTRY32W me{};me.dwSize=sizeof(me);Addr foundBase=0;std::filesystem::path path;
    if(Module32FirstW(modules.h,&me))do{
        if(_wcsicmp(me.szModule,GameName)==0){foundBase=(Addr)me.modBaseAddr;path=me.szExePath;break;}
    }while(Module32NextW(modules.h,&me));
    if(!foundBase)throw std::runtime_error("Game module not found");
    if(hashFile(path)!=RequiredSha)throw std::runtime_error("Game SHA256 mismatch: this build is not supported");
    HANDLE h=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,found);
    if(!h)throw std::runtime_error("OpenProcess read-only failed");
    handle_=h;pid_=found;base_=foundBase;exe_=path;
}
bool Process::read(Addr address,void* output,size_t size) const {
    if(!handle_||address<0x10000||address>=0x0000800000000000ULL||size==0||size>65536){
        readFailures_.fetch_add(1,std::memory_order_relaxed);return false;
    }
    SIZE_T got=0;
    const bool ok=ReadProcessMemory(handle_,reinterpret_cast<LPCVOID>(address),output,size,&got) && got==size;
    if(!ok)readFailures_.fetch_add(1,std::memory_order_relaxed);
    return ok;
}
bool Process::alive() const {DWORD exitCode=0;return handle_ && GetExitCodeProcess(handle_,&exitCode) && exitCode==STILL_ACTIVE;}
}
