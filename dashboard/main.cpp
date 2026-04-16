#include "DashboardMainWindow.h"
#include <spdlog/spdlog.h>

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