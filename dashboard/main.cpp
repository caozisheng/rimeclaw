#include "DashboardMainWindow.h"
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#endif

int main()
{
    try {
        rimeclaw::DashboardMainWindow app;
        app.run();
    }
    catch (const std::exception& e) {
        spdlog::error("[gfdashboard] Error: {}", e.what());
        return 1;
    }

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main();
}
#endif