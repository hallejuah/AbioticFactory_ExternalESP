#pragma once
#include "scanner.hpp"
#include "settings.hpp"
#include "bone_points.hpp"
namespace abiotic {
class Overlay {
public:
    explicit Overlay(HINSTANCE instance);
    int run();
    static LRESULT CALLBACK wndproc(HWND,UINT,WPARAM,LPARAM);
private:
    bool initialize();
    void tick();
    void paint(HDC hdc);
    void hotkeys();
    void showPanel();
    void updatePanel();
    void updatePanelVisibility();
    void geometry();
    void toggleCategory(Category category);
    bool focused()const;
    void drawRow(HDC, const Row&);
    void drawBoneDebug(HDC);
    void probeNearestEnemy();
    void probeNearestOutline();
    void probeNearestReflection();
    void probeNearestProperties();
    void probeNearestPropertyValues();
    void probeNearestBones();
    void probeNearestNativeOutline();
    struct ActorSample { Vec3 pos{}; Life life{}; bool valid{}; };
    std::unordered_map<Addr,ActorSample> actorSamples_;
    ULONGLONG lastActorSample_{};
    void onCommand(WPARAM,LPARAM);
    void onScroll(WPARAM,LPARAM);
    bool reconnect();
    HINSTANCE instance_{}; HWND overlay_{},game_{},panel_{};
    HWND tabs_{}, panelTitle_{};
    std::vector<HWND> controls_;
    Process proc_;UE5 ue_;Scanner scanner_;Config cfg_;
    RECT gameRect_{};std::vector<Row> rows_;
    std::vector<BoneDebugPoint> boneDebugPoints_;
    ULONGLONG lastBoneSample_{};
    unsigned long long tickCount_{};
    ULONGLONG lastScan_{},lastChainCheck_{},lastLevelCheck_{},lastReconnectAttempt_{},lastStats_{};
    ULONGLONG scanCount_{},scanErrors_{},reconnectCount_{},readErrors_{};
    double scanLastMs_{},scanAvgMs_{};
    bool panelOpen_{false};int page_{0};bool scanRequested_{false},connected_{false};
    std::array<bool,256> held_{};
    std::wstring status_;
};
}
