// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// CM_Common message handling (queueing, response matching, timeouts, error
// classification, interrupts, events) exercised through the simulated transport.

#include "CM_Sim.h"
#include "MessageEvent.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    constexpr int kRxTimeoutMs = 100;

    HostReport RegRead(HostReport::Actions a, std::uint16_t addr, std::uint16_t len = 2)
    {
        HostReport r(a, HostReport::Dir::READ, addr);
        ReportFields f = r.Fields();
        f.len = len;
        r.Fields(f);
        return r;
    }

    class ConnectionManagerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            dev = SimDevice::iMSProWith_iMS4d();
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

        bool WaitDone(MessageHandle h, std::chrono::milliseconds timeout = 2s)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline) {
                if (cm->Response(h).Done()) return true;
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }

        std::shared_ptr<SimDevice> dev;
        std::shared_ptr<CM_Sim> cm;
        ims_test::EventRecorder events;
    };

}

TEST_F(ConnectionManagerTest, BlockingRoundTrip)
{
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(resp.Done());
    EXPECT_FALSE(resp.RxCRC());
    EXPECT_EQ(resp.Payload<std::uint16_t>(), 44332);
    EXPECT_TRUE(events.WaitFor(MessageEvents::RESPONSE_RECEIVED));
}

TEST_F(ConnectionManagerTest, WriteReachesDevice)
{
    HostReport w(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 42);
    w.Payload<std::uint16_t>(0xBEEF);
    ASSERT_TRUE(cm->SendMsgBlocking(w).Done());
    EXPECT_EQ(dev->Reg(Target::Synthesiser, 42), 0xBEEF);
}

TEST(ConnectionManagerClosed, SendIsRejectedWhenNotConnected)
{
    auto cm = CM_Sim::Create(std::make_shared<SimDevice>());
    EXPECT_FALSE(cm->Open());
    EXPECT_EQ(cm->SendMsg(RegRead(HostReport::Actions::CTRLR_REG, 0)), NullMessage);
    EXPECT_FALSE(cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0)).Done());
}

TEST_F(ConnectionManagerTest, PipelinedRequestsMatchTheirResponses)
{
    constexpr int N = 300;
    for (int i = 0; i < N; i++) dev->WriteRegs(Target::Controller, static_cast<std::uint16_t>(100 + i), { static_cast<std::uint16_t>(i * 7) });

    std::vector<MessageHandle> handles;
    for (int i = 0; i < N; i++) handles.push_back(cm->SendMsg(RegRead(HostReport::Actions::CTRLR_REG, static_cast<std::uint16_t>(100 + i))));

    for (int i = 0; i < N; i++) {
        ASSERT_TRUE(WaitDone(handles[i])) << "message " << i;
        EXPECT_EQ(cm->Response(handles[i]).Payload<std::uint16_t>(), i * 7) << "message " << i;
    }
}

TEST_F(ConnectionManagerTest, ByteAtATimeDeliveryIsReassembled)
{
    dev->SetChunkSize(1);
    dev->SetLatency(200us);
    for (int i = 0; i < 20; i++) {
        auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::SYNTH_REG, 5, 8));
        ASSERT_TRUE(resp.Done());
        EXPECT_EQ(resp.Payload<std::vector<std::uint16_t>>()[0], 0x0105);
    }
}

TEST_F(ConnectionManagerTest, MissingResponseTimesOut)
{
    dev->InjectFault(Fault::Drop);
    const auto t0 = std::chrono::steady_clock::now();
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    const auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_FALSE(resp.Done());
    EXPECT_GE(elapsed, std::chrono::milliseconds(kRxTimeoutMs));
    EXPECT_TRUE(events.WaitFor(MessageEvents::RESPONSE_TIMED_OUT));
}

TEST_F(ConnectionManagerTest, RecoversAfterTimeout)
{
    dev->InjectFault(Fault::Drop);
    EXPECT_FALSE(cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0)).Done());
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(resp.Done());
    EXPECT_EQ(resp.Payload<std::uint16_t>(), 44332);
}

// DeviceReport::Done() is also true for a response that failed its CRC, and most callers of
// SendMsgBlocking only test Done(), so a corrupt response is returned as not done
TEST_F(ConnectionManagerTest, CorruptResponseIsNotReturnedAsDone)
{
    dev->InjectFault(Fault::CorruptCrc);
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    EXPECT_FALSE(resp.Done());
    EXPECT_TRUE(events.WaitFor(MessageEvents::RESPONSE_ERROR_CRC));
}

TEST_F(ConnectionManagerTest, CorruptResponseIsFlaggedOnTheMessage)
{
    dev->InjectFault(Fault::CorruptCrc);
    auto h = cm->SendMsg(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(events.WaitFor(MessageEvents::RESPONSE_ERROR_CRC));
    EXPECT_TRUE(cm->Response(h).RxCRC());
}

struct DeviceErrorCase {
    Fault fault;
    const char* name;
};

class ConnectionManagerDeviceError : public ConnectionManagerTest, public ::testing::WithParamInterface<DeviceErrorCase> {};

TEST_P(ConnectionManagerDeviceError, ReportedAsValidErrorResponse)
{
    dev->InjectFault(GetParam().fault);
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    EXPECT_TRUE(resp.Done());
    EXPECT_FALSE(resp.RxCRC());
    EXPECT_TRUE(resp.GeneralError() || resp.TxCRC() || resp.TxTimeout());
    EXPECT_TRUE(events.WaitFor(MessageEvents::RESPONSE_ERROR_VALID));
    EXPECT_EQ(events.Count(MessageEvents::RESPONSE_RECEIVED), 0u);
}

INSTANTIATE_TEST_SUITE_P(Faults, ConnectionManagerDeviceError, ::testing::Values(
    DeviceErrorCase{ Fault::GeneralError, "GeneralError" },
    DeviceErrorCase{ Fault::TxCrc, "TxCrc" },
    DeviceErrorCase{ Fault::NhfTimeout, "NhfTimeout" }
), [](const auto& p) { return std::string(p.param.name); });

TEST_F(ConnectionManagerTest, InterlockAlarmRaisesEventButResponseIsUsable)
{
    dev->InjectFault(Fault::Alarm);
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(resp.Done());
    EXPECT_TRUE(resp.HardwareAlarm());
    EXPECT_EQ(resp.Payload<std::uint16_t>(), 44332);
    EXPECT_TRUE(events.WaitFor(MessageEvents::INTERLOCK_ALARM_SET));
    EXPECT_TRUE(events.WaitFor(MessageEvents::RESPONSE_RECEIVED));
}

TEST_F(ConnectionManagerTest, StrayByteIsReportedAndResponseStillParsed)
{
    dev->InjectFault(Fault::Garbage);
    auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(resp.Done());
    EXPECT_EQ(resp.Payload<std::uint16_t>(), 44332);
    ASSERT_TRUE(events.WaitFor(MessageEvents::UNEXPECTED_RX_CHAR));
    EXPECT_EQ(events.Events(MessageEvents::UNEXPECTED_RX_CHAR)[0].param & 0xFF, 0xEE);
}

TEST_F(ConnectionManagerTest, InterruptWithOneWord)
{
    dev->RaiseInterrupt(2, { 0x34, 0x12 });
    ASSERT_TRUE(events.WaitFor(MessageEvents::INTERRUPT_RECEIVED));
    auto e = events.Events(MessageEvents::INTERRUPT_RECEIVED)[0];
    EXPECT_EQ(e.param, (2 << 16) | 0x1234);
}

TEST_F(ConnectionManagerTest, InterruptWithTwoWords)
{
    dev->RaiseInterrupt(1, { 0x01, 0x00, 0x02, 0x00 });
    ASSERT_TRUE(events.WaitFor(MessageEvents::INTERRUPT_RECEIVED));
    auto e = events.Events(MessageEvents::INTERRUPT_RECEIVED)[0];
    EXPECT_EQ(e.param, (1 << 16) | 1);
    EXPECT_EQ(e.param2, 2);
}

// Regression: interrupts with more than two words of payload were queued but never triggered,
// because the dispatch compared against the wrong payload type
TEST_F(ConnectionManagerTest, InterruptWithDataPayloadIsDelivered)
{
    const std::vector<std::uint8_t> data = { 1, 2, 3, 4, 5, 6 };
    dev->RaiseInterrupt(3, data);
    ASSERT_TRUE(events.WaitFor(MessageEvents::INTERRUPT_RECEIVED));
    auto e = events.Events(MessageEvents::INTERRUPT_RECEIVED)[0];
    EXPECT_EQ(e.param, 3 << 16);
    EXPECT_EQ(e.data, data);
}

TEST_F(ConnectionManagerTest, InterruptBetweenResponsesDoesNotDisturbThem)
{
    for (int i = 0; i < 10; i++) {
        dev->RaiseInterrupt(0, { static_cast<std::uint8_t>(i), 0 });
        auto resp = cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
        ASSERT_TRUE(resp.Done()) << i;
        EXPECT_EQ(resp.Payload<std::uint16_t>(), 44332) << i;
    }
    EXPECT_TRUE(events.WaitFor(MessageEvents::INTERRUPT_RECEIVED, 10));
}

TEST_F(ConnectionManagerTest, UnsubscribedHandlerReceivesNothing)
{
    ims_test::EventRecorder other;
    cm->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, &other);
    cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(other.WaitFor(MessageEvents::RESPONSE_RECEIVED));
    cm->MessageEventUnsubscribe(MessageEvents::RESPONSE_RECEIVED, &other);
    cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0));
    ASSERT_TRUE(events.WaitFor(MessageEvents::RESPONSE_RECEIVED, 2));
    EXPECT_EQ(other.Count(MessageEvents::RESPONSE_RECEIVED), 1u);
}

TEST_F(ConnectionManagerTest, DisconnectDisablesInterruptsAndReconnectWorks)
{
    for (int i = 0; i < 20; i++) {
        cm->Disconnect();
        EXPECT_FALSE(cm->Open());
        auto intren = dev->Requests(HostReport::Actions::CTRLR_INTREN);
        ASSERT_FALSE(intren.empty());
        EXPECT_FALSE(intren.back().read);
        EXPECT_EQ(intren.back().Words(), (std::vector<std::uint16_t>{ 0, 0 }));

        cm->Connect("SIM0");
        ASSERT_TRUE(cm->Open());
        ASSERT_TRUE(cm->SendMsgBlocking(RegRead(HostReport::Actions::CTRLR_REG, 0)).Done()) << "cycle " << i;
    }
}

TEST_F(ConnectionManagerTest, DisconnectWithUnansweredMessageCompletes)
{
    dev->InjectFault(Fault::Drop, 2); // the request and the INTREN-disable both go unanswered
    cm->SendMsg(RegRead(HostReport::Actions::CTRLR_REG, 0));
    const auto t0 = std::chrono::steady_clock::now();
    cm->Disconnect();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 2s);
    EXPECT_FALSE(cm->Open());
}

// BUG: responses carry no sequence number and CM_Common hands each one to the oldest waiting
// message. When one response in a pipeline is lost, every later response is attributed to the
// request before it -- reported as Done() with no error -- until the lost one times out. Device
// reports echo the request address, which could be used to detect the mismatch (the HIL suite
// checks that real firmware does echo it). Enable once responses are validated.
TEST_F(ConnectionManagerTest, DISABLED_LostResponseInPipelineDoesNotShiftLaterResponses)
{
    for (int i = 0; i < 6; i++) dev->WriteRegs(Target::Controller, static_cast<std::uint16_t>(100 + i), { static_cast<std::uint16_t>(1000 + i) });
    dev->SetHandler([this](const Request& r) -> std::optional<Response> {
        if (r.addr == 102) dev->InjectFault(Fault::Drop);
        return std::nullopt;
    });

    std::vector<MessageHandle> h;
    for (int i = 0; i < 6; i++) h.push_back(cm->SendMsg(RegRead(HostReport::Actions::CTRLR_REG, static_cast<std::uint16_t>(100 + i))));
    ASSERT_TRUE(WaitDone(h[5], 1s));

    for (int i = 0; i < 6; i++) {
        auto r = cm->Response(h[i]);
        if (i == 2) {
            EXPECT_FALSE(r.Done()) << "lost response must not be satisfied by another one";
        } else {
            ASSERT_TRUE(r.Done()) << "request " << i;
            EXPECT_EQ(r.Payload<std::uint16_t>(), 1000 + i) << "request " << i;
        }
    }
}

// Regression: completed messages are kept for autoFreeTimeout (10 s) and the parser used to walk all
// of them several times per pass, so the cost of each message grew with the number sent in the
// last 10 s (~130 us per blocking read at first, ~1.1 ms after 3000). The parser now only walks
// pending messages and frees completed ones periodically.
TEST_F(ConnectionManagerTest, RoundTripCostDoesNotGrowWithMessageHistory)
{
    const auto rpt = RegRead(HostReport::Actions::CTRLR_REG, 0);
    auto block = [&] {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 500; i++) cm->SendMsgBlocking(rpt);
        return std::chrono::steady_clock::now() - t0;
    };

    // Best of three, so a burst of load from tests running in parallel can't decide the result
    auto best = [&] { return std::min({ block(), block(), block() }); };

    const auto first = best();
    for (int i = 0; i < 2; i++) block();
    const auto last = best();
    EXPECT_LT(last, first * 3) << "first 1500: " << std::chrono::duration_cast<std::chrono::milliseconds>(first).count()
                               << " ms per 500, after 1000 more: " << std::chrono::duration_cast<std::chrono::milliseconds>(last).count() << " ms per 500";
}

// Regression: transports mark a message SENT after writing it, by which time the parser may already
// have completed it; setStatus used to let that overwrite RX_OK, leaving the message to time out
TEST(Message, CompletedStatusIsNotOverwrittenByLateSent)
{
    Message m(RegRead(HostReport::Actions::CTRLR_REG, 0));
    m.setStatus(Message::Status::RX_OK);
    m.setStatus(Message::Status::SENT);
    EXPECT_EQ(m.getStatus(), Message::Status::RX_OK);
    EXPECT_TRUE(m.waitForCompletion(std::chrono::milliseconds(0)));
}

TEST(Message, SendTimeIsSetWhenSent)
{
    Message m(RegRead(HostReport::Actions::CTRLR_REG, 0));
    m.setStatus(Message::Status::SENT);
    EXPECT_LT(m.TimeElapsed(), std::chrono::milliseconds(1000));
}

// Regression: the handle counter was a plain int, incremented from several threads
TEST(Message, HandlesAreUniqueAcrossThreads)
{
    std::vector<MessageHandle> a, b;
    auto make = [](std::vector<MessageHandle>& out) {
        for (int i = 0; i < 20000; i++) out.push_back(Message(HostReport()).getMessageHandle());
    };
    std::thread t1(make, std::ref(a)), t2(make, std::ref(b));
    t1.join();
    t2.join();
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end());
    EXPECT_EQ(std::adjacent_find(a.begin(), a.end()), a.end());
}

// Disconnect() used to clear DeviceIsOpen and notify the sender and memory transfer threads without
// holding the mutexes they wait on, so a thread that had just found the connection open could start
// waiting after the notification and never be joined. The window is too narrow to hit on demand
// (this passes against the old code too); it is kept as a stress check on the shutdown path.
TEST(ConnectionManagerCycles, RapidConnectDisconnectDoesNotHang)
{
    auto cm = CM_Sim::Create(SimDevice::iMSProWith_iMS4d());
    cm->SetTimeouts(100, kRxTimeoutMs, 10000, 100);
    for (int i = 0; i < 300; i++) {
        cm->Connect("SIM0");
        cm->Disconnect();
    }
    EXPECT_FALSE(cm->Open());
}
