// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "TestSupport.h"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ims_test::ConfigureLogging();
    return RUN_ALL_TESTS();
}
