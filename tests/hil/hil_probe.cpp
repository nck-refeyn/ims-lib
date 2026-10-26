// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// Differential probe: sends a fixed set of read-only requests and prints every field of each
// response, one line per request. Built against hardware (ims_hil_probe) and the simulator
// (ims_hil_probe_sim) so the two outputs can be diffed to find where the simulator's model of
// the firmware differs from the real thing.

#include "HilTarget.h"
#include "TestSupport.h"

#include <cstdint>
#include "IMSConstants.h"

#include <cstdio>

using namespace iMS;

namespace {

    void Probe(IConnectionManager& conn, const char* name, HostReport::Actions a, std::uint16_t addr,
        std::uint16_t len, std::uint8_t context = 0)
    {
        HostReport r = hil::RegRead(a, addr, len);
        if (context) {
            ReportFields f = r.Fields();
            f.context = context;
            r.Fields(f);
        }
        const DeviceReport resp = conn.SendMsgBlocking(r);
        std::printf("%-16s addr=%-4u len=%-3u ctx=%u -> ", name, addr, len, context);
        if (!resp.Done()) {
            std::printf("NO RESPONSE\n");
            return;
        }
        const auto& f = resp.Fields();
        std::printf("id=%u hdr=0x%02X ctx=%u len=%u addr=%u%s data=", static_cast<unsigned>(f.ID), f.hdr, f.context,
            f.len, f.addr, resp.RxCRC() ? " CRCERR" : "");
        for (auto b : resp.Payload<std::vector<std::uint8_t>>()) std::printf("%02X", b);
        std::printf("\n");
    }

}

int main()
{
    ims_test::ConfigureLogging();
    auto* t = hil::Acquire();
    if (!t) {
        std::fprintf(stderr, "no iMS system found\n");
        return 1;
    }
    std::printf("# %s\n", t->description.c_str());
    auto conn = t->Conn();

    // Identity and version blocks
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, 0, 2);
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, 5, 8);
    Probe(*conn, "SYNTH_REG", HostReport::Actions::SYNTH_REG, 0, 2);
    Probe(*conn, "SYNTH_REG", HostReport::Actions::SYNTH_REG, 5, 8);
    // Odd and zero lengths, and a length beyond one payload
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, 0, 1);
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, 0, 0);
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, 0, 64);
    // Controller image/playback registers
    for (std::uint16_t a : { CTRLR_REG_NumPts, CTRLR_REG_OscFreq, CTRLR_REG_ImgDelay, CTRLR_REG_ImgModes,
                             CTRLR_REG_Img_Play, CTRLR_REG_Img_Ctrl, CTRLR_REG_Img_Progress, CTRLR_REG_FPIFormat })
        Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, a, 2);
    Probe(*conn, "CTRLR_REG", HostReport::Actions::CTRLR_REG, CTRLR_REG_UUID, 16);
    // Synth registers the library writes, read back
    for (std::uint16_t a : { SYNTH_REG_IO_Signal_Control, SYNTH_REG_Phase_Offset_Ch1, SYNTH_REG_Channel_Swap,
                             SYNTH_REG_ChannelDelay34, SYNTH_REG_DutyCycleEn, SYNTH_REG_DutyCycleWidth, SYNTH_REG_DutyCycleDelay })
        Probe(*conn, "SYNTH_REG", HostReport::Actions::SYNTH_REG, a, 2);
    // Image table
    Probe(*conn, "CTRLR_IMGIDX", HostReport::Actions::CTRLR_IMGIDX, 0, 2,
        static_cast<std::uint8_t>(HostReport::ImageIndexOperations::GET_TABLE_SIZE));
    Probe(*conn, "CTRLR_IMGIDX", HostReport::Actions::CTRLR_IMGIDX, 0, 49,
        static_cast<std::uint8_t>(HostReport::ImageIndexOperations::GET_ENTRY));
    // Firmware upgrade status as FirmwareUpgrade sends it (len from the HostReport default)
    {
        HostReport fw(HostReport::Actions::FW_UPGRADE, HostReport::Dir::READ, 0);
        Probe(*conn, "FW_UPGRADE", HostReport::Actions::FW_UPGRADE, 0, fw.Fields().len, 1);
        Probe(*conn, "FW_UPGRADE", HostReport::Actions::FW_UPGRADE, 0, 4, 1);
    }
    // Temperatures (as SystemFunc reads them)
    Probe(*conn, "AOD_TEMP", HostReport::Actions::AOD_TEMP, 0, 2);
    Probe(*conn, "RFA_TEMP", HostReport::Actions::RFA_TEMP, 0, 2);

    hil::Release();
    return 0;
}
