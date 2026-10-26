// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Wire protocol: CRC, host report serialisation, device report parsing.

#include "DeviceReport.h"
#include "HostReport.h"
#include "ReportManipulation.h"
#include "SimDevice.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <random>
#include <string>

using namespace iMS;

namespace {

    std::vector<std::uint8_t> DeviceFrame(std::uint8_t id, std::uint8_t hdr, std::uint16_t addr,
        const std::vector<std::uint8_t>& payload, std::uint8_t context = 0)
    {
        const auto len = static_cast<std::uint16_t>(payload.size());
        std::vector<std::uint8_t> b = { id, hdr, context,
            static_cast<std::uint8_t>(len & 0xFF), static_cast<std::uint8_t>(len >> 8),
            static_cast<std::uint8_t>(addr & 0xFF), static_cast<std::uint8_t>(addr >> 8) };
        b.insert(b.end(), payload.begin(), payload.end());
        const auto crc = sim::Crc16(b.data(), b.size());
        b.push_back(static_cast<std::uint8_t>(crc & 0xFF));
        b.push_back(static_cast<std::uint8_t>(crc >> 8));
        return b;
    }

    DeviceReport ParseAll(const std::vector<std::uint8_t>& bytes)
    {
        DeviceReport r;
        for (auto b : bytes) r.Parse(b);
        return r;
    }

    std::uint16_t LibraryCrc(const std::vector<std::uint8_t>& data)
    {
        std::queue<std::uint8_t> q;
        for (auto b : data) q.push(b);
        return CRCGenerator(q).CRC();
    }

}

// --- CRC -------------------------------------------------------------------

TEST(Crc, MatchesCrc16UmtsCheckValue)
{
    const std::string check = "123456789";
    EXPECT_EQ(LibraryCrc({ check.begin(), check.end() }), 0xFEE8);
}

TEST(Crc, AgreesWithIndependentFirmwareImplementation)
{
    std::mt19937 rng(1234);
    for (int len = 1; len < 80; len++) {
        std::vector<std::uint8_t> data(len);
        for (auto& b : data) b = static_cast<std::uint8_t>(rng());
        EXPECT_EQ(LibraryCrc(data), sim::Crc16(data.data(), data.size())) << "length " << len;
    }
}

// --- Host report serialisation ---------------------------------------------

TEST(HostReportSerialise, ControllerRegisterReadHasNoPayload)
{
    HostReport r(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, 5);
    ReportFields f = r.Fields();
    f.len = 8;
    r.Fields(f);

    const auto& s = r.SerialStream();
    ASSERT_EQ(s.size(), 9u);
    EXPECT_EQ(s[0], 4);    // HOST_REPORT_ID_CTRLR
    EXPECT_EQ(s[1], 0x81); // READ | (CTRLR_REG & 0x0F)
    EXPECT_EQ(s[2], 0);    // context
    EXPECT_EQ(s[3], 8);    // len lo
    EXPECT_EQ(s[4], 0);    // len hi
    EXPECT_EQ(s[5], 5);    // addr lo
    EXPECT_EQ(s[6], 0);    // addr hi
    const auto crc = sim::Crc16(s.data(), 7);
    EXPECT_EQ(s[7], crc & 0xFF);
    EXPECT_EQ(s[8], crc >> 8);
}

TEST(HostReportSerialise, SynthRegisterWriteIsLittleEndian)
{
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 0x0103);
    r.Payload<std::uint16_t>(0x1234);

    const auto& s = r.SerialStream();
    const std::vector<std::uint8_t> head(s.begin(), s.begin() + 9);
    EXPECT_EQ(head, (std::vector<std::uint8_t>{ 1, 0x07, 0, 2, 0, 0x03, 0x01, 0x34, 0x12 }));
}

TEST(HostReportSerialise, ActionBank0x20IsSelectedThroughContext)
{
    HostReport fw(HostReport::Actions::FW_UPGRADE, HostReport::Dir::READ, 0);
    EXPECT_EQ(fw.Fields().ID, ReportTypes::HOST_REPORT_ID_SYNTH);
    EXPECT_EQ(fw.Fields().hdr, 0x80);
    EXPECT_EQ(fw.Fields().context, 1);

    HostReport cfw(HostReport::Actions::CTRLR_FW_UPGRADE, HostReport::Dir::WRITE, 0);
    EXPECT_EQ(cfw.Fields().ID, ReportTypes::HOST_REPORT_ID_CTRLR);
    EXPECT_EQ(cfw.Fields().hdr, 0x00);
    EXPECT_EQ(cfw.Fields().context, 1);
}

TEST(HostReportSerialise, PayloadIsCappedAtMaximumLength)
{
    HostReport r(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::WRITE, 0);
    r.Payload<std::vector<std::uint8_t>>(std::vector<std::uint8_t>(100, 0xAB));
    EXPECT_EQ(r.SerialStream().size(), 7u + IOReport::PAYLOAD_MAX_LENGTH + 2u);
}

TEST(HostReportSerialise, SerialStreamIsCachedAfterFirstCall)
{
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 1);
    r.Payload<std::uint16_t>(1);
    const auto first = r.SerialStream();
    r.Payload<std::uint16_t>(2);
    EXPECT_EQ(r.SerialStream(), first);
}

// --- Host report default lengths --------------------------------------------

struct DefaultLenCase {
    HostReport::Actions action;
    HostReport::Dir dir;
    std::uint16_t addr;
    std::uint16_t expected_len;
};

class HostReportDefaultLength : public ::testing::TestWithParam<DefaultLenCase> {};

std::string DefaultLenName(const ::testing::TestParamInfo<DefaultLenCase>& p)
{
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Action%02X_%s_Addr%u", static_cast<unsigned>(p.param.action),
        p.param.dir == HostReport::Dir::READ ? "Read" : "Write", static_cast<unsigned>(p.param.addr));
    return buf;
}

TEST_P(HostReportDefaultLength, MatchesProtocol)
{
    const auto& c = GetParam();
    HostReport r(c.action, c.dir, c.addr);
    EXPECT_EQ(r.Fields().len, c.expected_len);
}

INSTANTIATE_TEST_SUITE_P(Actions, HostReportDefaultLength, ::testing::Values(
    DefaultLenCase{ HostReport::Actions::CTRLR_REG,     HostReport::Dir::READ,  0, 2 },
    DefaultLenCase{ HostReport::Actions::SYNTH_REG,     HostReport::Dir::WRITE, 0, 2 },
    DefaultLenCase{ HostReport::Actions::RF_POWER,      HostReport::Dir::WRITE, 0, 1 },
    DefaultLenCase{ HostReport::Actions::AOD_TEMP,      HostReport::Dir::READ,  0, 2 },
    DefaultLenCase{ HostReport::Actions::CTRLR_IMGDMA,  HostReport::Dir::WRITE, 0, 8 },
    DefaultLenCase{ HostReport::Actions::CTRLR_IMAGE,   HostReport::Dir::WRITE, 0, 64 },
    DefaultLenCase{ HostReport::Actions::LUT_ENTRY,     HostReport::Dir::WRITE, 0, 64 },
    DefaultLenCase{ HostReport::Actions::EXT_ADC,       HostReport::Dir::READ,  0, 16 },
    DefaultLenCase{ HostReport::Actions::EXT_ADC,       HostReport::Dir::WRITE, 0, 2 },
    DefaultLenCase{ HostReport::Actions::WAVE_SHAPING,  HostReport::Dir::WRITE, 0, 2 }
), DefaultLenName);

// Regression: the FW_UPGRADE / CTRLR_FW_UPGRADE case in HostReport's constructor was missing a
// `break` and fell through into WAVE_SHAPING, so every firmware-upgrade report got len=2.
INSTANTIATE_TEST_SUITE_P(FirmwareUpgrade, HostReportDefaultLength, ::testing::Values(
    DefaultLenCase{ HostReport::Actions::FW_UPGRADE,       HostReport::Dir::READ,  0, 4 },
    DefaultLenCase{ HostReport::Actions::FW_UPGRADE,       HostReport::Dir::READ,  1, 2 },
    DefaultLenCase{ HostReport::Actions::FW_UPGRADE,       HostReport::Dir::READ,  2, 4 },
    DefaultLenCase{ HostReport::Actions::FW_UPGRADE,       HostReport::Dir::WRITE, 0, 1 },
    DefaultLenCase{ HostReport::Actions::CTRLR_FW_UPGRADE, HostReport::Dir::READ,  0, 4 },
    DefaultLenCase{ HostReport::Actions::CTRLR_FW_UPGRADE, HostReport::Dir::WRITE, 1, 0 }
), DefaultLenName);

// --- Device report parsing --------------------------------------------------

TEST(DeviceReportParse, CompleteFrame)
{
    auto r = ParseAll(DeviceFrame(5, 0x41, 0x0030, { 0x34, 0x12, 0x78, 0x56 }, 3));
    ASSERT_TRUE(r.Done());
    EXPECT_FALSE(r.RxCRC());
    EXPECT_FALSE(r.GeneralError());
    EXPECT_FALSE(r.TxCRC());
    EXPECT_FALSE(r.TxTimeout());
    EXPECT_FALSE(r.HardwareAlarm());
    EXPECT_EQ(r.Fields().ID, ReportTypes::DEVICE_REPORT_ID_CTRLR);
    EXPECT_EQ(r.Fields().context, 3);
    EXPECT_EQ(r.Fields().addr, 0x30);
    EXPECT_EQ(r.Fields().len, 4);
    EXPECT_EQ(r.Payload<std::uint16_t>(), 0x1234);
    EXPECT_EQ(r.Payload<std::vector<std::uint16_t>>(), (std::vector<std::uint16_t>{ 0x1234, 0x5678 }));
}

TEST(DeviceReportParse, ZeroLengthPayload)
{
    auto r = ParseAll(DeviceFrame(2, 0x40, 7, {}));
    ASSERT_TRUE(r.Done());
    EXPECT_FALSE(r.RxCRC());
    EXPECT_TRUE(r.Payload<std::vector<std::uint8_t>>().empty());
}

TEST(DeviceReportParse, CorruptCrcIsDetected)
{
    auto bytes = DeviceFrame(2, 0x40, 0, { 1, 2, 3 });
    bytes[8] ^= 0x01; // flip a payload bit
    auto r = ParseAll(bytes);
    EXPECT_TRUE(r.Done());
    EXPECT_TRUE(r.RxCRC());
}

TEST(DeviceReportParse, NonReportIdIsUnexpected)
{
    DeviceReport r;
    r.Parse(0x55);
    EXPECT_TRUE(r.UnexpectedChar());
    EXPECT_TRUE(r.Idle());
    EXPECT_FALSE(r.Done());
}

TEST(DeviceReportParse, RecoversAfterResetParser)
{
    DeviceReport r;
    r.Parse(0x55);
    ASSERT_TRUE(r.UnexpectedChar());
    r.ResetParser();
    for (auto b : DeviceFrame(5, 0x40, 0, { 9, 0 })) r.Parse(b);
    EXPECT_TRUE(r.Done());
    EXPECT_EQ(r.Payload<std::uint16_t>(), 9);
}

TEST(DeviceReportParse, BytesAfterCompletionAreIgnored)
{
    auto bytes = DeviceFrame(5, 0x40, 0, { 1, 0 });
    auto r = ParseAll(bytes);
    for (auto b : DeviceFrame(5, 0x40, 0, { 2, 0 })) r.Parse(b);
    EXPECT_EQ(r.Payload<std::uint16_t>(), 1);
}

TEST(DeviceReportParse, InterruptReportIsAccepted)
{
    auto r = ParseAll(DeviceFrame(73, 0x40, 2, { 1, 0 }));
    EXPECT_TRUE(r.Done());
    EXPECT_EQ(r.Fields().ID, ReportTypes::INTERRUPT_REPORT_ID_CTRLR);
}

struct StatusCase {
    const char* name;
    std::uint8_t hdr;
    bool general, txcrc, timeout, alarm;
};

class DeviceReportStatus : public ::testing::TestWithParam<StatusCase> {};

TEST_P(DeviceReportStatus, HeaderBitsDecode)
{
    const auto& c = GetParam();
    auto r = ParseAll(DeviceFrame(2, c.hdr, 0, {}));
    ASSERT_TRUE(r.Done());
    EXPECT_EQ(r.GeneralError(), c.general);
    EXPECT_EQ(r.TxCRC(), c.txcrc);
    EXPECT_EQ(r.TxTimeout(), c.timeout);
    EXPECT_EQ(r.HardwareAlarm(), c.alarm);
}

INSTANTIATE_TEST_SUITE_P(Bits, DeviceReportStatus, ::testing::Values(
    StatusCase{ "DataOk",            0x40, false, false, false, false },
    StatusCase{ "ActionBitsIgnored", 0x4F, false, false, false, false },
    StatusCase{ "CrcError",          0x00, false, true,  false, false },
    StatusCase{ "GeneralError",      0x20, true,  false, false, false },
    StatusCase{ "NhfTimeout",        0x50, false, false, true,  false },
    StatusCase{ "InterlockAlarm",    0xC0, false, false, false, true  }
), [](const auto& p) { return std::string(p.param.name); });

// --- IOReport payload conversions ------------------------------------------

TEST(IOReportPayload, ScalarRoundTripsAndSetsLength)
{
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 0);
    r.Payload<std::uint32_t>(0xDEADBEEF);
    EXPECT_EQ(r.Fields().len, 4);
    EXPECT_EQ(r.Payload<std::vector<std::uint8_t>>(), (std::vector<std::uint8_t>{ 0xEF, 0xBE, 0xAD, 0xDE }));
    EXPECT_EQ(r.Payload<std::uint32_t>(), 0xDEADBEEFu);
}

TEST(IOReportPayload, VectorOfWordsIsPacked)
{
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 0);
    r.Payload<std::vector<std::uint16_t>>({ 1, 0x0203 });
    EXPECT_EQ(r.Fields().len, 4);
    EXPECT_EQ(r.Payload<std::vector<std::uint8_t>>(), (std::vector<std::uint8_t>{ 1, 0, 3, 2 }));
}

TEST(IOReportPayload, WordVectorDropsTrailingOddByte)
{
    HostReport r(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, 0);
    r.Payload<std::vector<std::uint8_t>>({ 1, 0, 2 });
    EXPECT_EQ(r.Payload<std::vector<std::uint16_t>>(), (std::vector<std::uint16_t>{ 1 }));
}

TEST(IOReportPayload, StringStopsAtFirstNonAlphanumeric)
{
    HostReport r(HostReport::Actions::SYNTH_EEPROM, HostReport::Dir::WRITE, 0);
    r.Payload<std::vector<std::uint8_t>>({ 'i', 'M', 'S', '4', 0, 'x' });
    EXPECT_EQ(r.Payload<std::string>(), "iMS4");
}
