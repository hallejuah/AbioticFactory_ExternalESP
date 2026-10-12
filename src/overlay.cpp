#include "abiotic/overlay.hpp"
#include "abiotic/mesh_probe.hpp"
#include "abiotic/outline_probe.hpp"
#include "abiotic/reflection_probe.hpp"
#include "abiotic/property_chain.hpp"
#include "abiotic/property_values.hpp"
#include "abiotic/bone_probe.hpp"
#include "abiotic/native_outline.hpp"
#include "abiotic/bool_masks.hpp"
#include "abiotic/render_pipeline.hpp"
#include "abiotic/outline_survey.hpp"
#include "abiotic/weapon_research.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <CommCtrl.h>
#include <dwmapi.h>
namespace abiotic {
namespace {
constexpr COLORREF Transparent=RGB(1,2,3);
constexpr int HotInterval=16; // ~60 Hz projection / paint
constexpr int ID_TAB_BASE=500, ID_CHECK_BASE=550, ID_DEC_BASE=650, ID_INC_BASE=750;
struct Box {RECT r;};
BOOL CALLBACK enumGame(HWND hwnd,LPARAM param){
    auto* hit=(std::pair<DWORD,HWND>*)param;
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(pid!=hit->first||!IsWindowVisible(hwnd))return TRUE;
    RECT r{};GetClientRect(hwnd,&r);
    if(r.right-r.left>=640&&r.bottom-r.top>=360){
        if(!hit->second)hit->second=hwnd;
        else {RECT old{};GetClientRect(hit->second,&old);
            if((r.right-r.left)*(r.bottom-r.top)>(old.right-old.left)*(old.bottom-old.top))hit->second=hwnd;}
    }return TRUE;
}
HWND gameWindow(DWORD pid){std::pair<DWORD,HWND> x{pid,nullptr};EnumWindows(enumGame,(LPARAM)&x);return x.second;}
COLORREF color(Category cat){
    switch(cat){case Category::Hostile:return RGB(255,104,95);case Category::NarrativeNpc:return RGB(114,229,255);
    case Category::Container:return RGB(247,214,108);case Category::Dropped:return RGB(144,255,155);
    case Category::MicroLoot:return RGB(107,255,206);case Category::ResourceNode:return RGB(188,161,255);
    default:return RGB(255,255,255);}
}
std::wstring humanName(const Actor& actor){
    if(actor.category==Category::Dropped)return L"Dropped Item (?)";
    std::string s=actor.name;
    for(auto prefix: {"NPC_Monster_","NPC_Robot_","Resource_MicroNode_","ResourceNode_","Deployed_Container_","Container_"}){
        auto len=strlen(prefix);if(s.rfind(prefix,0)==0){s=s.substr(len);break;}
    }
    if(s.size()>2&&s.ends_with("_C"))s.resize(s.size()-2);
    std::replace(s.begin(),s.end(),'_',' ');
    return std::wstring(s.begin(),s.end());
}
HWND control(HWND parent,const wchar_t* type,const std::wstring& text,int id,int x,int y,int w,int h,DWORD style=0){
    return CreateWindowExW(0,type,text.c_str(),WS_VISIBLE|WS_CHILD|style,x,y,w,h,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
}
std::wstring formatNum(double n,int decimals=0){std::wostringstream ss;ss<<std::fixed<<std::setprecision(decimals)<<n;return ss.str();}
}
Overlay::Overlay(HINSTANCE instance):instance_(instance),ue_(proc_),scanner_(ue_){loadSettings(cfg_);}
int Overlay::run(){
    // Keep the UI alive when the game has not started or has been restarted.
    if(!initialize())return 1;
    connected_=false;
    status_=L"Waiting for Abiotic Factor...";
    SetTimer(overlay_,1,HotInterval,nullptr);
    reconnect();
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    saveSettings(cfg_);
    return (int)msg.wParam;
}
bool Overlay::reconnect(){
    const auto now=GetTickCount64();
    if(lastReconnectAttempt_ && now-lastReconnectAttempt_<3000)return false;
    lastReconnectAttempt_=now;
    cancelWeaponSampling(weaponSample_);
    proc_.detach();
    ue_.clearCaches();
    scanner_.reset();
    rows_.clear();actorSamples_.clear();lastActorSample_=0;
    boneDebugPoints_.clear();lastBoneSample_=0;
    game_=nullptr;
    gameRect_={};
    connected_=false;
    try {
        proc_.attach();  // Includes SHA256 verification before read-only OpenProcess.
        game_=gameWindow(proc_.pid());
        if(!game_)throw std::runtime_error("Game window not found");
        connected_=true;
        ++reconnectCount_;
        lastScan_=0;
        lastChainCheck_=0;
        scanRequested_=true;
        geometry();
        status_=L"Connected to game PID "+std::to_wstring(proc_.pid());
        OutputDebugStringW((L"Abiotic ESP: "+status_+L"\n").c_str());
        return true;
    } catch(const std::exception& e){
        const std::string what=e.what();
        OutputDebugStringA(("Abiotic ESP: reconnect waiting: "+what+"\n").c_str());
        proc_.detach();
        game_=nullptr;
        status_=L"Waiting for game (check SHA256 and window)";
        return false;
    }
}

bool Overlay::initialize(){
    // Geometry uses physical pixels to match the game client coordinates.
    SetProcessDPIAware();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance=instance_;wc.lpfnWndProc=wndproc;wc.lpszClassName=L"AbioticExternalESPOverlay";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=CreateSolidBrush(Transparent);
    if(!RegisterClassExW(&wc))return false;
    overlay_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
        wc.lpszClassName,L"Abiotic External ESP",WS_POPUP,0,0,800,600,nullptr,nullptr,instance_,this);
    if(!overlay_)return false;
    if(!SetLayeredWindowAttributes(overlay_,Transparent,255,LWA_COLORKEY))return false;
    geometry();ShowWindow(overlay_,SW_SHOWNOACTIVATE);UpdateWindow(overlay_);
    return true;
}
LRESULT CALLBACK Overlay::wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* self=(Overlay*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_NCCREATE){self=(Overlay*)((CREATESTRUCTW*)lp)->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)self);}
    if(!self)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
        case WM_TIMER:if(hwnd==self->overlay_)self->tick();return 0;
        case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
            if(hwnd==self->overlay_)self->paint(dc);
            EndPaint(hwnd,&ps);return 0;}
        case WM_COMMAND:self->onCommand(wp,lp);return 0;
        case WM_HSCROLL:self->onScroll(wp,lp);return 0;
        case WM_CLOSE:if(hwnd==self->panel_){self->showPanel();return 0;}
            if(hwnd==self->overlay_){self->restoreTrial();if(self->customDepthTrial_.active)return 0;}
            DestroyWindow(hwnd);return 0;
        case WM_DESTROY:if(hwnd==self->overlay_){self->restoreTrial();PostQuitMessage(0);}return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void Overlay::geometry(){
    if(!game_||!IsWindow(game_))return;
    RECT client{};POINT p{0,0};
    if(!GetClientRect(game_,&client)||!ClientToScreen(game_,&p))return;
    RECT r{p.x,p.y,p.x+client.right,p.y+client.bottom};
    if(r.left!=gameRect_.left||r.top!=gameRect_.top||r.right!=gameRect_.right||r.bottom!=gameRect_.bottom){
        gameRect_=r;
        SetWindowPos(overlay_,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,
            SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    if(panelOpen_&&panel_){
        int px=r.right-365,py=r.top+50;
        SetWindowPos(panel_,HWND_TOPMOST,px,py,350,455,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
}
bool Overlay::focused()const{
    HWND front=GetForegroundWindow();if(!front)return false;
    DWORD pid=0;GetWindowThreadProcessId(front,&pid);
    return pid==proc_.pid()||pid==GetCurrentProcessId();
}
void Overlay::toggleCategory(Category cat){
    switch(cat){case Category::ResourceNode:cfg_.resource=!cfg_.resource;break;
      case Category::MicroLoot:cfg_.micro=!cfg_.micro;break;
      case Category::Dropped:cfg_.dropped=!cfg_.dropped;break;default:break;}
    if(panelOpen_)updatePanel();
}
void Overlay::hotkeys(){
    auto edge=[&](int k){bool down=(GetAsyncKeyState(k)&0x8000)!=0;bool pressed=down&&!held_[k];held_[k]=down;return pressed;};
    if(edge(VK_END)){restoreTrial();if(customDepthTrial_.active)return;DestroyWindow(overlay_);return;}
    if(edge(VK_F1))probeRecoilTrial();
    if(edge(VK_F3))toggleCategory(Category::ResourceNode);
    if(edge(VK_F4))toggleCategory(Category::MicroLoot);
    if(edge(VK_F5))toggleCategory(Category::Dropped);
    if(edge(VK_F6))cfg_.showHp=!cfg_.showHp;
    if(edge(VK_F7))cfg_.hideDead=!cfg_.hideDead;
    if(edge(VK_F8))cfg_.active=!cfg_.active;
    if(edge(VK_F9))scanRequested_=true;
    if(edge(VK_F10))showPanel();
    if(edge(VK_F11))probeNeedleRifle(); // read-only needle rifle research
    // v1.7: disable active memory trials while researching render pipeline.
    // F12 outline research disabled in clean Box ESP.
}
void Overlay::probeNeedleRifle(){
    if(!connected_||weaponSample_.active)return;
    std::string reason;
    const bool started=startWeaponSampling(ue_,weaponSample_,GetTickCount64(),reason);
    OutputDebugStringA(("QuillRifle sampler: "+reason+"\n").c_str());
    status_=started?L"QuillRifle F11: capturing for 2 seconds":
        L"QuillRifle F11: equip rifle / check game connection";
}
void Overlay::probeNearestEnemy(){
    if(!connected_||rows_.empty())return;
    // Probe one visible hostile closest to crosshair on demand (F11).
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5;
        const double dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=probeActorComponents(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendProbeReport(result);
    OutputDebugStringA(result.report.c_str());
    status_=saved?L"Mesh probe saved: abiotic_mesh_probe.txt":L"Mesh probe file write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestOutline(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5;
        const double dy=row.screen.y-h*0.5;
        const double distance=dx*dx+dy*dy;
        if(distance<best){best=distance;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=inspectOutlineComponent(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendOutlineProbe(result);
    OutputDebugStringA(result.report.c_str());
    status_=saved?L"Outline report saved: abiotic_outline_probe.txt":L"Cannot write outline report";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestReflection(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left, h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5,dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=inspectReflection(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendReflectionReport(result);
    OutputDebugStringA(result.report.c_str());
    status_=saved?L"Reflection report saved: abiotic_reflection_probe.txt":L"Reflection report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestProperties(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5;
        const double dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=probePropertyChains(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendPropertyChain(result);
    OutputDebugStringA(result.text.c_str());
    status_=saved?L"Property candidates saved: abiotic_property_chain.txt":L"Cannot write property report";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestPropertyValues(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5,dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=inspectPropertyValues(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendPropertyValuesReport(result);
    OutputDebugStringA(result.report.c_str());
    status_=saved?L"Property report saved: abiotic_property_values.txt":L"Property report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestBones(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife && row.life.dead)continue;
        const double dx=row.screen.x-w*0.5;
        const double dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=inspectBoneTransforms(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendBoneProbe(result);
    OutputDebugStringA(result.text.c_str());
    status_=saved?L"Bone report saved: abiotic_bone_transforms.txt":L"Bone report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestNativeOutline(){
    if(!connected_ || rows_.empty())return;
    const int width=gameRect_.right-gameRect_.left;
    const int height=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife&&row.life.dead)continue;
        const double dx=row.screen.x-width*0.5,dy=row.screen.y-height*0.5;
        const double dist=dx*dx+dy*dy;
        if(dist<best){best=dist;chosen=&row;}
    }
    if(!chosen)return;
    const auto result=inspectNativeOutline(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendNativeOutlineReport(result);
    OutputDebugStringA(result.text.c_str());
    status_=saved?L"Native outline report: abiotic_native_outline.txt":L"Native outline report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeNearestBoolMasks(){
    if(!connected_ || rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* selected=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife&&row.life.dead)continue;
        const double dx=row.screen.x-w*0.5;
        const double dy=row.screen.y-h*0.5;
        const double d=dx*dx+dy*dy;
        if(d<best){best=d;selected=&row;}
    }
    if(!selected)return;
    const auto report=inspectBoolMasks(ue_,selected->actor.address,selected->actor.name);
    const bool saved=appendBoolMaskReport(report);
    OutputDebugStringA(report.text.c_str());
    status_=saved?L"Bool report saved: abiotic_bool_masks.txt":L"Bool report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::runCustomDepthTrial(){
    if(!connected_ || customDepthTrial_.active)return;
    if(rows_.empty())return;
    const int w=gameRect_.right-gameRect_.left;
    const int h=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.name!="NPC_Monster_Pest_C" || (row.hasLife&&row.life.dead))continue;
        const double dx=row.screen.x-w*0.5,dy=row.screen.y-h*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    std::string note;
    const bool started=startCustomDepthTrial(ue_,proc_,chosen->actor.address,customDepthTrial_,note);
    OutputDebugStringA(("CustomDepth trial: "+note+"\n").c_str());
    {std::ofstream out(settingsPath().parent_path()/L"abiotic_customdepth_test.txt",std::ios::app);
     out<<"tick="<<GetTickCount64()<<" start="<<(started?"yes":"no")<<" "<<note<<"\n";}
    status_=started?L"CustomDepth trial ON (5 sec)":customDepthTrial_.active?
        L"CustomDepth recovery pending; do not exit":L"CustomDepth trial refused; see log";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::restoreTrial(){
    if(!customDepthTrial_.active)return;
    std::string note;
    const bool restored=restoreCustomDepthTrial(ue_,proc_,customDepthTrial_,note);
    OutputDebugStringA(("CustomDepth restore: "+note+"\n").c_str());
    {std::ofstream out(settingsPath().parent_path()/L"abiotic_customdepth_test.txt",std::ios::app);
     out<<"tick="<<GetTickCount64()<<" restored="<<(restored?"yes":"no")<<" "<<note<<"\n";}
    if(restored)status_=L"CustomDepth original bit restored";
    else status_=L"CustomDepth restore pending/failed; see log";
}
void Overlay::probeNearestRenderPipeline(){
    if(!connected_ || rows_.empty())return;
    const int width=gameRect_.right-gameRect_.left;
    const int height=gameRect_.bottom-gameRect_.top;
    const Row* chosen=nullptr;
    double best=1.0e100;
    for(const auto& row:rows_){
        if(row.actor.category!=Category::Hostile)continue;
        if(row.hasLife&&row.life.dead)continue;
        const double dx=row.screen.x-width*0.5;
        const double dy=row.screen.y-height*0.5;
        const double score=dx*dx+dy*dy;
        if(score<best){best=score;chosen=&row;}
    }
    if(!chosen)return;
    const auto report=inspectRenderPipeline(ue_,chosen->actor.address,chosen->actor.name);
    const bool saved=appendRenderPipelineReport(report);
    OutputDebugStringA(report.text.c_str());
    status_=saved?L"Render pipeline report: abiotic_render_pipeline.txt":L"Render report write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::probeOutlineSurvey(){
    if(!connected_||scanner_.data().actors.empty())return;
    const auto report=surveyOutlineActivation(ue_,scanner_.data(),outlineSurveyState_);
    const bool saved=appendOutlineSurvey(report);
    OutputDebugStringA(report.text.c_str());
    status_=saved?(L"Outline survey "+std::to_wstring(report.nextIndex)+L"/"+
        std::to_wstring(report.total)+L": abiotic_outline_survey.txt"):
        L"Outline survey write failed";
    if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
}
void Overlay::tick(){
    if(weaponSample_.active){
        std::string summary;
        if(stepWeaponSampling(ue_,weaponSample_,GetTickCount64(),summary)){
            const bool saved=appendWeaponSamples(weaponSample_);
            status_=saved?L"QuillRifle samples saved to CSV":
                L"QuillRifle capture complete; CSV save failed";
            OutputDebugStringA(("QuillRifle sampler: "+summary+"\n").c_str());
        }
    }
    if(customDepthTrial_.active && GetTickCount64()>=customDepthTrial_.deadline)restoreTrial();
    hotkeys();
    if(!IsWindow(overlay_))return;
    const ULONGLONG now=GetTickCount64();
    if(connected_&&(!proc_.alive()||!IsWindow(game_))){
        restoreTrial();
        // Never detach a live process while its restoration remains pending.
        if(customDepthTrial_.active)return;
        cancelWeaponSampling(weaponSample_);
        connected_=false;game_=nullptr;gameRect_={};
        scanner_.reset();ue_.clearCaches();rows_.clear();actorSamples_.clear();lastActorSample_=0;
        outlineSurveyState_=OutlineSurveyState{};
        boneDebugPoints_.clear();lastBoneSample_=0;
        proc_.detach();scanRequested_=false;
        status_=L"Game closed; waiting to reconnect";
    }
    if(!connected_){
        reconnect();
        InvalidateRect(overlay_,nullptr,FALSE);
        if(!connected_)return;
    }
    geometry();
    if(now-lastChainCheck_>=3000){
        lastChainCheck_=now;
        if(scanner_.identityChanged()){scanRequested_=true;rows_.clear();actorSamples_.clear();lastActorSample_=0;}
    }
    if(scanRequested_||now-lastScan_>=(ULONGLONG)cfg_.scanIntervalSeconds*1000){
        std::string err;
        const ULONGLONG t0=GetTickCount64();
        const bool success=scanner_.refresh(err);
        scanLastMs_=static_cast<double>(GetTickCount64()-t0);
        ++scanCount_;
        scanAvgMs_+=(scanLastMs_-scanAvgMs_)/static_cast<double>(scanCount_);
        lastScan_=now;scanRequested_=false;
        if(success){
            actorSamples_.clear();lastActorSample_=0;
            status_=L"Actors: "+std::to_wstring(scanner_.data().actors.size())+L" | scan OK";
        }else{
            ++scanErrors_;
            status_=L"Streaming / retry on next scan";
            OutputDebugStringA(("Abiotic ESP scan: "+err+"\n").c_str());
        }
    }
    if(now-lastStats_>=5000){
        lastStats_=now;
        readErrors_=proc_.readFailures();
        std::wostringstream metrics;
        metrics<<L"AbioticESP [PID "<<proc_.pid()<<L"] actors="<<scanner_.data().actors.size()
               <<L" scans="<<scanCount_<<L" scan_last_ms="<<scanLastMs_
               <<L" scan_avg_ms="<<std::fixed<<std::setprecision(1)<<scanAvgMs_
               <<L" scan_errors="<<scanErrors_<<L" read_errors="<<readErrors_
               <<L" reconnects="<<reconnectCount_<<L"\n";
        OutputDebugStringW(metrics.str().c_str());
        if(panel_&&panelOpen_)SetWindowTextW(panel_,(L"Abiotic ESP | "+status_).c_str());
    }
    rows_.clear();
    const int w=gameRect_.right-gameRect_.left,h=gameRect_.bottom-gameRect_.top;
    auto& snapshot=scanner_.data();
    Camera cam{};
    if(!cfg_.active||w<=0||h<=0||(cfg_.focusOnly&&!focused())||
       !snapshot.cameraManager||!ue_.readCamera(snapshot.cameraManager,cam)){
        InvalidateRect(overlay_,nullptr,FALSE);return;
    }
    // Position/health samples are refreshed at 30 Hz, while the latest camera is
    // sampled and reprojected at ~60 Hz to reduce camera-motion label stepping.
    const bool sampleActors = !lastActorSample_ || now-lastActorSample_>=33;
    if(sampleActors)lastActorSample_=now;
    for(const auto& actor:snapshot.actors){
        if(!cfg_.enabled(actor.category))continue;
        auto& sample=actorSamples_[actor.address];
        if(sampleActors || !sample.valid){
            Vec3 pos{};
            sample.valid=ue_.actorPos(actor.address,pos);
            if(sample.valid){
                sample.pos=pos;
                sample.life=Life{};
                if(actor.category==Category::Hostile&&verifiedLife(actor.name))
                    sample.life=ue_.life(actor.address,actor.name=="NPC_Robot_Security_C");
            }
        }
        if(!sample.valid)continue;
        ScreenPoint pt=project(cam,sample.pos,w,h);
        if(!pt.visible)continue;
        bool loot=actor.category==Category::Dropped||actor.category==Category::MicroLoot||actor.category==Category::ResourceNode;
        if(pt.distance>(loot?cfg_.lootDistance:cfg_.maxDistance))continue;
        Row row;row.actor=actor;row.screen=pt;
        if(actor.category==Category::Hostile){
            // Approximate vertical bounds; not a per-model silhouette or collision box.
            const double nominalHeight=
                actor.name=="NPC_Monster_Pest_C"?85.0:
                actor.name=="NPC_Monster_Carbuncle_C"?100.0:
                actor.name=="NPC_Monster_Peccary_C"?155.0:
                actor.name=="NPC_Robot_Security_C"?190.0:165.0;
            const Vec3 upper{sample.pos.x,sample.pos.y,sample.pos.z+nominalHeight};
            row.boxTop=project(cam,upper,w,h);
            row.hasBoxTop=row.boxTop.visible;
        }
        if(actor.category==Category::Hostile){
            row.life=sample.life;row.hasLife=row.life.valid;
            if(cfg_.hideDead&&verifiedLife(actor.name)&&row.hasLife&&row.life.hp<=0.001&&row.life.dead)continue;
        }
        rows_.push_back(std::move(row));
    }
    std::sort(rows_.begin(),rows_.end(),[](const Row& a,const Row& b){
        auto priority=[](Category c){return c==Category::Hostile?0:c==Category::NarrativeNpc?1:
            (c==Category::Dropped||c==Category::MicroLoot||c==Category::ResourceNode)?2:3;};
        auto x=priority(a.actor.category),y=priority(b.actor.category);
        return x==y?a.screen.distance<b.screen.distance:x<y;
    });
    if((int)rows_.size()>cfg_.maxLabels)rows_.resize(cfg_.maxLabels);
    cfg_.skeletonMode=0;
    boneDebugPoints_.clear();
    InvalidateRect(overlay_,nullptr,FALSE);
}
void Overlay::paint(HDC dc){
    RECT r{};GetClientRect(overlay_,&r);
    HBRUSH transparent=CreateSolidBrush(Transparent);FillRect(dc,&r,transparent);DeleteObject(transparent);
    SetBkMode(dc,TRANSPARENT);
    if(!cfg_.active||!focused()&&cfg_.focusOnly)return;
    for(auto& row:rows_)drawRow(dc,row);
    // Skeleton display disabled in v2.0 clean box mode.
}
void Overlay::drawBoneDebug(HDC dc){
    // The links are guessed from the observed Pest screenshot. NOT UE bone parents.
    if(cfg_.skeletonMode==2){
        constexpr std::array<std::pair<int,int>,9> links{{{0,1},{1,2},{2,3},{3,4},
            {4,5},{4,6},{5,7},{6,8},{2,9}}};
        std::array<const BoneDebugPoint*,10> indexed{};
        for(const auto& point:boneDebugPoints_)
            if(point.index>=0 && point.index<10 && point.screen.visible)
                indexed[static_cast<size_t>(point.index)]=&point;
        HPEN pen=CreatePen(PS_SOLID,2,RGB(255,195,77));
        HGDIOBJ previous=SelectObject(dc,pen);
        for(const auto [from,to]:links){
            const auto* a=indexed[static_cast<size_t>(from)];
            const auto* b=indexed[static_cast<size_t>(to)];
            if(!a || !b)continue;
            const double dx=a->screen.x-b->screen.x;
            const double dy=a->screen.y-b->screen.y;
            // Prevent huge erroneous cross-screen lines while geometry is experimental.
            if(dx*dx+dy*dy>400.0*400.0)continue;
            MoveToEx(dc,static_cast<int>(std::lround(a->screen.x)),
                static_cast<int>(std::lround(a->screen.y)),nullptr);
            LineTo(dc,static_cast<int>(std::lround(b->screen.x)),
                static_cast<int>(std::lround(b->screen.y)));
        }
        SelectObject(dc,previous);
        DeleteObject(pen);
    }
    auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
    SetDCPenColor(dc,RGB(102,255,245));
    auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    for(const auto& point:boneDebugPoints_){
        const int x=static_cast<int>(std::lround(point.screen.x));
        const int y=static_cast<int>(std::lround(point.screen.y));
        Ellipse(dc,x-4,y-4,x+5,y+5);
        const std::wstring n=std::to_wstring(point.index);
        SetTextColor(dc,RGB(102,255,245));
        TextOutW(dc,x+7,y-7,n.c_str(),static_cast<int>(n.size()));
    }
    SelectObject(dc,oldBrush);
    SelectObject(dc,oldPen);
}
void Overlay::drawRow(HDC dc,const Row& row){
    auto x=(int)std::lround(row.screen.x),y=(int)std::lround(row.screen.y);
    COLORREF c=color(row.actor.category);
    HPEN pen=CreatePen(PS_SOLID,2,c);auto oldPen=SelectObject(dc,pen);
    auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    if(row.actor.category==Category::Hostile){
        // Root-to-head projected approximate 2D box. Unknown creature proportions
        // are estimates until real bounds can be identified.
        const int height=std::clamp(
            row.hasBoxTop?std::abs(y-static_cast<int>(std::lround(row.boxTop.y))):44,
            24,360);
        const int width=std::clamp(static_cast<int>(std::lround(height*0.58)),18,230);
        const int top=row.hasBoxTop?std::min(y,static_cast<int>(std::lround(row.boxTop.y))):y-height/2;
        Rectangle(dc,x-width/2,top,x+(width+1)/2,top+height);
    }else Ellipse(dc,x-3,y-3,x+3,y+3);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    std::wstring text=humanName(row.actor)+L" "+formatNum(row.screen.distance/100.0)+L"m";
    bool hostile=row.actor.category==Category::Hostile;
    if(cfg_.showHp&&hostile){
        if(row.hasLife){
            float max=baseHP(row.actor.name);
            text+=L" HP:"+formatNum(row.life.hp,1);
            if(max>0)text+=L"/"+formatNum(max);
            if(row.actor.name=="NPC_Robot_Security_C"&&cfg_.showArmor&&row.life.armorValid)
                text+=L" Armor:"+formatNum(row.life.armor,1); // No guessed max armor.
        }else text+=L" HP:?";
    }
    HFONT font=CreateFontW(-cfg_.fontSize*2,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto oldFont=SelectObject(dc,font);
    SIZE sz{};GetTextExtentPoint32W(dc,text.c_str(),(int)text.size(),&sz);
    const int viewportWidth = static_cast<int>(gameRect_.right - gameRect_.left);
    const int labelWidth = static_cast<int>(sz.cx);
    int tx = std::clamp(x - labelWidth / 2, 2, std::max(2, viewportWidth - labelWidth - 2));
    int ty=(y>70)?y-35:y+18;
    SetTextColor(dc,RGB(15,15,15));TextOutW(dc,tx+1,ty+1,text.c_str(),(int)text.size());
    SetTextColor(dc,c);TextOutW(dc,tx,ty,text.c_str(),(int)text.size());
    SelectObject(dc,oldFont);DeleteObject(font);
    if(!cfg_.showHp||!cfg_.showBars||!hostile||!row.hasLife)return;
    float maximum=baseHP(row.actor.name);
    if(maximum<=0)return;
    float fraction=std::clamp(row.life.hp/maximum,0.0f,1.0f);
    int ww=cfg_.barWidth,hh=cfg_.barHeight;
    int left = std::clamp(x - ww / 2, 2, std::max(2, viewportWidth - ww - 2));
    int top=ty+sz.cy+3;
    RECT back{left,top,left+ww,top+hh};
    HBRUSH brush=CreateSolidBrush(RGB(35,35,35));FillRect(dc,&back,brush);DeleteObject(brush);
    RECT fill{left+1,top+1,left+1+(int)((ww-2)*fraction),top+hh-1};
    COLORREF hc=fraction>0.6?RGB(88,231,121):fraction>0.3?RGB(255,209,90):RGB(255,100,100);
    brush=CreateSolidBrush(hc);FillRect(dc,&fill,brush);DeleteObject(brush);
}
void Overlay::showPanel(){
    if(!panel_){
        WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance_;wc.lpfnWndProc=wndproc;
        wc.lpszClassName=L"AbioticExternalESPSettings";wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
        wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassExW(&wc);
        panel_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Abiotic ESP | Settings",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,350,455,nullptr,nullptr,instance_,this);
        if(!panel_)return;
    }
    panelOpen_=!panelOpen_;
    if(panelOpen_){geometry();updatePanel();ShowWindow(panel_,SW_SHOWNORMAL);SetForegroundWindow(panel_);}
    else ShowWindow(panel_,SW_HIDE);
}
void Overlay::updatePanel(){
    if(!panel_||!panelOpen_)return;
    for(HWND h:controls_)if(IsWindow(h))DestroyWindow(h);
    controls_.clear();
    auto add=[&](const wchar_t* cls,const std::wstring& s,int id,int x,int y,int w,int h,DWORD style=0){
        HWND hwnd=control(panel_,cls,s,id,x,y,w,h,style);controls_.push_back(hwnd);return hwnd;};
    static constexpr const wchar_t* pages[]={L"Objects",L"Visual",L"Distance",L"System"};
    for(int i=0;i<4;i++)add(L"BUTTON",pages[i],ID_TAB_BASE+i,8+i*83,12,80,31,
        BS_PUSHBUTTON);
    auto chk=[&](const wchar_t* label,int id,bool on,int y){
        auto h=add(L"BUTTON",label,ID_CHECK_BASE+id,18,y,304,26,BS_AUTOCHECKBOX);
        SendMessageW(h,BM_SETCHECK,on?BST_CHECKED:BST_UNCHECKED,0);
    };
    auto step=[&](const wchar_t* label,int id,int value,const wchar_t* unit,int y){
        add(L"STATIC",label,0,16,y,190,22);
        add(L"BUTTON",L"-",ID_DEC_BASE+id,209,y-3,31,25,BS_PUSHBUTTON);
        add(L"STATIC",std::to_wstring(value)+unit,0,246,y,58,22);
        add(L"BUTTON",L"+",ID_INC_BASE+id,304,y-3,31,25,BS_PUSHBUTTON);
    };
    switch(page_){
      case 0:
        chk(L"Enemies",0,cfg_.hostile,58);chk(L"Narrative NPC",1,cfg_.narrative,94);
        chk(L"Containers",2,cfg_.container,130);chk(L"Dropped items",3,cfg_.dropped,166);
        chk(L"Micro loot",4,cfg_.micro,202);chk(L"Resource nodes",5,cfg_.resource,238);break;
      case 1:
        chk(L"Show HP",6,cfg_.showHp,58);chk(L"HP bars",7,cfg_.showBars,94);
        chk(L"Hide dead",8,cfg_.hideDead,130);chk(L"Robot armor value",9,cfg_.showArmor,166);
        step(L"Font size",3,cfg_.fontSize,L"",215);
        step(L"HP bar width",4,cfg_.barWidth,L"px",251);
        step(L"HP bar height",5,cfg_.barHeight,L"px",287);break;
      case 2:
        step(L"ESP distance",0,cfg_.maxDistance/100,L"m",72);
        step(L"Loot distance",1,cfg_.lootDistance/100,L"m",115);
        step(L"Maximum labels",2,cfg_.maxLabels,L"",158);
        add(L"STATIC",L"Values apply immediately.",0,18,226,310,20);break;
      case 3:
        chk(L"Only when game/menu focused",10,cfg_.focusOnly,60);
        add(L"STATIC",L"Clean Box ESP: skeleton probes disabled",0,18,88,310,22);
        add(L"STATIC",cfg_.skeletonMode==2?L"Mode: points + lines":
            cfg_.skeletonMode==1?L"Mode: points only":L"Mode: off",0,35,110,250,19);
        step(L"Scan interval",6,cfg_.scanIntervalSeconds,L"s",130);
        add(L"STATIC",L"F3/F4/F5: loot    F6: HP    F7: dead",0,18,168,310,22);
        add(L"STATIC",L"F8: ESP    F9: rescan    F10: panel",0,18,195,310,22);
        add(L"STATIC",L"End: exit. No game memory writes in v1.7.",0,18,222,310,22);
        add(L"STATIC",L"MaxArmor: research TODO (per robot).",0,18,255,310,22);
        add(L"STATIC",L"F11: 2s QuillRifle samples (read-only)",0,18,330,310,22);
        add(L"STATIC",L"Scans: "+std::to_wstring(scanCount_)+L" / errors: "+std::to_wstring(scanErrors_),0,18,282,310,22);
        add(L"STATIC",L"RPM errors: "+std::to_wstring(proc_.readFailures())+L" / reconnects: "+std::to_wstring(reconnectCount_),0,18,305,310,22);break;
    }
    add(L"BUTTON",L"Save settings",900,18,344,147,32,BS_PUSHBUTTON);
    add(L"BUTTON",L"Close",901,172,344,147,32,BS_PUSHBUTTON);
}
void Overlay::onCommand(WPARAM wp,LPARAM){
    int id=LOWORD(wp);
    if(id>=ID_TAB_BASE&&id<ID_TAB_BASE+4){page_=id-ID_TAB_BASE;updatePanel();return;}
    if(id>=ID_CHECK_BASE&&id<ID_CHECK_BASE+12){
        switch(id-ID_CHECK_BASE){
        case 0:cfg_.hostile=!cfg_.hostile;break;case 1:cfg_.narrative=!cfg_.narrative;break;
        case 2:cfg_.container=!cfg_.container;break;case 3:cfg_.dropped=!cfg_.dropped;break;
        case 4:cfg_.micro=!cfg_.micro;break;case 5:cfg_.resource=!cfg_.resource;break;
        case 6:cfg_.showHp=!cfg_.showHp;break;case 7:cfg_.showBars=!cfg_.showBars;break;
        case 8:cfg_.hideDead=!cfg_.hideDead;break;case 9:cfg_.showArmor=!cfg_.showArmor;break;
        case 10:cfg_.focusOnly=!cfg_.focusOnly;break;
        case 11:cfg_.skeletonMode=cfg_.skeletonMode?0:1;boneDebugPoints_.clear();lastBoneSample_=0;break;
        }return;
    }
    if((id>=ID_DEC_BASE&&id<ID_DEC_BASE+7)||(id>=ID_INC_BASE&&id<ID_INC_BASE+7)){
        const bool plus=id>=ID_INC_BASE;
        int field=id-(plus?ID_INC_BASE:ID_DEC_BASE);
        int sign=plus?1:-1;
        switch(field){
          case 0:cfg_.maxDistance=std::clamp(cfg_.maxDistance+sign*500,100,50000);break;
          case 1:cfg_.lootDistance=std::clamp(cfg_.lootDistance+sign*500,100,50000);break;
          case 2:cfg_.maxLabels=std::clamp(cfg_.maxLabels+sign*5,1,250);break;
          case 3:cfg_.fontSize=std::clamp(cfg_.fontSize+sign,8,25);break;
          case 4:cfg_.barWidth=std::clamp(cfg_.barWidth+sign*5,24,240);break;
          case 5:cfg_.barHeight=std::clamp(cfg_.barHeight+sign,3,24);break;
          case 6:cfg_.scanIntervalSeconds=std::clamp(cfg_.scanIntervalSeconds+sign,3,120);break;
        }updatePanel();return;
    }
    if(id==900)saveSettings(cfg_);
    if(id==901)showPanel();
}
void Overlay::onScroll(WPARAM,LPARAM){}
void Overlay::updatePanelVisibility(){}
}
