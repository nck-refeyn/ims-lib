// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Receive-side link faults seen on an FTDI-connected iCSA during image downloads (about one
// response in 6,500): a stray byte in the device-to-host stream, and a run of ~60 bytes missing
// (a lost USB packet). Responses must still reach the right requests, the ones whose responses
// were lost must fail, and nothing may be reported as a device error.

#include "CM_Sim.h"
#include "MessageEvent.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <cstdio>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    // No test here waits for a request to time out (lost responses are failed by the ones after
    // them), so this only has to be long enough that a loaded machine doesn't time out the rest
    constexpr int kRxTimeoutMs = 1000;
    constexpr int kRequests = 40;
    constexpr std::uint16_t kFirstReg = 100;
    constexpr std::uint16_t kFaultReg = 110;

    HostReport RegRead(std::uint16_t addr)
    {
        HostReport r(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, addr);
        ReportFields f = r.Fields();
        f.len = 2;
        r.Fields(f);
        return r;
    }

    struct Outcome {
        int answered = 0;   // Done with the right value
        int wrong = 0;      // Done with another request's value
        int failed = 0;     // not Done
    };

    class LinkCorruptionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            dev = SimDevice::iMSProWith_iMS4d();
            for (int i = 0; i < kRequests; i++)
                dev->WriteRegs(Target::Controller, static_cast<std::uint16_t>(kFirstReg + i), { static_cast<std::uint16_t>(1000 + i) });
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

        // Runs the fault when the device handles the read of kFaultReg, i.e. just before its response
        void FaultAt(std::function<void(SimDevice&)> fault)
        {
            dev->SetHandler([this, fault](const Request& r) -> std::optional<Response> {
                if (r.action == HostReport::Actions::CTRLR_REG && r.addr == kFaultReg) fault(*dev);
                return std::nullopt;
            });
        }

        // Sends the requests and waits until each has been answered or has failed. A request's failure
        // event follows its status change, so once the failure events account for every request not
        // answered, none is still in flight. On a loaded machine that can take far longer than the
        // round trip, so a fixed wait would count requests still in the pipeline as failed.
        Outcome Pipeline()
        {
            std::vector<MessageHandle> h;
            for (int i = 0; i < kRequests; i++) h.push_back(cm->SendMsg(RegRead(static_cast<std::uint16_t>(kFirstReg + i))));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10 * kRxTimeoutMs);
            Outcome o;
            for (;;) {
                o = Outcome{};
                for (int i = 0; i < kRequests; i++) {
                    auto r = cm->Response(h[i]);
                    if (!r.Done() || r.RxCRC()) o.failed++;
                    else if (r.Payload<std::uint16_t>() == 1000 + i && r.Fields().addr == kFirstReg + i) o.answered++;
                    else o.wrong++;
                }
                const auto reported = events.Count(MessageEvents::RESPONSE_TIMED_OUT) + events.Count(MessageEvents::RESPONSE_ERROR_CRC);
                if (static_cast<std::size_t>(o.failed) <= reported || std::chrono::steady_clock::now() > deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            handles = h;
            return o;
        }

        std::shared_ptr<SimDevice> dev;
        std::shared_ptr<CM_Sim> cm;
        ims_test::EventRecorder events;
        std::vector<MessageHandle> handles;
    };

}

// A stray byte ahead of a response: on the iCSA it was 0x49, which is also the interrupt report ID
class StrayByte : public LinkCorruptionTest, public ::testing::WithParamInterface<std::vector<std::uint8_t>> {};

TEST_P(StrayByte, AllResponsesStillReachTheirRequests)
{
    const auto stray = GetParam();
    FaultAt([stray](SimDevice& d) { d.InjectRaw(stray); });
    auto o = Pipeline();
    EXPECT_EQ(o.wrong, 0);
    EXPECT_EQ(o.failed, 0);
    EXPECT_EQ(o.answered, kRequests);
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_ERROR_VALID), 0u) << "a corrupt frame must not be reported as a device error";
    EXPECT_TRUE(events.WaitFor(MessageEvents::UNEXPECTED_RX_CHAR));
}

INSTANTIATE_TEST_SUITE_P(Bytes, StrayByte, ::testing::Values(
    std::vector<std::uint8_t>{ 0x49 },          // interrupt report ID, as seen on the iCSA
    std::vector<std::uint8_t>{ 0x05 },          // controller response ID
    std::vector<std::uint8_t>{ 0x02 },          // synthesiser response ID
    std::vector<std::uint8_t>{ 0xA3 },          // not a report ID, as also seen on the iCSA
    std::vector<std::uint8_t>{ 0x85, 0x42 }     // two fragments
), [](const auto& p) {
    std::string name = "Byte";
    char buf[4];
    for (auto b : p.param) { std::snprintf(buf, sizeof(buf), "%02X", b); name += buf; }
    return name;
});

// About 60 bytes missing from the stream, starting part way into a response, as when a USB
// packet is lost: the responses inside it are lost, everything else must still be right
TEST_F(LinkCorruptionTest, DroppedPacketLosesOnlyTheResponsesInIt)
{
    FaultAt([](SimDevice& d) { d.DropOutput(2, 60); });
    auto o = Pipeline();
    EXPECT_EQ(o.wrong, 0);
    EXPECT_GE(o.failed, 6);
    EXPECT_LE(o.failed, 8);
    EXPECT_EQ(o.answered + o.failed, kRequests);
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_TIMED_OUT), static_cast<std::size_t>(o.failed));
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_ERROR_VALID), 0u);
}

// The requests whose responses were lost are failed as soon as a later response shows they are
// gone, not after the full RX timeout
TEST_F(LinkCorruptionTest, LostResponsesFailWithoutWaitingForTimeout)
{
    FaultAt([](SimDevice& d) { d.DropOutput(2, 60); });
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<MessageHandle> h;
    for (int i = 0; i < kRequests; i++) h.push_back(cm->SendMsg(RegRead(static_cast<std::uint16_t>(kFirstReg + i))));
    ASSERT_TRUE(events.WaitFor(MessageEvents::RESPONSE_TIMED_OUT, 6, std::chrono::milliseconds(3 * kRxTimeoutMs)));
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(kRxTimeoutMs));
}

// A response damaged in place (bad CRC, header intact) still fails its own request, as a CRC error
TEST_F(LinkCorruptionTest, CorruptResponseFailsOnlyItsRequest)
{
    FaultAt([](SimDevice& d) { d.InjectFault(Fault::CorruptCrc); });
    auto o = Pipeline();
    EXPECT_EQ(o.wrong, 0);
    EXPECT_EQ(o.failed, 1);
    ASSERT_TRUE(events.WaitFor(MessageEvents::RESPONSE_ERROR_CRC));
    EXPECT_EQ(events.Events(MessageEvents::RESPONSE_ERROR_CRC)[0].param, handles[kFaultReg - kFirstReg]);
}

// The failure that aborted an image download on the iCSA: one stray 0x49 among the acknowledgements
TEST_F(LinkCorruptionTest, StrayByteDuringImageDownloadDoesNotFailIt)
{
    dev->SetHandler([this, n = 0](const Request& r) mutable -> std::optional<Response> {
        if (r.action == HostReport::Actions::CTRLR_IMAGE && ++n == 50) dev->InjectRaw({ 0x49 });
        return std::nullopt;
    });
    boost::container::deque<std::uint8_t> data(64 * 200);
    for (std::size_t i = 0; i < data.size(); i++) data[i] = static_cast<std::uint8_t>(i * 13);
    const std::vector<std::uint8_t> original(data.begin(), data.end());
    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, {}));
    ASSERT_EQ(events.WaitForAny({ MessageEvents::MEMORY_TRANSFER_COMPLETE, MessageEvents::MEMORY_TRANSFER_ERROR }, 5s),
        MessageEvents::MEMORY_TRANSFER_COMPLETE);
    EXPECT_EQ(dev->Memory(HostReport::Actions::CTRLR_IMAGE, 0, original.size()), original);
}
