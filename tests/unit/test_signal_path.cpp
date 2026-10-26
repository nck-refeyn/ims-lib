// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// SignalPath: verifies the register traffic each API call produces on the device.

#include "SimFixture.h"
#include "SignalPath.h"

#include <cstdint>
#include "IMSConstants.h"

#include <gtest/gtest.h>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;
using ims_test::SimSystem;

namespace {

    class SignalPathTest : public ims_test::SimSystemTest
    {
    protected:
        // SignalPath sends asynchronously; wait for the device to have seen `n` requests
        std::vector<Request> Sent(std::size_t n)
        {
            EXPECT_TRUE(dev().WaitForRequests(n, 1s));
            return dev().Requests();
        }

        // Give asynchronous sends a moment, then confirm nothing reached the device
        void ExpectNothingSent()
        {
            EXPECT_FALSE(dev().WaitForRequests(1, 50ms));
        }
    };

}

TEST_F(SignalPathTest, DDSPowerLevelIsInvertedAttenuation)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.UpdateDDSPowerLevel(Percent(100)));
    ASSERT_TRUE(sp.UpdateDDSPowerLevel(Percent(50)));
    ASSERT_TRUE(sp.UpdateDDSPowerLevel(Percent(0)));

    auto r = Sent(3);
    for (auto& req : r) {
        EXPECT_EQ(req.action, HostReport::Actions::RF_POWER);
        EXPECT_FALSE(req.read);
        EXPECT_EQ(req.addr, 0);
    }
    EXPECT_EQ(r[0].payload, (std::vector<std::uint8_t>{ 0x00 }));
    EXPECT_EQ(r[1].payload, (std::vector<std::uint8_t>{ 127 }));
    EXPECT_EQ(r[2].payload, (std::vector<std::uint8_t>{ 0xFF }));
}

// Regression for c8176ab: duty-cycle timing is in 8 ns (125 MHz sync clock) units, with a
// 2-tick latency compensation applied to the delay
TEST_F(SignalPathTest, RFDutyCycleUses125MHzTicks)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.SetRFDutyCycle(true, std::chrono::nanoseconds(800), std::chrono::nanoseconds(400)));

    auto r = Sent(1);
    EXPECT_EQ(r[0].action, HostReport::Actions::SYNTH_REG);
    EXPECT_EQ(r[0].addr, SYNTH_REG_DutyCycleEn);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 1, 50, 98 }));
    EXPECT_EQ(dev().Reg(Target::Synthesiser, SYNTH_REG_DutyCycleWidth), 50);
    EXPECT_EQ(dev().Reg(Target::Synthesiser, SYNTH_REG_DutyCycleDelay), 98);
}

TEST_F(SignalPathTest, RFDutyCycleDelayCompensationOnlyAboveTwoTicks)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.SetRFDutyCycle(false, std::chrono::nanoseconds(16), std::chrono::nanoseconds(0)));
    ASSERT_TRUE(sp.SetRFDutyCycle(false, std::chrono::nanoseconds(24), std::chrono::nanoseconds(0)));
    auto r = Sent(2);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 0, 0, 2 }));
    EXPECT_EQ(r[1].Words(), (std::vector<std::uint16_t>{ 0, 0, 1 }));
}

TEST_F(SignalPathTest, RFDutyCycleRejectsOutOfRangeValues)
{
    SignalPath sp(ims());
    EXPECT_FALSE(sp.SetRFDutyCycle(true, std::chrono::nanoseconds(0), std::chrono::nanoseconds(65536LL * 8)));
    EXPECT_FALSE(sp.SetRFDutyCycle(true, std::chrono::nanoseconds(65536LL * 8), std::chrono::nanoseconds(0)));
    EXPECT_FALSE(sp.SetRFDutyCycle(true, std::chrono::nanoseconds(-8), std::chrono::nanoseconds(0)));
    ExpectNothingSent();
}

TEST_F(SignalPathTest, ChannelDelayEnforcesMinimumSeparation)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.SetChannelDelay(std::chrono::nanoseconds(100), std::chrono::nanoseconds(200)));
    ASSERT_TRUE(sp.SetChannelDelay(std::chrono::nanoseconds(0), std::chrono::nanoseconds(0)));
    auto r = Sent(2);
    EXPECT_EQ(r[0].addr, SYNTH_REG_ChannelDelay34);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 20, 10 })); // {ch3/4, ch1/2} in 10 ns units
    EXPECT_EQ(r[1].Words(), (std::vector<std::uint16_t>{ 3, 0 }));
}

TEST_F(SignalPathTest, PhaseTuningSmallAngle)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.UpdatePhaseTuning(RFChannel(2), Degrees(1.0)));
    auto r = Sent(1);
    EXPECT_EQ(r[0].addr, SYNTH_REG_Phase_Offset_Ch2);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 45 })); // floor(1 * 16383 / 360)
}

// Regression: UpdatePhaseTuning used to cast its 14-bit phase word through std::uint8_t, so any
// offset above ~5.6 degrees was corrupted.
TEST_F(SignalPathTest, PhaseTuningUsesFull14BitRange)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.UpdatePhaseTuning(RFChannel(1), Degrees(90.0)));
    ASSERT_TRUE(sp.UpdatePhaseTuning(RFChannel(1), Degrees(359.0)));
    auto r = Sent(2);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 4095 }));
    EXPECT_EQ(r[1].Words(), (std::vector<std::uint16_t>{ 16337 }));
}

TEST_F(SignalPathTest, PhaseTuningRejectsAllChannels)
{
    SignalPath sp(ims());
    EXPECT_FALSE(sp.UpdatePhaseTuning(RFChannel(RFChannel::all), Degrees(10.0)));
    ExpectNothingSent();
}

TEST_F(SignalPathTest, ChannelReversalUnlocksThenWritesSwapBit)
{
    SignalPath sp(ims());
    ASSERT_TRUE(sp.SetChannelReversal(true));
    auto r = Sent(2);
    EXPECT_EQ(r[0].addr, SYNTH_REG_IO_Config_Mask);
    EXPECT_EQ(r[0].Words(), (std::vector<std::uint16_t>{ 1 }));
    EXPECT_EQ(r[1].addr, SYNTH_REG_Channel_Swap);
    EXPECT_EQ(r[1].Words(), (std::vector<std::uint16_t>{ 1 }));
}

TEST_F(SignalPathTest, CalibrationToneAtBandEdges32BitSynth)
{
    SignalPath sp(ims());
    const double lower = ims()->Synth().GetCap().lowerFrequency;
    const double upper = ims()->Synth().GetCap().upperFrequency;

    ASSERT_TRUE(sp.SetCalibrationTone(FAP(lower, 0.0, 0.0)));
    ASSERT_TRUE(sp.SetCalibrationTone(FAP(upper + 50.0, 0.0, 0.0))); // clamped to the band

    auto r = Sent(4);
    // 32-bit synths split the tuning word: low 16 bits to FreqLower, high 16 bits with phase/ampl
    EXPECT_EQ(r[0].addr, SYNTH_REG_FreqLower);
    EXPECT_EQ(r[0].payload, (std::vector<std::uint8_t>{ 0, 0 }));
    EXPECT_EQ(r[1].addr, SYNTH_REG_SingleTone_Phase);
    EXPECT_EQ(r[1].payload, (std::vector<std::uint8_t>{ 0, 0, 0, 0, 0, 0 }));

    EXPECT_EQ(r[2].payload, (std::vector<std::uint8_t>{ 0xFF, 0xFF }));
    ASSERT_EQ(r[3].payload.size(), 6u);
    EXPECT_EQ(r[3].payload[4], 0xFF);
    EXPECT_EQ(r[3].payload[5], 0xFF);
}

TEST(SignalPathNoSynth, OperationsFailWithoutSendingAnything)
{
    auto dev = SimDevice::iMSProWith_iMS4d();
    dev->SetPresent(Target::Synthesiser, false);
    auto s = SimSystem::Start(dev);
    ASSERT_FALSE(s.ims->Synth().IsValid());

    SignalPath sp(s.ims);
    EXPECT_FALSE(sp.UpdateDDSPowerLevel(Percent(50)));
    EXPECT_FALSE(sp.SetRFDutyCycle(true, std::chrono::nanoseconds(80), std::chrono::nanoseconds(80)));
    EXPECT_FALSE(sp.SetChannelReversal(true));
    EXPECT_FALSE(dev->WaitForRequests(1, 50ms));
    s.Stop();
}
