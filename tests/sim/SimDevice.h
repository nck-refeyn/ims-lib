// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// SimDevice: a behavioural model of iMS controller/synthesiser firmware at the
// wire-protocol level. It consumes the raw byte stream a connection manager
// would put on USB/serial/ethernet, decodes host reports, and produces device
// report bytes. It is independent of the library's own serializer/parser so
// that the two sides check each other.
//
// Default behaviour (firmware details checked against an iCSA running v1.1.30):
//   - CTRLR_REG / SYNTH_REG are word-addressed register files (addr = register
//     index, payload = little-endian 16-bit words). Reads round `len` down to whole words.
//   - CTRLR_IMAGE is byte memory addressed in 64-byte transactions
//     (offset = ((context << 16) | addr) * 64).
//   - CTRLR_IMGIDX is an image table: ADD_ENTRY allocates a handle and memory, GET_ENTRY
//     reads by position, CHECK_UUID finds a handle, DEL_ENTRY / ERASE_ALL remove entries.
//   - FW_UPGRADE / CTRLR_FW_UPGRADE status reads have a fixed length whatever `len` asks for.
//   - Every other action is a byte-addressed memory per action (offset = addr).
//   - Reads return `len` bytes of current state (zero if never written).
//   - Writes store the payload and return an empty DATA_OK response.
//   - Responses echo the request address; their context is always 0.
// Tests customise behaviour with SetHandler(), InjectFault() and the presets.

#ifndef IMS_SIM_DEVICE_H__
#define IMS_SIM_DEVICE_H__

#include "HostReport.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace iMS {
namespace sim {

    enum class Target { Synthesiser, Controller };

    // A host->device frame as decoded by the simulated firmware
    struct Request {
        Target target;
        HostReport::Actions action;
        bool read;
        std::uint8_t hdr;
        std::uint8_t context;
        std::uint16_t len;
        std::uint16_t addr;
        std::vector<std::uint8_t> payload;

        // Payload interpreted as little-endian 16-bit words
        std::vector<std::uint16_t> Words() const;
    };

    // Status bits in the device report header
    namespace status {
        constexpr std::uint8_t Alarm        = 0x80;
        constexpr std::uint8_t DataOk       = 0x40;
        constexpr std::uint8_t GeneralError = 0x20;
        constexpr std::uint8_t NhfTimeout   = 0x10;
    }

    struct Response {
        std::vector<std::uint8_t> payload;
        std::uint8_t status = status::DataOk;
        // Address field of the response; the request's address if not set
        std::optional<std::uint16_t> addr;

        static Response Ok(std::vector<std::uint8_t> payload = {});
        static Response Words(const std::vector<std::uint16_t>& words);
        static Response Error(std::uint8_t status_bits);
    };

    enum class Fault {
        Drop,         // swallow the request, never answer
        CorruptCrc,   // answer with a bad CRC
        GeneralError, // answer with the general-error status
        TxCrc,        // answer claiming the host frame had a CRC error
        NhfTimeout,   // answer with the no-host-frame timeout flag
        Alarm,        // answer normally but with the interlock alarm bit set
        Garbage       // emit a stray byte ahead of the answer
    };

    // CRC-16 (poly 0x8005, init 0, MSB first, no reflection) as computed by firmware
    std::uint16_t Crc16(const std::uint8_t* data, std::size_t len);

    struct ImageEntry {
        std::uint16_t handle;
        // As GET_ENTRY returns it: UUID(16) size(4) points(4) format(4) address(4) status(1) name(16)
        std::vector<std::uint8_t> bytes;
    };

    class SimDevice
    {
    public:
        using Handler = std::function<std::optional<Response>(const Request&)>;
        using Clock = std::chrono::steady_clock;

        SimDevice();

        // Ready-made systems whose magic numbers exist in the hardware database
        static std::shared_ptr<SimDevice> iMSProWith_iMS4d();  // FastTransfer controller, 32-bit synth
        static std::shared_ptr<SimDevice> iMSLiteWith_iMS4();  // non-FastTransfer controller, 16-bit synth
        static std::shared_ptr<SimDevice> iCSA();              // compact synth: controller and 2-channel synth in one

        // --- Identity -------------------------------------------------------
        // Sets register 0 (magic) and registers 5..8 (version/build date)
        void SetIdentity(Target t, std::uint16_t magic, const std::array<std::uint16_t, 4>& version);
        // An absent target never answers (host sees an RX timeout)
        void SetPresent(Target t, bool present);

        // --- State ----------------------------------------------------------
        void WriteRegs(Target t, std::uint16_t addr, const std::vector<std::uint16_t>& words);
        // As the firmware reports it, read-only bits included
        std::uint16_t Reg(Target t, std::uint16_t addr) const;
        void WriteMemory(HostReport::Actions a, std::uint32_t offset, const std::vector<std::uint8_t>& data);
        std::vector<std::uint8_t> Memory(HostReport::Actions a, std::uint32_t offset, std::size_t len) const;
        std::vector<ImageEntry> ImageTable() const;

        // --- Behaviour ------------------------------------------------------
        // Consulted before the default model. Return std::nullopt to fall through.
        void SetHandler(Handler h);
        // Register bits in `mask` read as `value` whatever is written (e.g. capability flags)
        void SetReadOnlyBits(Target t, std::uint16_t addr, std::uint16_t mask, std::uint16_t value);
        // Requests for this action are answered with a general error, as for absent peripherals
        void SetUnsupported(HostReport::Actions a);
        // After power-up an iCSA reports one table slot whose entry can't be read; it goes away
        // with the first ADD_ENTRY or ERASE_ALL
        void SetPhantomImageSlot(bool phantom);
        // Applies to the next `count` requests that are answered
        void InjectFault(Fault f, int count = 1);
        // Delay between receiving a request and its answer becoming readable
        void SetLatency(std::chrono::microseconds latency);
        // Maximum bytes returned by a single HostRead (0 = unlimited)
        void SetChunkSize(std::size_t bytes);
        // Push an unsolicited interrupt report (ID 73) to the host
        void RaiseInterrupt(std::uint16_t addr, const std::vector<std::uint8_t>& payload);

        // --- Inspection -----------------------------------------------------
        std::vector<Request> Requests() const;
        std::vector<Request> Requests(HostReport::Actions a) const;
        void ClearRequests();
        bool WaitForRequests(std::size_t n, std::chrono::milliseconds timeout) const;
        // Host frames discarded because their CRC did not match
        std::size_t RejectedFrames() const;

        // --- Link (used by CM_Sim) -------------------------------------------
        void HostWrite(const std::uint8_t* data, std::size_t len);
        std::size_t HostRead(std::uint8_t* buf, std::size_t max, std::chrono::milliseconds timeout);
        // Drop any partially received frame and undelivered output
        void ResetLink();
        // Unblock any thread waiting in HostRead
        void Wake();

    private:
        struct Outbound {
            Clock::time_point ready;
            std::vector<std::uint8_t> bytes;
        };

        void ProcessFrame(const std::vector<std::uint8_t>& frame);
        Response DefaultModelLocked(const Request& r);
        Response ImageIndexLocked(const Request& r);
        std::uint16_t RegLocked(Target t, std::uint16_t addr) const;
        void QueueLocked(std::vector<std::uint8_t> bytes);
        static std::vector<std::uint8_t> Encode(std::uint8_t id, std::uint8_t hdr, std::uint8_t context,
            std::uint16_t addr, const std::vector<std::uint8_t>& payload);

        mutable std::mutex m_mutex;
        mutable std::condition_variable m_cv;

        std::map<std::uint16_t, std::uint16_t> m_regs[2];
        std::map<std::uint8_t, std::vector<std::uint8_t>> m_mem;
        bool m_present[2] = { true, true };
        std::map<std::uint16_t, std::pair<std::uint16_t, std::uint16_t>> m_readOnly[2];  // addr -> (mask, value)
        std::vector<std::uint8_t> m_unsupported;
        std::vector<ImageEntry> m_images;
        std::uint32_t m_nextImageAddress = 0;
        bool m_phantomSlot = false;

        // Shared so the unlocked call in ProcessFrame runs the stored handler, state and all
        std::shared_ptr<Handler> m_handler;
        std::deque<Fault> m_faults;
        std::chrono::microseconds m_latency{ 0 };
        std::size_t m_chunk = 0;

        std::vector<std::uint8_t> m_rxFrame;
        std::deque<Outbound> m_tx;
        std::size_t m_txOffset = 0;
        bool m_wake = false;

        std::vector<Request> m_log;
        std::size_t m_rejected = 0;
    };

}
}

#endif
