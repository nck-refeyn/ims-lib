// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "HilTarget.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cstdio>

namespace {

    // Connects once for the whole run and records what was tested in the XML report
    class HilEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            if (auto* t = hil::Acquire()) {
                std::printf("HIL target: %s%s\n", t->description.c_str(), t->simulated ? " [simulated]" : "");
                ::testing::Test::RecordProperty("hil_target", t->description);
                ::testing::Test::RecordProperty("hil_simulated", t->simulated ? "true" : "false");
            }
            ::testing::Test::RecordProperty("hil_writes_allowed", hil::WritesAllowed() ? "true" : "false");
        }

        void TearDown() override { hil::Release(); }
    };

}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ims_test::ConfigureLogging();
    ::testing::AddGlobalTestEnvironment(new HilEnvironment);
    return RUN_ALL_TESTS();
}
