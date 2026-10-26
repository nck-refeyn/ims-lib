// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// CM_Common's pipelined memory transfer (image download / upload) against the simulator.

#include "CM_Sim.h"
#include "MessageEvent.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <thread>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    const std::array<std::uint8_t, 16> kUuid{};

    boost::container::deque<std::uint8_t> Pattern(std::size_t n, std::uint8_t seed = 0)
    {
        boost::container::deque<std::uint8_t> d(n);
        for (std::size_t i = 0; i < n; i++) d[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xFF);
        return d;
    }

    class MemoryTransferTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            dev = SimDevice::iMSProWith_iMS4d();
            cm = CM_Sim::Create(dev);
            cm->SetTimeouts(100, 100, 10000, 100);
            cm->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, &events);
            cm->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_NOT_IDLE, &events);
            cm->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_ERROR, &events);
            cm->Connect("SIM0");
        }

        void TearDown() override
        {
            cm->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, &events);
            cm->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_NOT_IDLE, &events);
            cm->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_ERROR, &events);
            cm->Disconnect();
        }

        std::shared_ptr<SimDevice> dev;
        std::shared_ptr<CM_Sim> cm;
        ims_test::EventRecorder events;
    };

}

TEST_F(MemoryTransferTest, DownloadPadsToTransferUnitAndLandsInImageMemory)
{
    auto data = Pattern(1000);
    const auto original = data;

    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 5s));
    EXPECT_EQ(events.Events(MessageEvents::MEMORY_TRANSFER_COMPLETE)[0].param, 1024);
    EXPECT_EQ(data.size(), 1024u);

    auto mem = dev->Memory(HostReport::Actions::CTRLR_IMAGE, 0, 1000);
    EXPECT_TRUE(std::equal(mem.begin(), mem.end(), original.begin()));

    auto writes = dev->Requests(HostReport::Actions::CTRLR_IMAGE);
    ASSERT_EQ(writes.size(), 16u);
    for (std::size_t i = 0; i < writes.size(); i++) {
        EXPECT_FALSE(writes[i].read);
        EXPECT_EQ(writes[i].addr, i);
        EXPECT_EQ(writes[i].payload.size(), 64u);
    }
}

TEST_F(MemoryTransferTest, DownloadRejectsUnalignedAddress)
{
    auto data = Pattern(64);
    EXPECT_FALSE(cm->MemoryDownload(data, 4, 0, kUuid));
    EXPECT_TRUE(dev->Requests(HostReport::Actions::CTRLR_IMAGE).empty());
}

TEST_F(MemoryTransferTest, SecondTransferWhileBusyIsRefused)
{
    dev->SetLatency(500us);
    auto first = Pattern(64 * 256);
    auto second = Pattern(64);
    ASSERT_TRUE(cm->MemoryDownload(first, 0, 0, kUuid));
    EXPECT_FALSE(cm->MemoryDownload(second, 0, 0, kUuid));
    EXPECT_EQ(events.Count(MessageEvents::MEMORY_TRANSFER_NOT_IDLE), 1u);
    EXPECT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 10s));
}

TEST_F(MemoryTransferTest, UploadReturnsDeviceMemory)
{
    auto pattern = Pattern(640, 7);
    dev->WriteMemory(HostReport::Actions::CTRLR_IMAGE, 0, std::vector<std::uint8_t>(pattern.begin(), pattern.end()));

    boost::container::deque<std::uint8_t> readback;
    ASSERT_TRUE(cm->MemoryUpload(readback, 0, 640, 3, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 5s));
    EXPECT_EQ(readback, pattern);

    // Upload is preceded by an image-index read that arms the controller's DMA
    auto idx = dev->Requests(HostReport::Actions::CTRLR_IMGIDX);
    ASSERT_EQ(idx.size(), 1u);
    EXPECT_EQ(idx[0].addr, 3);
    EXPECT_EQ(idx[0].context, 2);
}

TEST_F(MemoryTransferTest, DownloadThenUploadRoundTrip)
{
    auto data = Pattern(64 * 100, 99);
    const auto original = data;
    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 5s));

    boost::container::deque<std::uint8_t> readback;
    ASSERT_TRUE(cm->MemoryUpload(readback, 0, static_cast<int>(original.size()), 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 2, 5s));
    EXPECT_EQ(readback, original);
}

TEST_F(MemoryTransferTest, TransactionsBeyond64kUseContextForUpperAddressBits)
{
    // 0x10000 transactions fit in the 16-bit address; the next one needs context = 1
    cm->SetTimeouts(100, ims_test::kHeavyTrafficRxTimeoutMs, 10000, 100);
    const std::size_t n = (0x10000 + 2) * 64;
    auto data = Pattern(n);
    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 30s));

    auto writes = dev->Requests(HostReport::Actions::CTRLR_IMAGE);
    ASSERT_EQ(writes.size(), 0x10002u);
    EXPECT_EQ(writes[0xFFFF].context, 0);
    EXPECT_EQ(writes[0xFFFF].addr, 0xFFFF);
    EXPECT_EQ(writes[0x10000].context, 1);
    EXPECT_EQ(writes[0x10000].addr, 0);
    EXPECT_EQ(dev->Memory(HostReport::Actions::CTRLR_IMAGE, 0x10001 * 64, 64), std::vector<std::uint8_t>(data.end() - 64, data.end()));
}

// BUG: when a transfer response is lost, later responses shift onto earlier requests (see
// ConnectionManagerTest.DISABLED_LostResponseInPipelineDoesNotShiftLaterResponses), the last
// request times out, and the worker still reports MEMORY_TRANSFER_COMPLETE with the full byte
// count. MEMORY_TRANSFER_ERROR is never raised, so callers get misplaced upload data.
TEST_F(MemoryTransferTest, DISABLED_LostResponseDuringUploadIsReported)
{
    auto pattern = Pattern(640, 3);
    dev->WriteMemory(HostReport::Actions::CTRLR_IMAGE, 0, std::vector<std::uint8_t>(pattern.begin(), pattern.end()));
    dev->SetHandler([this, n = 0](const Request& r) mutable -> std::optional<Response> {
        if (r.action == HostReport::Actions::CTRLR_IMAGE && ++n == 5) dev->InjectFault(Fault::Drop);
        return std::nullopt;
    });

    boost::container::deque<std::uint8_t> readback;
    ASSERT_TRUE(cm->MemoryUpload(readback, 0, 640, 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_COMPLETE, 1, 5s) ||
                events.WaitFor(MessageEvents::MEMORY_TRANSFER_ERROR, 1, 1s));
    // Either the failure is reported, or (e.g. after a retry) the data is right
    const bool errored = events.Count(MessageEvents::MEMORY_TRANSFER_ERROR) > 0;
    const bool completed = events.Count(MessageEvents::MEMORY_TRANSFER_COMPLETE) > 0;
    EXPECT_NE(errored, completed);
    if (completed) EXPECT_EQ(readback, pattern);
}

TEST_F(MemoryTransferTest, LostResponseDuringDownloadIsReported)
{
    dev->SetHandler([this, n = 0](const Request& r) mutable -> std::optional<Response> {
        if (r.action == HostReport::Actions::CTRLR_IMAGE && ++n == 5) dev->InjectFault(Fault::Drop);
        return std::nullopt;
    });
    auto data = Pattern(640);
    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, kUuid));
    ASSERT_TRUE(events.WaitFor(MessageEvents::MEMORY_TRANSFER_ERROR, 1, 5s));
    EXPECT_EQ(events.Count(MessageEvents::MEMORY_TRANSFER_COMPLETE), 0u);
}

// Closing the connection stops the parser, so a transfer waiting for responses must give up rather
// than block Disconnect() forever
TEST_F(MemoryTransferTest, DisconnectDuringTransferReturnsAndReportsFailure)
{
    // Disconnect() first lets outstanding messages drain for up to rxTimeout + 1 s, so the transfer
    // must outlast that even with precise sleeps: 2000 transactions, 16 in flight, 20 ms each is 2.5 s
    dev->SetLatency(20ms);
    auto data = Pattern(64 * 2000);
    ASSERT_TRUE(cm->MemoryDownload(data, 0, 0, kUuid));
    std::this_thread::sleep_for(50ms);

    const auto t0 = std::chrono::steady_clock::now();
    cm->Disconnect();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 3s);
    EXPECT_EQ(events.Count(MessageEvents::MEMORY_TRANSFER_ERROR), 1u);
    EXPECT_EQ(events.Count(MessageEvents::MEMORY_TRANSFER_COMPLETE), 0u);
}
