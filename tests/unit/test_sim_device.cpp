// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// The simulator is test infrastructure; these tests pin down its behaviour so the
// higher-level tests can rely on it. They drive it with real HostReport bytes and
// decode its output with the real DeviceReport parser.

#include "DeviceReport.h"
#include "HostReport.h"
#include "SimDevice.h"

#include <gtest/gtest.h>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    void Send(SimDevice& dev, HostReport r)
    {
        const auto& s = r.SerialStream();
        dev.HostWrite(s.data(), s.size());
    }

    std::vector<std::uint8_t> Drain(SimDevice& dev, std::chrono::milliseconds timeout = 50ms)
    {
        std::vector<std::uint8_t> out;
        std::uint8_t buf[256];
        while (std::size_t n = dev.HostRead(buf, sizeof(buf), timeout)) out.insert(out.end(), buf, buf + n);
        return out;
    }

    // Parses the first complete report out of `bytes`, consuming it
    DeviceReport TakeReport(std::vector<std::uint8_t>& bytes)
    {
        DeviceReport r;
        std::size_t i = 0;
        while (i < bytes.size() && !r.Done()) r.Parse(bytes[i++]);
        bytes.erase(bytes.begin(), bytes.begin() + i);
        return r;
    }

    HostReport RegRead(HostReport::Actions a, std::uint16_t addr, std::uint16_t len)
    {
        HostReport r(a, HostReport::Dir::READ, addr);
        ReportFields f = r.Fields();
        f.len = len;
        r.Fields(f);
        return r;
    }

}

TEST(SimDevice, PresetIdentityIsReadable)
{
    auto dev = SimDevice::iMSProWith_iMS4d();
    Send(*dev, RegRead(HostReport::Actions::CTRLR_REG, 0, 2));
    auto out = Drain(*dev);
    auto r = TakeReport(out);
    ASSERT_TRUE(r.Done());
    EXPECT_FALSE(r.RxCRC());
    EXPECT_EQ(r.Fields().ID, ReportTypes::DEVICE_REPORT_ID_CTRLR);
    EXPECT_EQ(r.Payload<std::uint16_t>(), 44332);
    EXPECT_TRUE(out.empty());
}

TEST(SimDevice, RegistersAreWordAddressed)
{
    SimDevice dev;
    HostReport w(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 10);
    w.Payload<std::vector<std::uint16_t>>({ 0x1111, 0x2222, 0x3333 });
    Send(dev, w);
    EXPECT_EQ(dev.Reg(Target::Synthesiser, 11), 0x2222);
    EXPECT_EQ(dev.Reg(Target::Controller, 11), 0); // separate register files

    Drain(dev);
    Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 11, 4));
    auto out = Drain(dev);
    auto r = TakeReport(out);
    EXPECT_EQ(r.Payload<std::vector<std::uint16_t>>(), (std::vector<std::uint16_t>{ 0x2222, 0x3333 }));
}

TEST(SimDevice, DecodesActionBanksAndLogsRequests)
{
    SimDevice dev;
    HostReport pll(HostReport::Actions::PLL_REF, HostReport::Dir::WRITE, 0);
    pll.Payload<std::uint16_t>(1);
    HostReport ws(HostReport::Actions::WAVE_SHAPING, HostReport::Dir::WRITE, 0);
    ws.Payload<std::uint16_t>(2);

    Send(dev, HostReport(HostReport::Actions::FW_UPGRADE, HostReport::Dir::READ, 0));
    Send(dev, pll);
    Send(dev, HostReport(HostReport::Actions::CTRLR_FW_UPGRADE, HostReport::Dir::READ, 2));
    Send(dev, ws);

    auto log = dev.Requests();
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0].action, HostReport::Actions::FW_UPGRADE);
    EXPECT_TRUE(log[0].read);
    EXPECT_EQ(log[1].action, HostReport::Actions::PLL_REF);
    EXPECT_EQ(log[2].action, HostReport::Actions::CTRLR_FW_UPGRADE);
    EXPECT_EQ(log[2].target, Target::Controller);
    EXPECT_EQ(log[3].action, HostReport::Actions::WAVE_SHAPING);
}

TEST(SimDevice, RejectsHostFrameWithBadCrcAndReportsIt)
{
    SimDevice dev;
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 3);
    r.Payload<std::uint16_t>(0xAAAA);
    auto bytes = r.SerialStream();
    bytes.back() ^= 0xFF;
    dev.HostWrite(bytes.data(), bytes.size());

    EXPECT_EQ(dev.RejectedFrames(), 1u);
    EXPECT_TRUE(dev.Requests().empty());
    EXPECT_EQ(dev.Reg(Target::Synthesiser, 3), 0);
    auto out = Drain(dev);
    auto resp = TakeReport(out);
    ASSERT_TRUE(resp.Done());
    EXPECT_TRUE(resp.TxCRC());
}

TEST(SimDevice, ResynchronisesOnGarbageAndAcceptsSplitWrites)
{
    SimDevice dev;
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 3);
    r.Payload<std::uint16_t>(0x1234);
    std::vector<std::uint8_t> bytes = { 0x00, 0xFF, 0x77 };
    const auto& s = r.SerialStream();
    bytes.insert(bytes.end(), s.begin(), s.end());
    for (auto b : bytes) dev.HostWrite(&b, 1);
    EXPECT_EQ(dev.Reg(Target::Synthesiser, 3), 0x1234);
}

TEST(SimDevice, HandlerOverridesDefaultModel)
{
    SimDevice dev;
    dev.SetHandler([](const Request& r) -> std::optional<Response> {
        if (r.action == HostReport::Actions::AOD_TEMP) return Response::Words({ 0x0190 });
        return std::nullopt;
    });
    Send(dev, HostReport(HostReport::Actions::AOD_TEMP, HostReport::Dir::READ, 0));
    Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 2));
    auto out = Drain(dev);
    EXPECT_EQ(TakeReport(out).Payload<std::uint16_t>(), 0x0190);
    EXPECT_EQ(TakeReport(out).Payload<std::uint16_t>(), 0); // fell through to default
}

TEST(SimDevice, StatefulHandlerKeepsItsState)
{
    SimDevice dev;
    dev.SetHandler([n = 0](const Request&) mutable -> std::optional<Response> {
        return Response::Words({ static_cast<std::uint16_t>(++n) });
    });
    for (int i = 0; i < 3; i++) Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 2));
    auto out = Drain(dev);
    EXPECT_EQ(TakeReport(out).Payload<std::uint16_t>(), 1);
    EXPECT_EQ(TakeReport(out).Payload<std::uint16_t>(), 2);
    EXPECT_EQ(TakeReport(out).Payload<std::uint16_t>(), 3);
}

TEST(SimDevice, FaultsApplyInOrderThenClear)
{
    SimDevice dev;
    dev.InjectFault(Fault::GeneralError);
    dev.InjectFault(Fault::Alarm);
    for (int i = 0; i < 3; i++) Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 2));
    auto out = Drain(dev);
    EXPECT_TRUE(TakeReport(out).GeneralError());
    EXPECT_TRUE(TakeReport(out).HardwareAlarm());
    auto clean = TakeReport(out);
    EXPECT_FALSE(clean.GeneralError());
    EXPECT_FALSE(clean.HardwareAlarm());
}

TEST(SimDevice, DropAndAbsentTargetProduceNoOutput)
{
    SimDevice dev;
    dev.InjectFault(Fault::Drop);
    Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 2));
    dev.SetPresent(Target::Controller, false);
    Send(dev, RegRead(HostReport::Actions::CTRLR_REG, 0, 2));
    EXPECT_TRUE(Drain(dev, 20ms).empty());
    EXPECT_EQ(dev.Requests().size(), 2u);
}

TEST(SimDevice, ChunkSizeLimitsEachRead)
{
    SimDevice dev;
    dev.SetChunkSize(3);
    Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 8));
    std::uint8_t buf[64];
    EXPECT_EQ(dev.HostRead(buf, sizeof(buf), 10ms), 3u);
    EXPECT_EQ(dev.HostRead(buf, sizeof(buf), 10ms), 3u);
}

TEST(SimDevice, LatencyDelaysResponse)
{
    SimDevice dev;
    dev.SetLatency(30ms);
    Send(dev, RegRead(HostReport::Actions::SYNTH_REG, 0, 2));
    std::uint8_t buf[64];
    EXPECT_EQ(dev.HostRead(buf, sizeof(buf), 5ms), 0u);
    EXPECT_GT(dev.HostRead(buf, sizeof(buf), 100ms), 0u);
}

TEST(SimDevice, ImageMemoryUsesTransactionAddressing)
{
    SimDevice dev;
    HostReport w(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::WRITE, 2);
    w.Payload<std::vector<std::uint8_t>>(std::vector<std::uint8_t>(64, 0x5A));
    Send(dev, w);
    EXPECT_EQ(dev.Memory(HostReport::Actions::CTRLR_IMAGE, 127, 1)[0], 0);
    EXPECT_EQ(dev.Memory(HostReport::Actions::CTRLR_IMAGE, 128, 64), std::vector<std::uint8_t>(64, 0x5A));
}

TEST(SimDevice, InterruptFrameIsWellFormed)
{
    SimDevice dev;
    dev.RaiseInterrupt(2, { 7, 0 });
    auto out = Drain(dev);
    auto r = TakeReport(out);
    ASSERT_TRUE(r.Done());
    EXPECT_FALSE(r.RxCRC());
    EXPECT_EQ(r.Fields().ID, ReportTypes::INTERRUPT_REPORT_ID_CTRLR);
    EXPECT_EQ(r.Fields().addr, 2);
}

TEST(SimDevice, OddLengthRegisterReadRoundsDownToWords)
{
    auto dev = SimDevice::iCSA();
    Send(*dev, RegRead(HostReport::Actions::CTRLR_REG, 0, 3));
    auto out = Drain(*dev);
    auto r = TakeReport(out);
    EXPECT_EQ(r.Fields().len, 2);
}

TEST(SimDevice, ResponsesCarryZeroContext)
{
    SimDevice dev;
    HostReport fw(HostReport::Actions::FW_UPGRADE, HostReport::Dir::READ, 0);
    Send(dev, fw);
    auto out = Drain(dev);
    auto r = TakeReport(out);
    EXPECT_EQ(r.Fields().context, 0);
    EXPECT_EQ(r.Fields().len, 4) << "status reads have a fixed length, whatever len asks for";
}

TEST(SimDevice, ReadOnlyBitsAndUnsupportedActions)
{
    auto dev = SimDevice::iCSA();
    HostReport w(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, 74);
    w.Payload<std::uint16_t>(0x0010);
    Send(*dev, w);
    EXPECT_EQ(dev->Reg(Target::Controller, 74), 0x0011);

    Send(*dev, HostReport(HostReport::Actions::AOD_TEMP, HostReport::Dir::READ, 0));
    auto out = Drain(*dev);
    TakeReport(out); // the write's response
    EXPECT_TRUE(TakeReport(out).GeneralError());
}
