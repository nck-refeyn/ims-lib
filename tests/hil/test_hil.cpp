// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Hardware-in-the-loop conformance tests. Built twice: against real hardware and
// against the simulator. Read-only tests always run; tests that change device state
// need IMS_HIL_ALLOW_WRITE=1.

#include "HilTarget.h"

#include <cstdint>
#include "IMSConstants.h"

#include <gtest/gtest.h>

#include <thread>

using namespace iMS;

namespace {

    class Hil : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            t = hil::Acquire();
            ASSERT_NE(t, nullptr) << "No iMS system found; check IMS_HIL_INTERFACE / IMS_HIL_ADDRESS";
            conn = t->Conn();
        }

        hil::Target* t = nullptr;
        std::shared_ptr<IConnectionManager> conn;
    };

    struct Reg {
        HostReport::Actions action;
        std::uint16_t addr;
    };

}

TEST_F(Hil, SystemIsIdentified)
{
    RecordProperty("target", t->description);
    ASSERT_TRUE(t->ims->Ctlr().IsValid());
    ASSERT_TRUE(t->ims->Synth().IsValid());
    EXPECT_FALSE(t->ims->Ctlr().Model().empty()) << "controller magic not in hardware database";
    EXPECT_FALSE(t->ims->Synth().Model().empty()) << "synthesiser magic not in hardware database";
    EXPECT_GE(t->ims->Ctlr().GetVersion().major, 0);
    EXPECT_GE(t->ims->Synth().GetVersion().major, 0);
}

// Characterises the firmware: if responses echo the request address, the library can use it to
// detect responses attributed to the wrong request (see the DISABLED_ pipeline test in the unit suite)
TEST_F(Hil, ResponsesEchoRequestAddress)
{
    const Reg regs[] = {
        { HostReport::Actions::CTRLR_REG, 0 }, { HostReport::Actions::CTRLR_REG, 5 },
        { HostReport::Actions::CTRLR_REG, CTRLR_REG_NumPts }, { HostReport::Actions::SYNTH_REG, 0 },
        { HostReport::Actions::SYNTH_REG, 6 },
    };
    for (const auto& r : regs) {
        auto resp = conn->SendMsgBlocking(hil::RegRead(r.action, r.addr));
        ASSERT_TRUE(resp.Done()) << "addr " << r.addr;
        EXPECT_EQ(resp.Fields().addr, r.addr);
    }
}

TEST_F(Hil, RepeatedReadsAreStable)
{
    constexpr int N = 500;
    const auto first = conn->SendMsgBlocking(hil::RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(first.Done());
    const auto magic = first.Payload<std::uint16_t>();

    int failed = 0, crc = 0, wrong = 0;
    for (int i = 0; i < N; i++) {
        auto resp = conn->SendMsgBlocking(hil::RegRead(HostReport::Actions::CTRLR_REG, 0));
        if (!resp.Done()) failed++;
        else if (resp.RxCRC()) crc++;
        else if (resp.Payload<std::uint16_t>() != magic) wrong++;
    }
    EXPECT_EQ(failed, 0) << "timeouts out of " << N;
    EXPECT_EQ(crc, 0) << "CRC errors out of " << N;
    EXPECT_EQ(wrong, 0) << "wrong values out of " << N;
}

TEST_F(Hil, PipelinedReadsMatchBlockingReads)
{
    // Version registers 5..8 of both targets: read-only, and distinct enough to catch mismatches
    std::vector<Reg> regs;
    std::vector<std::uint16_t> expected;
    for (auto a : { HostReport::Actions::CTRLR_REG, HostReport::Actions::SYNTH_REG }) {
        for (std::uint16_t addr = 5; addr <= 8; addr++) {
            auto resp = conn->SendMsgBlocking(hil::RegRead(a, addr));
            ASSERT_TRUE(resp.Done());
            regs.push_back({ a, addr });
            expected.push_back(resp.Payload<std::uint16_t>());
        }
    }

    constexpr int N = 256;
    std::vector<MessageHandle> handles;
    for (int i = 0; i < N; i++) {
        const auto& r = regs[i % regs.size()];
        handles.push_back(conn->SendMsg(hil::RegRead(r.action, r.addr)));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (int i = 0; i < N; i++) {
        DeviceReport resp;
        while (!(resp = conn->Response(handles[i])).Done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(resp.Done()) << "message " << i;
        EXPECT_EQ(resp.Payload<std::uint16_t>(), expected[i % regs.size()]) << "message " << i;
    }
}

TEST_F(Hil, ReconnectCycles)
{
    for (int i = 0; i < 10; i++) {
        t->ims->Disconnect();
        ASSERT_FALSE(t->ims->Open());
        t->ims->Connect();
        ASSERT_TRUE(t->ims->Open()) << "cycle " << i;
        auto resp = conn->SendMsgBlocking(hil::RegRead(HostReport::Actions::CTRLR_REG, 0));
        ASSERT_TRUE(resp.Done()) << "cycle " << i;
    }
}

class HilImage : public Hil, public ::testing::WithParamInterface<int> {};

// BUG: a duplicate message handle gives a response to the wrong request, so a large verify
// occasionally fails. Enable once message handles are unique.
TEST_P(HilImage, DISABLED_DownloadVerifies)
{
    // GTEST_SKIP only returns from the function it is in, so it must be here, not in a helper
    if (!hil::WritesAllowed()) GTEST_SKIP() << "changes device state; set IMS_HIL_ALLOW_WRITE=1";
    if (!t->ims->Ctlr().GetCap().FastImageTransfer) GTEST_SKIP() << "controller has no image table";

    const auto img = hil::RampImage(*t->ims, static_cast<std::size_t>(GetParam()));
    const auto r = hil::DownloadImage(t->ims, img, true);
    hil::EraseImage(t->ims, img);

    ASSERT_TRUE(r.finished) << r.error;
    EXPECT_TRUE(r.verified) << r.error;
    EXPECT_GT(r.bytes, 0);
    RecordProperty("bytes", r.bytes);
    RecordProperty("download_ms", static_cast<int>(r.elapsed.count() * 1000));
}

INSTANTIATE_TEST_SUITE_P(Points, HilImage, ::testing::Values(1, 1000, 20000),
    [](const auto& p) { return std::to_string(p.param); });
