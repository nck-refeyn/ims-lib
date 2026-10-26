// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Value types that need no hardware.

#include "IMSSystem.h"
#include "IMSTypeDefs.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace iMS;

TEST(Frequency, UnitConversions)
{
    MHz f(12.5);
    EXPECT_DOUBLE_EQ(static_cast<double>(f), 12.5);
    EXPECT_DOUBLE_EQ(static_cast<double>(Frequency(f)), 12.5e6);

    kHz k(250);
    EXPECT_DOUBLE_EQ(static_cast<double>(k), 250.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(Frequency(k)), 250e3);

    f = 100.0;
    EXPECT_DOUBLE_EQ(static_cast<double>(Frequency(f)), 100e6);
}

TEST(Percent, ClampsToValidRange)
{
    EXPECT_DOUBLE_EQ(Percent(-5.0), 0.0);
    EXPECT_DOUBLE_EQ(Percent(150.0), 100.0);
    EXPECT_DOUBLE_EQ(Percent(42.5), 42.5);

    Percent p;
    p = 101.0;
    EXPECT_DOUBLE_EQ(p, 100.0);
    p = -1.0;
    EXPECT_DOUBLE_EQ(p, 0.0);
}

TEST(RFChannel, RejectsOutOfRangeChannels)
{
    EXPECT_NO_THROW(RFChannel(1));
    EXPECT_NO_THROW(RFChannel(4));
    EXPECT_THROW(RFChannel(0), std::invalid_argument);
    EXPECT_THROW(RFChannel(5), std::invalid_argument);
    EXPECT_TRUE(RFChannel(RFChannel::all).IsAll());
    EXPECT_TRUE(RFChannel().IsAll());
}

TEST(RFChannel, IncrementSaturatesAtLastChannel)
{
    RFChannel c(3);
    ++c;
    EXPECT_EQ(static_cast<int>(c), 4);
    ++c;
    EXPECT_EQ(static_cast<int>(c), 4);
    --c;
    EXPECT_EQ(static_cast<int>(c), 3);
}

TEST(FWVersion, DecodesRegisterBlock)
{
    // major.minor in word 0, revision in word 1, packed date in word 2, hh:mm in word 3
    FWVersion v({ 0x0203, 120, static_cast<std::uint16_t>(15 | (6 << 5) | (25 << 9)), static_cast<std::uint16_t>(30 | (14 << 8)) });
    EXPECT_EQ(v.major, 2);
    EXPECT_EQ(v.minor, 3);
    EXPECT_EQ(v.revision, 120);
    EXPECT_EQ(v.build_date.tm_year, 125); // years since 1900
    EXPECT_EQ(v.build_date.tm_mon, 5);    // June, zero-based
    EXPECT_EQ(v.build_date.tm_mday, 15);
    EXPECT_EQ(v.build_date.tm_hour, 14);
    EXPECT_EQ(v.build_date.tm_min, 30);
}

TEST(FWVersion, WrongSizeYieldsInvalidVersion)
{
    FWVersion v(std::vector<std::uint16_t>{ 0x0203, 1, 2, 3, 4 });
    EXPECT_EQ(v.major, -1);
    EXPECT_EQ(v.minor, -1);
    EXPECT_EQ(v.revision, -1);
}
