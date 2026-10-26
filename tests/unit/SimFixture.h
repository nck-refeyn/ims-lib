// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#ifndef IMS_TEST_SIM_FIXTURE_H__
#define IMS_TEST_SIM_FIXTURE_H__

#include "CM_Sim.h"
#include "IMSSystem.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

namespace ims_test {

    // Short RX timeout keeps fault-path tests fast; still far above simulator latency
    constexpr int kTestRxTimeoutMs = 150;

    // A connected, initialised IMSSystem backed by a simulated device
    struct SimSystem {
        std::shared_ptr<iMS::sim::SimDevice> dev;
        std::shared_ptr<iMS::sim::CM_Sim> cm;
        std::shared_ptr<iMS::IMSSystem> ims;

        static SimSystem Start(std::shared_ptr<iMS::sim::SimDevice> dev, bool initialise = true)
        {
            SimSystem s;
            s.dev = std::move(dev);
            s.cm = iMS::sim::CM_Sim::Create(s.dev);
            s.cm->SetTimeouts(100, kTestRxTimeoutMs, 10000, 100);
            s.ims = iMS::IMSSystem::Create(s.cm, std::string("SIM0"));
            s.ims->Connect();
            if (initialise) s.ims->Initialise();
            s.dev->ClearRequests();
            return s;
        }

        void Stop()
        {
            if (ims) ims->Disconnect();
        }
    };

    class SimSystemTest : public ::testing::Test
    {
    protected:
        void SetUp() override { sys = SimSystem::Start(iMS::sim::SimDevice::iMSProWith_iMS4d()); }
        void TearDown() override { sys.Stop(); }

        iMS::sim::SimDevice& dev() { return *sys.dev; }
        iMS::sim::CM_Sim& cm() { return *sys.cm; }
        std::shared_ptr<iMS::IMSSystem> ims() { return sys.ims; }

        SimSystem sys;
    };

}

#endif
