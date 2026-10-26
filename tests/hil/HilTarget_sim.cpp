// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// HIL backend for the simulator, so the HIL suite itself is exercised in ordinary CI.

#include "HilTarget.h"
#include "CM_Sim.h"

#include <cstdlib>
#include <string>

namespace hil {

    namespace {
        std::unique_ptr<Target> g_target;
    }

    Target* Acquire()
    {
        if (g_target) return g_target.get();

        const char* preset = std::getenv("IMS_SIM_PRESET");
        const std::string name = preset ? preset : "pro";
        auto dev = name == "icsa" ? iMS::sim::SimDevice::iCSA()
                 : name == "lite" ? iMS::sim::SimDevice::iMSLiteWith_iMS4()
                 : iMS::sim::SimDevice::iMSProWith_iMS4d();
        if (const char* lat = std::getenv("IMS_SIM_LATENCY_US"))
            dev->SetLatency(std::chrono::microseconds(std::atoi(lat)));

        auto cm = iMS::sim::CM_Sim::Create(dev);
        auto ims = iMS::IMSSystem::Create(cm, std::string("SIM0"));
        ims->Connect();
        ims->Initialise();

        g_target = std::make_unique<Target>(Target{ ims, Describe(*ims), true });
        return g_target.get();
    }

    void Release()
    {
        if (g_target) g_target->ims->Disconnect();
        g_target.reset();
    }

}
