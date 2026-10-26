// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// The hardware-in-the-loop suite runs the same tests and benchmarks against either
// real hardware (ims_hil_tests, ims_bench) or the simulator (ims_hil_tests_sim,
// ims_bench_sim). Which one is chosen at link time: HilTarget_hw.cpp or HilTarget_sim.cpp.
//
// Environment
//   IMS_HIL_INTERFACE    hardware: connection module to scan, e.g. CM_USBSS, CM_ETH (default: scan all)
//   IMS_HIL_ADDRESS      hardware: address hint for that module, e.g. an IP address
//   IMS_HIL_ALLOW_WRITE  set to 1 to run tests that change device state (image memory/table)
//   IMS_SIM_LATENCY_US   simulator: response latency, to approximate a real link (default 0)
//   IMS_SIM_PRESET       simulator: pro (iMS-Pro + iMS4d, default), lite (iMS-Lite + iMS4) or icsa

#ifndef IMS_HIL_TARGET_H__
#define IMS_HIL_TARGET_H__

#include "IConnectionManager.h"
#include "IMSSystem.h"
#include "Image.h"

#include <chrono>
#include <memory>
#include <string>

namespace hil {

    struct Target {
        std::shared_ptr<iMS::IMSSystem> ims;
        std::string description;
        bool simulated;

        std::shared_ptr<iMS::IConnectionManager> Conn() const { return ims->Connection(); }
    };

    // Finds and connects to the system under test on first call. Returns nullptr if none.
    Target* Acquire();
    // Disconnects; a later Acquire() reconnects
    void Release();

    bool WritesAllowed();
    std::string Describe(const iMS::IMSSystem& ims);

    // --- Helpers shared by the HIL tests and benchmarks ------------------------

    iMS::HostReport RegRead(iMS::HostReport::Actions a, std::uint16_t addr, std::uint16_t len = 2);

    // An image with a distinct frequency ramp, so a misplaced or corrupted transfer fails verification
    iMS::Image RampImage(const iMS::IMSSystem& ims, std::size_t points);

    struct DownloadResult {
        bool finished = false;
        bool verified = false;
        int bytes = 0;
        std::chrono::duration<double> elapsed{ 0 };
        std::string error;
    };
    DownloadResult DownloadImage(const std::shared_ptr<iMS::IMSSystem>& ims, const iMS::Image& img, bool verify,
        std::chrono::seconds timeout = std::chrono::seconds(60));

    // Removes the image from the controller's image table, if present
    void EraseImage(const std::shared_ptr<iMS::IMSSystem>& ims, const iMS::Image& img);

}

#endif
