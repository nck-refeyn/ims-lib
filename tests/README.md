# Tests

Two layers:

- **Unit/integration tests** run against a simulated iMS system. No hardware needed. They run in CI on every push and PR.
- **Hardware-in-the-loop (HIL) tests and benchmarks** run locally against a real iMS system. The same sources are also built against the simulator, so CI keeps them compiling and passing.

```
tests/
  sim/        SimDevice (firmware-level protocol model) and CM_Sim (a CM_Common transport bound to it)
  support/    test main, logging control, EventRecorder
  unit/       ims_unit_tests
  hil/        ims_hil_tests[_sim], ims_bench[_sim], compare_bench.py, baselines/
```

## Building and running

Tests are off by default. Enable them with `IMSLIB_BUILD_TESTS` (unit tests) and/or `IMSLIB_BUILD_HIL` (HIL tests, benchmarks and the hardware transports). GoogleTest and Google Benchmark come from `find_package` if they are installed, otherwise they are fetched and pinned by hash.

MSYS2 UCRT64 shell:

```bash
cmake -S . -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DIMSLIB_BUILD_TESTS=ON -DIMSLIB_BUILD_HIL=ON
cmake --build build/tests
ctest --test-dir build/tests -j 8 --output-on-failure      # unit + simulated HIL
ctest --test-dir build/tests -L unit                       # unit only
```

Use `RelWithDebInfo` on MinGW for now. Debug builds abort intermittently because of the `std::shared_mutex` issue described under [Known issues](#known-issues).

On Linux, run `bash build.sh` in `res/` before configuring. CMake tries to run it, but it is not marked executable in git.

Library logging is silenced in tests. Set `IMS_TEST_LOG=trace|debug|info|warning|error` to see it.

## The simulator

`SimDevice` consumes the raw bytes a transport would put on the wire, decodes host reports with its own frame parser and CRC (independent of the library's), and answers with device reports. By default:

- `CTRLR_REG` / `SYNTH_REG` are word-addressed register files.
- `CTRLR_IMAGE` is image memory addressed in 64-byte transactions (`context` holds the upper address bits).
- Every other action is byte-addressed memory. Reads return what was written, or zeros.
- `CTRLR_IMGIDX` is an image table (add, get by position, check UUID, delete by handle, erase all).
- Firmware details match an iCSA on v1.1.30: responses carry context 0, register reads round down to whole words, and firmware-upgrade status reads have a fixed length.
- `iMSProWith_iMS4d()`, `iMSLiteWith_iMS4()` and `iCSA()` preset magic numbers and versions that exist in the hardware database, so `IMSSystem::Initialise()` identifies them. `iCSA()` also reproduces the power-up state read from iCSA2510 and iCSA2605. That includes register reset values, the read-only MSB-first bit in `FPIFormat`, the missing temperature sensors, and the unreadable table slot reported after power-up. `ims_hil_probe` and `ims_hil_probe_sim` send the same read-only requests to hardware and to the simulator; diff their output to check the model against a unit.

`CM_Sim` derives from `CM_Common` exactly like `CM_FTDI` and the other transports do. Message queueing, response matching, timeouts, events and memory transfer therefore all run unmodified.

Scripting behaviour in a test:

```cpp
dev().SetHandler([](const sim::Request& r) -> std::optional<sim::Response> {
    if (r.action == HostReport::Actions::AOD_TEMP) return sim::Response::Words({ 0x0190 });
    return std::nullopt;                           // fall through to the default model
});
dev().InjectFault(sim::Fault::Drop);               // next answer never arrives -> RX timeout
dev().InjectFault(sim::Fault::CorruptCrc, 3);      // also GeneralError, TxCrc, NhfTimeout, Alarm, Garbage
dev().SetLatency(1ms); dev().SetChunkSize(3);      // slow, fragmented link
dev().SetPresent(sim::Target::Synthesiser, false); // synth never answers
dev().RaiseInterrupt(addr, payload);               // unsolicited interrupt report
auto writes = dev().Requests(HostReport::Actions::SYNTH_REG);  // what the library actually sent
```

`ims_test::SimSystemTest` (in `unit/SimFixture.h`) gives each test a connected, initialised `IMSSystem` on an iMS-Pro + iMS4d simulator.

## Bugs found by these tests

Each fix has a regression test, named in the second column.

| Problem | Regression test |
|---|---|
| Each `ImageDownload` wrote `FPIFormat << 1` instead of setting bit 1. Bit 0 is read-only 1, so the register filled with ones (0x0001 → 0xFFFF over ~16 downloads) until a power cycle. Seen on iCSA2510 and iCSA2605. On iCSA2605 the RF stayed correct with the register saturated. It now writes `CTRLR_REG_FPIFormat_MSB_Enable`, which also repairs a corrupted register. | `ICSAImageTest.RepeatedDownloadsOnlySetMsbEnableBit` |
| `UpdatePhaseTuning` cast its 14-bit phase word through `uint8_t`. | `SignalPathTest.PhaseTuningUsesFull14BitRange` |
| `HostReport` fell through from `FW_UPGRADE`/`CTRLR_FW_UPGRADE` into `WAVE_SHAPING`, so their default `len` was always 2. iCSA firmware returns 4 bytes regardless, so it was masked there. | `FirmwareUpgrade/HostReportDefaultLength.*` |
| `FWVersion` read four words from a vector of any size. | `FWVersion.WrongSizeYieldsInvalidVersion` |

Not changed:

- `res/build.sh` is not marked executable in git, so on Linux CMake cannot run it and `settings_ini.h` is never generated. Fix with `git update-index --chmod=+x res/build.sh`. CI runs the script explicitly meanwhile.
- `ImageDownload` (like `SequenceDownload`, `ToneBufferDownload`, `CompensationTableDownload` and `Auxiliary`) keeps a reference to the object it is given, as documented, so passing a temporary is undefined behaviour. A deleted rvalue overload would make that a compile error, but would also break existing code that compiles today.

## Known issues

These were found while writing the tests. Each bug has a `DISABLED_` test that asserts the intended behaviour; remove the prefix once it is fixed. To run them:
`ims_unit_tests --gtest_also_run_disabled_tests --gtest_filter=*DISABLED_*`

| Test | Problem |
|---|---|
| `ConnectionManagerTest.DISABLED_LostResponseInPipelineDoesNotShiftLaterResponses` | Responses have no sequence number. After one lost response, every later in-flight response is given to the request before it and reported as `Done()` with no error. |
| `MemoryTransferTest.DISABLED_LostResponseDuringUploadIsReported` | Because of the above, a lost response during `MemoryUpload` returns misplaced data and `MEMORY_TRANSFER_COMPLETE`; `MEMORY_TRANSFER_ERROR` is never raised. |
| `ConnectionManagerTest.DISABLED_RoundTripCostDoesNotGrowWithMessageHistory` | Completed messages stay in the registry for 10 s and are rescanned on every parser pass. Round-trip cost grows with recent traffic (~130 µs → ~1.1 ms after 3000 messages on a zero-latency link), and `CM_Common::MemoryTransfer` becomes quadratic. |
| `MemoryTransferTest.DISABLED_TransactionsBeyond64kUseContextForUpperAddressBits` | `CM_Common::MemoryTransfer` dereferences a message before checking it for null, so a message freed before it is harvested crashes it. The test frees completed messages after 1 s to avoid the quadratic cost above, which on a slow machine (the 32-bit Windows CI runner) can happen mid-transfer. |
| `ImageDownloadTest.DISABLED_DestroyingDownloaderRightAfterVerifyIsSafe` | `ImageDownload::Impl` declares its worker threads before its buffers, so the destructor frees the buffers before joining the workers. The verify worker is still running after it fires `VERIFY_SUCCESS`, so destroying the downloader on that event corrupts the heap (`0xc0000374`; 4 of 5 runs). Fix: stop the workers at the start of `~Impl()`. |

Not covered by a test:

- **MinGW `std::shared_mutex`**: concurrent *first* use of a `std::shared_mutex` fails in winpthreads with `EINVAL`. In Debug, GCC 15's libstdc++ assertions abort the process. In Release the lock silently isn't taken. It affects `MessageRegistry`, `Message` and `IEventTrigger`. Locking each mutex once in its owner's constructor, before other threads can see it, avoids it.
- `Message::setStatus` notifies without holding `m_waitMutex`, so the untimed `waitForCompletion()` used by `SendMsgBlocking` can miss its wakeup.
- `DeviceReport::Done()` is also true for CRC errors, and most callers check only `Done()`.

## Hardware-in-the-loop

`ims_hil_tests` and `ims_bench` find the system the way SDK users do (`ConnectionList::Scan`). The `_sim` builds of the same sources use the simulator instead. Environment:

| Variable | Meaning |
|---|---|
| `IMS_HIL_INTERFACE` | Connection module to scan, e.g. `CM_USBSS`, `CM_ETH`. Empty scans all. |
| `IMS_HIL_ADDRESS` | Address hint for that module, e.g. an IP address. |
| `IMS_HIL_ALLOW_WRITE` | `1` enables tests and benchmarks that change device state (image memory and table). Without it, only read-only tests run. |
| `IMS_SIM_LATENCY_US` | Simulator only: response latency to approximate a real link. |
| `IMS_SIM_PRESET` | Simulator only: `pro` (iMS-Pro + iMS4d, default), `lite` (iMS-Lite + iMS4) or `icsa`. |

Conformance tests (`test_hil.cpp`) cover identification, response address echo, repeated and pipelined reads, reconnect cycles, and image download + verify at 1/1k/20k points. `ResponsesEchoRequestAddress` checks something we haven't confirmed yet: whether firmware echoes the request address. If it does, the library could use it to detect the response mismatch above.

Benchmarks (`bench_hil.cpp`) measure register round trip, pipelined throughput (16/256/4096 in flight), image download time (1k/20k/100k points), and host-only serialise/parse cost. Each benchmark reconnects first, so it starts with an empty message registry.

```bash
ims_bench --benchmark_repetitions=5 --benchmark_out=bench.json --benchmark_out_format=json
python tests/hil/compare_bench.py tests/hil/baselines/<rig>.json bench.json --threshold 0.15
```

### Running against hardware

HIL runs locally, not in CI: CI only runs the `_sim` builds. With the system attached and its FTDI D2XX / Cypress USB drivers installed:

```bash
cmake -S . -B build/hil -G Ninja -DCMAKE_BUILD_TYPE=Release -DIMSLIB_BUILD_HIL=ON
cmake --build build/hil
IMS_HIL_ALLOW_WRITE=1 build/hil/tests/hil/ims_hil_tests
```

### Baselines

Compare against a `bench.json` from the same rig on a known-good commit, rather than the previous run, so slow drift doesn't go unnoticed. `tests/hil/baselines/<rig>.json` is the place to commit one, in a PR that says why the numbers changed.
