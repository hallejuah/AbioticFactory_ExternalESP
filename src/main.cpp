#include "abiotic/overlay.hpp"
#include <windows.h>
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int) {
    abiotic::Overlay app(h);
    return app.run();
}
