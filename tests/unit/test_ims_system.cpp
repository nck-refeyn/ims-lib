// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// IMSSystem identification: magic/version probing and capability lookup in the
// embedded hardware database, driven end-to-end through the simulated transport.

#include "SimFixture.h"
#include "Image_p.h"

#include <cstdint>
#include "IMSConstants.h"

#include <gtest/gtest.h>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;
using ims_test::SimSystem;

TEST(IMSSystemInitialise, IdentifiesProControllerAndSynth)
{
    auto s = SimSystem::Start(SimDevice::iMSProWith_iMS4d());

    const auto& c = s.ims->Ctlr();
    ASSERT_TRUE(c.IsValid());
    EXPECT_EQ(c.Model(), "iMSP");
    EXPECT_TRUE(c.GetCap().FastImageTransfer);
    EXPECT_TRUE(c.GetCap().SimultaneousPlayback);
    EXPECT_TRUE(c.GetCap().RemoteUpgrade); // database value "2" = from major version 2
    EXPECT_EQ(c.GetCap().MaxImageSize, 18000000);
    EXPECT_EQ(c.GetVersion().major, 2);
    EXPECT_EQ(c.GetVersion().minor, 3);
    EXPECT_EQ(c.GetVersion().revision, 120);

    const auto& sy = s.ims->Synth();
    ASSERT_TRUE(sy.IsValid());
    EXPECT_EQ(sy.Model(), "iMS4d");
    EXPECT_EQ(sy.GetCap().freqBits, 32);
    EXPECT_EQ(sy.GetCap().channels, 4);
    EXPECT_NEAR(static_cast<double>(sy.GetCap().lowerFrequency), 10.17, 1e-9);
    EXPECT_NEAR(static_cast<double>(sy.GetCap().upperFrequency), 228.92, 1e-9);
    EXPECT_TRUE(sy.GetCap().ChannelComp); // revision 80 >= 73
    EXPECT_EQ(sy.AddOn(), nullptr);

    s.Stop();
}

TEST(IMSSystemInitialise, ChannelCompDependsOnSynthRevision)
{
    auto dev = SimDevice::iMSProWith_iMS4d();
    dev->WriteRegs(Target::Synthesiser, 6, { 72 });
    auto s = SimSystem::Start(dev);
    EXPECT_FALSE(s.ims->Synth().GetCap().ChannelComp);
    s.Stop();
}

TEST(IMSSystemInitialise, LiteControllerReadsSingleImageFromRegisters)
{
    auto dev = SimDevice::iMSLiteWith_iMS4();
    dev->WriteRegs(Target::Controller, CTRLR_REG_NumPts, { 321 });
    dev->WriteRegs(Target::Controller, CTRLR_REG_UUID, { 0x0201, 0x0403, 0x0605, 0x0807, 0x0A09, 0x0C0B, 0x0E0D, 0x100F });
    auto s = SimSystem::Start(dev);

    ASSERT_TRUE(s.ims->Ctlr().IsValid());
    EXPECT_EQ(s.ims->Ctlr().Model(), "iMSL");
    EXPECT_FALSE(s.ims->Ctlr().GetCap().FastImageTransfer);
    EXPECT_EQ(s.ims->Synth().Model(), "iMS4");
    EXPECT_EQ(s.ims->Synth().GetCap().freqBits, 16);

    const auto& tbl = s.ims->Ctlr().ImgTable();
    ASSERT_EQ(tbl.size(), 1u);
    EXPECT_EQ(tbl.begin()->NPts(), 321);
    EXPECT_EQ(tbl.begin()->UUID()[0], 1);
    EXPECT_EQ(tbl.begin()->UUID()[15], 16);
    s.Stop();
}

TEST(IMSSystemInitialise, MissingSynthLeavesOnlyController)
{
    auto dev = SimDevice::iMSProWith_iMS4d();
    dev->SetPresent(Target::Synthesiser, false);
    auto s = SimSystem::Start(dev);
    EXPECT_TRUE(s.ims->Ctlr().IsValid());
    EXPECT_FALSE(s.ims->Synth().IsValid());
    s.Stop();
}

TEST(IMSSystemInitialise, NothingAnsweringYieldsInvalidSystem)
{
    auto dev = std::make_shared<SimDevice>();
    dev->SetPresent(Target::Synthesiser, false);
    dev->SetPresent(Target::Controller, false);
    auto s = SimSystem::Start(dev);
    EXPECT_FALSE(s.ims->Ctlr().IsValid());
    EXPECT_FALSE(s.ims->Synth().IsValid());
    s.Stop();
}

TEST(IMSSystemInitialise, WorksOverSlowFragmentedLink)
{
    auto dev = SimDevice::iMSProWith_iMS4d();
    dev->SetChunkSize(3);
    dev->SetLatency(1ms);
    auto s = SimSystem::Start(dev);
    EXPECT_EQ(s.ims->Ctlr().Model(), "iMSP");
    EXPECT_EQ(s.ims->Synth().Model(), "iMS4d");
    s.Stop();
}

TEST(IMSSystemInitialise, WithoutConnectionReturnsFalse)
{
    auto ims = IMSSystem::Create(std::shared_ptr<IConnectionManager>(), std::string("nowhere"));
    EXPECT_FALSE(ims->Initialise());
    EXPECT_FALSE(ims->Open());
}

TEST(IMSSystemDiscover, SimTransportDiscoversOneSystem)
{
    auto cm = CM_Sim::Create(SimDevice::iMSProWith_iMS4d());
    cm->SetTimeouts(100, ims_test::kTestRxTimeoutMs, 10000, 100);
    auto found = cm->Discover(ListBase<std::string>(), nullptr);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0]->ConnPort(), "SIM0");
    EXPECT_EQ(found[0]->Ctlr().Model(), "iMSP");
    EXPECT_FALSE(cm->Open()); // Discover leaves the connection closed
}

TEST(IMSSystemInitialise, IdentifiesCompactSynth)
{
    auto s = SimSystem::Start(SimDevice::iCSA());
    EXPECT_EQ(s.ims->Ctlr().Model(), "iCSA");
    EXPECT_TRUE(s.ims->Ctlr().GetCap().FastImageTransfer);
    EXPECT_EQ(s.ims->Ctlr().GetCap().MaxImageSize, 250000);
    EXPECT_EQ(s.ims->Synth().Model(), "iCSA");
    EXPECT_EQ(s.ims->Synth().GetCap().channels, 2);
    EXPECT_EQ(s.ims->Synth().GetCap().freqBits, 32);
    s.Stop();
}
