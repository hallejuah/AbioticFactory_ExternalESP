#include "abiotic/settings.hpp"
#include <fstream>
#include <string>
#include <algorithm>
namespace abiotic {
std::filesystem::path settingsPath(){
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    return std::filesystem::path(path).parent_path()/L"abiotic_cpp_settings.ini";
}
void loadSettings(Config& c){
    auto p=settingsPath();auto s=p.wstring();
    auto b=[&](const wchar_t* key,bool& v){v=GetPrivateProfileIntW(L"ESP",key,v?1:0,s.c_str())!=0;};
    auto n=[&](const wchar_t* key,int& v,int min,int max){v=std::clamp((int)GetPrivateProfileIntW(L"ESP",key,v,s.c_str()),min,max);};
    b(L"hostile",c.hostile);b(L"narrative",c.narrative);b(L"container",c.container);
    b(L"dropped",c.dropped);b(L"micro",c.micro);b(L"resource",c.resource);
    b(L"showHp",c.showHp);b(L"hideDead",c.hideDead);b(L"showBars",c.showBars);
    b(L"showArmor",c.showArmor);b(L"focusOnly",c.focusOnly);
    b(L"skeletonDebug",c.skeletonDebug);
    b(L"skeletonDebug",c.skeletonDebug);
    n(L"maxLabels",c.maxLabels,1,250);n(L"maxDistance",c.maxDistance,100,50000);
    n(L"lootDistance",c.lootDistance,100,50000);n(L"fontSize",c.fontSize,8,25);
    n(L"barWidth",c.barWidth,24,240);n(L"barHeight",c.barHeight,3,24);
    n(L"scanIntervalSeconds",c.scanIntervalSeconds,3,120);
}
void saveSettings(const Config& c){
    auto s=settingsPath().wstring();
    auto b=[&](const wchar_t* key,bool value){WritePrivateProfileStringW(L"ESP",key,value?L"1":L"0",s.c_str());};
    auto n=[&](const wchar_t* key,int value){auto v=std::to_wstring(value);WritePrivateProfileStringW(L"ESP",key,v.c_str(),s.c_str());};
    b(L"hostile",c.hostile);b(L"narrative",c.narrative);b(L"container",c.container);
    b(L"dropped",c.dropped);b(L"micro",c.micro);b(L"resource",c.resource);
    b(L"showHp",c.showHp);b(L"hideDead",c.hideDead);b(L"showBars",c.showBars);
    b(L"showArmor",c.showArmor);b(L"focusOnly",c.focusOnly);
    n(L"maxLabels",c.maxLabels);n(L"maxDistance",c.maxDistance);n(L"lootDistance",c.lootDistance);
    n(L"fontSize",c.fontSize);n(L"barWidth",c.barWidth);n(L"barHeight",c.barHeight);
    n(L"scanIntervalSeconds",c.scanIntervalSeconds);
}
}
