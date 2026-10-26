// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// HIL benchmarks. Built against real hardware (ims_bench) and against the simulator
// (ims_bench_sim); the simulator numbers isolate host-side library overhead.
//
//   ims_bench --benchmark_out=results.json --benchmark_out_format=json --benchmark_repetitions=5
//
// Benchmarks that change device state are skipped unless IMS_HIL_ALLOW_WRITE=1.

#include "HilTarget.h"
#include "DeviceReport.h"
#include "ReportManipulation.h"

#include <benchmark/benchmark.h>

using namespace iMS;

namespace {

    // Reconnects so each benchmark starts with an empty message registry. The library keeps
    // completed messages for 10 s and per-message cost grows with their number, so without this
    // one benchmark's traffic would slow down the next and results would depend on run order.
    hil::Target* AcquireOrSkip(benchmark::State& state)
    {
        auto* t = hil::Acquire();
        if (!t) {
            state.SkipWithError("no iMS system found; check IMS_HIL_INTERFACE / IMS_HIL_ADDRESS");
            return nullptr;
        }
        t->ims->Disconnect();
        t->ims->Connect();
        if (!t->ims->Open()) {
            state.SkipWithError("reconnect failed");
            return nullptr;
        }
        return t;
    }

}

// --- Link + firmware + library --------------------------------------------------

// Round trip of a single blocking register read
static void BM_RegisterReadLatency(benchmark::State& state)
{
    auto* t = AcquireOrSkip(state);
    if (!t) return;
    auto conn = t->Conn();
    const auto rpt = hil::RegRead(HostReport::Actions::CTRLR_REG, 0);

    std::int64_t failures = 0;
    for (auto _ : state) {
        if (!conn->SendMsgBlocking(rpt).Done()) failures++;
    }
    state.counters["failures"] = static_cast<double>(failures);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RegisterReadLatency)->UseRealTime()->Unit(benchmark::kMicrosecond);

// Throughput with N requests in flight. Responses arrive in order, so waiting for the last
// one covers the batch. Large N also exposes per-message bookkeeping cost in the library.
static void BM_PipelinedReads(benchmark::State& state)
{
    auto* t = AcquireOrSkip(state);
    if (!t) return;
    auto conn = t->Conn();
    const auto rpt = hil::RegRead(HostReport::Actions::CTRLR_REG, 0);
    const auto n = state.range(0);

    std::int64_t failures = 0;
    for (auto _ : state) {
        for (std::int64_t i = 0; i < n - 1; i++) conn->SendMsg(rpt);
        if (!conn->SendMsgBlocking(rpt).Done()) failures++;
    }
    state.counters["failures"] = static_cast<double>(failures);
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_PipelinedReads)->Arg(16)->Arg(256)->Arg(4096)->UseRealTime()->Unit(benchmark::kMillisecond);

// Public-API image download (FastTransfer controllers). Timed from StartDownload to DOWNLOAD_FINISHED.
static void BM_ImageDownload(benchmark::State& state)
{
    auto* t = AcquireOrSkip(state);
    if (!t) return;
    if (!hil::WritesAllowed()) {
        state.SkipWithMessage("changes device state; set IMS_HIL_ALLOW_WRITE=1");
        return;
    }
    if (!t->ims->Ctlr().GetCap().FastImageTransfer) {
        state.SkipWithMessage("controller has no image table");
        return;
    }

    std::int64_t bytes = 0;
    for (auto _ : state) {
        // A fresh image each time: an identical one would be found in the table and skipped
        const auto img = hil::RampImage(*t->ims, static_cast<std::size_t>(state.range(0)));
        const auto r = hil::DownloadImage(t->ims, img, false);
        hil::EraseImage(t->ims, img);
        if (!r.finished) {
            state.SkipWithError(r.error.c_str());
            break;
        }
        state.SetIterationTime(r.elapsed.count());
        bytes += r.bytes;
    }
    state.SetBytesProcessed(bytes);
}
BENCHMARK(BM_ImageDownload)->Arg(1000)->Arg(20000)->Arg(100000)->UseManualTime()->Unit(benchmark::kMillisecond);

// --- Host-only protocol cost (no device involved) --------------------------------

static void BM_SerializeHostReport(benchmark::State& state)
{
    const std::vector<std::uint8_t> payload(64, 0xA5);
    for (auto _ : state) {
        HostReport r(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::WRITE, 1);
        r.Payload<std::vector<std::uint8_t>>(payload);
        benchmark::DoNotOptimize(r.SerialStream().data());
    }
    state.SetBytesProcessed(state.iterations() * 64);
}
BENCHMARK(BM_SerializeHostReport);

static void BM_ParseDeviceReport(benchmark::State& state)
{
    HostReport src(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::WRITE, 1);
    src.Payload<std::vector<std::uint8_t>>(std::vector<std::uint8_t>(64, 0x5A));
    auto bytes = src.SerialStream();
    bytes[0] = static_cast<std::uint8_t>(ReportTypes::DEVICE_REPORT_ID_CTRLR);
    bytes[1] = 0x40;
    std::queue<std::uint8_t> q;
    for (std::size_t i = 0; i + 2 < bytes.size(); i++) q.push(bytes[i]);
    const auto crc = CRCGenerator(q).CRC();
    bytes[bytes.size() - 2] = static_cast<std::uint8_t>(crc & 0xFF);
    bytes[bytes.size() - 1] = static_cast<std::uint8_t>(crc >> 8);

    for (auto _ : state) {
        DeviceReport r;
        for (auto b : bytes) r.Parse(b);
        if (!r.Done() || r.RxCRC()) state.SkipWithError("parse failed");
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * 64);
}
BENCHMARK(BM_ParseDeviceReport);
