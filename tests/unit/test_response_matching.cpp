// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Matching responses to requests as the iCSA firmware (v1.1.30) addresses them. It answers a
// CTRLR_REG write with the address after the last register written, an error with the address of
// the response before it, and reports a forced stop with an interrupt report that can arrive among
// the responses to the stop's own requests. Each of these once put every later response one request
// out of step, so the next image download took its memory address from the wrong response and
// wrote the image to address 0: the synth then played the wrong image until it was reset.

#include "CM_Sim.h"
#include "MessageEvent.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    // Long enough that a loaded machine doesn't time out responses that are on their way
    constexpr int kRxTimeoutMs = 1000;
    // A register whose value is read back after each sequence, to show the responses are in step
    constexpr std::uint16_t kProbeReg = 200;
    constexpr std::uint16_t kProbeValue = 0x1234;

    HostReport RegRead(std::uint16_t addr)
    {
        HostReport r(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, addr);
        ReportFields f = r.Fields();
        f.len = 2;
        r.Fields(f);
        return r;
    }

    HostReport RegWrite(std::uint16_t addr, std::uint16_t value)
    {
        HostReport r(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, addr);
        r.Payload<std::uint16_t>(value);
        return r;
    }

    HostReport Command(HostReport::Actions action, std::uint16_t addr)
    {
        HostReport r(action, HostReport::Dir::WRITE, addr);
        ReportFields f = r.Fields();
        f.len = 0;
        r.Fields(f);
        return r;
    }

    class ResponseMatchingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            dev = SimDevice::iCSA();
            dev->WriteRegs(Target::Controller, kProbeReg, { kProbeValue });
            cm = CM_Sim::Create(dev);
            cm->SetTimeouts(100, kRxTimeoutMs, 10000, 100);
            for (int e = 0; e < MessageEvents::Count; e++) cm->MessageEventSubscribe(e, &events);
            cm->Connect("SIM0");
        }

        void TearDown() override
        {
            for (int e = 0; e < MessageEvents::Count; e++) cm->MessageEventUnsubscribe(e, &events);
            cm->Disconnect();
        }

        // Sends all requests back to back, then a read of kProbeReg, and waits until each has been
        // answered or has failed (its failure event follows its status change)
        std::vector<DeviceReport> Pipeline(const std::vector<HostReport>& requests)
        {
            std::vector<MessageHandle> h;
            for (const auto& r : requests) h.push_back(cm->SendMsg(r));
            h.push_back(cm->SendMsg(RegRead(kProbeReg)));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10 * kRxTimeoutMs);
            std::vector<DeviceReport> responses;
            for (;;) {
                responses.clear();
                std::size_t pending = 0;
                for (auto handle : h) {
                    responses.push_back(cm->Response(handle));
                    if (!responses.back().Done()) pending++;
                }
                const auto reported = events.Count(MessageEvents::RESPONSE_TIMED_OUT) + events.Count(MessageEvents::RESPONSE_ERROR_CRC);
                if (pending <= reported || std::chrono::steady_clock::now() > deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return responses;
        }

        static void ExpectProbeInStep(const std::vector<DeviceReport>& responses)
        {
            const auto& probe = responses.back();
            ASSERT_TRUE(probe.Done());
            EXPECT_FALSE(probe.GeneralError());
            EXPECT_EQ(probe.Payload<std::uint16_t>(), kProbeValue) << "the read got another request's response";
        }

        std::shared_ptr<SimDevice> dev;
        std::shared_ptr<CM_Sim> cm;
        ims_test::EventRecorder events;
    };

}

// ImagePlayer::Play writes ImgModes (0x33) then ExtPolarity (0x34): the first write's response
// carries 0x34, the address of the second write
TEST_F(ResponseMatchingTest, WritesToConsecutiveRegistersAreAllAnswered)
{
    auto responses = Pipeline({ RegWrite(0x33, 9), RegWrite(0x34, 0), RegWrite(0x36, 8), RegWrite(0x31, 0x21) });
    for (std::size_t i = 0; i + 1 < responses.size(); i++) {
        EXPECT_TRUE(responses[i].Done()) << "write " << i;
        EXPECT_FALSE(responses[i].GeneralError()) << "write " << i;
    }
    ExpectProbeInStep(responses);
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_TIMED_OUT), 0u) << "no response is lost";
}

// A multi-register write is answered with the address after its last register
TEST_F(ResponseMatchingTest, MultiRegisterWriteIsAnswered)
{
    HostReport points(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, 0x40);
    points.Payload<std::uint32_t>(6166);
    auto responses = Pipeline({ points, RegWrite(0x42, 1) });
    EXPECT_TRUE(responses[0].Done());
    EXPECT_FALSE(responses[0].GeneralError());
    ExpectProbeInStep(responses);
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_TIMED_OUT), 0u);
}

// ImagePlayer::Stop(IMMEDIATELY): the iCSA rejects Seq_Stop and DMA_Abort, with the address of the
// response before each, and reports the stop with an interrupt report, here ahead of Seq_Stop's response
TEST_F(ResponseMatchingTest, ForcedStopLeavesResponsesInStep)
{
    dev->SetUnsupported(HostReport::Actions::CTRLR_SEQPLAY);
    dev->SetUnsupported(HostReport::Actions::CTRLR_SYNDMA);
    dev->SetHandler([this](const Request& r) -> std::optional<Response> {
        if (r.action == HostReport::Actions::CTRLR_SEQPLAY) dev->RaiseInterrupt(0, { 0x00, 0x00 });
        return std::nullopt;
    });
    auto responses = Pipeline({
        RegWrite(0x35, 1),                                          // Img_Play = FSTOP
        Command(HostReport::Actions::CTRLR_SEQPLAY, 2),             // Seq_Stop
        Command(HostReport::Actions::CTRLR_SYNDMA, 2),              // DMA_Abort
        RegRead(0x4A),                                              // as ImageDownload reads FPIFormat next
    });
    EXPECT_TRUE(responses[0].Done());
    EXPECT_FALSE(responses[0].GeneralError());
    EXPECT_TRUE(responses[1].GeneralError()) << "Seq_Stop got the interrupt report or another response";
    EXPECT_TRUE(responses[2].GeneralError());
    EXPECT_EQ(responses[3].Payload<std::uint16_t>() & 1, 1) << "the FPIFormat read got another response";
    ExpectProbeInStep(responses);
    EXPECT_TRUE(events.WaitFor(MessageEvents::INTERRUPT_RECEIVED, 1, 1s)) << "the interrupt report is handled as one";
}
