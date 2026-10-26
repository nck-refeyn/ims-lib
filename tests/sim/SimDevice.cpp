// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "SimDevice.h"

#include <algorithm>
#include <iterator>

namespace iMS {
namespace sim {

    namespace {
        constexpr std::uint8_t ID_HOST_SYNTH = 1;
        constexpr std::uint8_t ID_DEV_SYNTH = 2;
        constexpr std::uint8_t ID_HOST_CTRLR = 4;
        constexpr std::uint8_t ID_DEV_CTRLR = 5;
        constexpr std::uint8_t ID_INTERRUPT = 73;
        constexpr std::size_t HEADER_LEN = 7;
        constexpr std::size_t PAYLOAD_MAX = 64;
        constexpr std::size_t IMAGE_TRANSFER_UNIT = 64;
        constexpr std::size_t IMAGE_ENTRY_NAME_LEN = 16;
        constexpr std::uint8_t IMAGE_ENTRY_STATUS = 0x02;  // as read from an iCSA

        void AppendU32(std::vector<std::uint8_t>& b, std::uint32_t v)
        {
            for (int i = 0; i < 4; i++) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }

        std::uint32_t ReadU32(const std::vector<std::uint8_t>& b, std::size_t at)
        {
            return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
        }

        int Index(Target t) { return t == Target::Controller ? 1 : 0; }

        std::vector<std::uint8_t> WordsToBytes(const std::vector<std::uint16_t>& words)
        {
            std::vector<std::uint8_t> b;
            b.reserve(words.size() * 2);
            for (auto w : words) {
                b.push_back(static_cast<std::uint8_t>(w & 0xFF));
                b.push_back(static_cast<std::uint8_t>(w >> 8));
            }
            return b;
        }
    }

    std::uint16_t Crc16(const std::uint8_t* data, std::size_t len)
    {
        std::uint16_t crc = 0;
        for (std::size_t i = 0; i < len; i++) {
            crc ^= static_cast<std::uint16_t>(data[i]) << 8;
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x8005) : static_cast<std::uint16_t>(crc << 1);
        }
        return crc;
    }

    std::vector<std::uint16_t> Request::Words() const
    {
        std::vector<std::uint16_t> w;
        for (std::size_t i = 0; i + 1 < payload.size(); i += 2)
            w.push_back(static_cast<std::uint16_t>(payload[i] | (payload[i + 1] << 8)));
        return w;
    }

    Response Response::Ok(std::vector<std::uint8_t> payload)
    {
        Response r;
        r.payload = std::move(payload);
        return r;
    }

    Response Response::Words(const std::vector<std::uint16_t>& words)
    {
        return Ok(WordsToBytes(words));
    }

    Response Response::Error(std::uint8_t status_bits)
    {
        Response r;
        r.status = status_bits;
        return r;
    }

    SimDevice::SimDevice() {}

    std::shared_ptr<SimDevice> SimDevice::iMSProWith_iMS4d()
    {
        auto dev = std::make_shared<SimDevice>();
        // v2.3 rev 120, built 2025-06-15 14:30
        dev->SetIdentity(Target::Controller, 44332, { 0x0203, 120, 15 | (6 << 5) | (25 << 9), 30 | (14 << 8) });
        // v1.5 rev 80 (>= 73 enables ChannelComp in the database)
        dev->SetIdentity(Target::Synthesiser, 59196, { 0x0105, 80, 15 | (6 << 5) | (25 << 9), 30 | (14 << 8) });
        return dev;
    }

    std::shared_ptr<SimDevice> SimDevice::iMSLiteWith_iMS4()
    {
        auto dev = std::make_shared<SimDevice>();
        dev->SetIdentity(Target::Controller, 1193, { 0x0102, 10, 1 | (1 << 5) | (20 << 9), 0 });
        dev->SetIdentity(Target::Synthesiser, 51195, { 0x0104, 20, 1 | (1 << 5) | (20 << 9), 0 });
        return dev;
    }

    // Power-up state read from iCSA2510 and iCSA2605 (firmware v1.1.30)
    std::shared_ptr<SimDevice> SimDevice::iCSA()
    {
        auto dev = std::make_shared<SimDevice>();
        dev->SetIdentity(Target::Controller, 18355, { 0x0101, 30, 0x34B2, 0x0D0A });
        dev->SetIdentity(Target::Synthesiser, 18355, { 0x0101, 30, 0x34B2, 0x0D0A });
        dev->WriteRegs(Target::Controller, 3, { 0xFFFF, 0xFFFF });
        // Register 18 varies between units (0x021E, 0x0033) and is not modelled
        dev->WriteRegs(Target::Controller, 54, { 0x0100 });  // Img_Ctrl
        // FPIFormat bit 0: read-only "MSB-first mode supported"
        dev->WriteRegs(Target::Controller, 74, { 0x0001 });
        dev->SetReadOnlyBits(Target::Controller, 74, 0x0001, 0x0001);
        dev->WriteRegs(Target::Synthesiser, 1, { 0x2000 });  // IO signal control
        dev->WriteRegs(Target::Synthesiser, 13, { 0x0006 }); // channel swap / pixel control
        dev->WriteRegs(Target::Synthesiser, 131, { 0xFFFF, 0xFFFF });  // duty cycle width, delay
        // No AOD or RF amplifier temperature sensors
        dev->SetUnsupported(HostReport::Actions::AOD_TEMP);
        dev->SetUnsupported(HostReport::Actions::RFA_TEMP);
        dev->SetPhantomImageSlot(true);
        dev->SetFirmwareAddressEcho(true);
        return dev;
    }

    void SimDevice::SetIdentity(Target t, std::uint16_t magic, const std::array<std::uint16_t, 4>& version)
    {
        WriteRegs(t, 0, { magic });
        WriteRegs(t, 5, { version[0], version[1], version[2], version[3] });
    }

    void SimDevice::SetPresent(Target t, bool present)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_present[Index(t)] = present;
    }

    void SimDevice::WriteRegs(Target t, std::uint16_t addr, const std::vector<std::uint16_t>& words)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (std::size_t i = 0; i < words.size(); i++)
            m_regs[Index(t)][static_cast<std::uint16_t>(addr + i)] = words[i];
    }

    std::uint16_t SimDevice::Reg(Target t, std::uint16_t addr) const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return RegLocked(t, addr);
    }

    std::uint16_t SimDevice::RegLocked(Target t, std::uint16_t addr) const
    {
        auto& regs = m_regs[Index(t)];
        auto it = regs.find(addr);
        std::uint16_t v = it == regs.end() ? 0 : it->second;
        auto ro = m_readOnly[Index(t)].find(addr);
        if (ro != m_readOnly[Index(t)].end())
            v = static_cast<std::uint16_t>((v & ~ro->second.first) | (ro->second.second & ro->second.first));
        return v;
    }

    void SimDevice::SetReadOnlyBits(Target t, std::uint16_t addr, std::uint16_t mask, std::uint16_t value)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_readOnly[Index(t)][addr] = { mask, value };
    }

    void SimDevice::SetUnsupported(HostReport::Actions a)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_unsupported.push_back(static_cast<std::uint8_t>(a));
    }

    void SimDevice::SetPhantomImageSlot(bool phantom)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_phantomSlot = phantom;
    }

    void SimDevice::SetFirmwareAddressEcho(bool on)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_firmwareEcho = on;
    }

    std::vector<ImageEntry> SimDevice::ImageTable() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_images;
    }

    void SimDevice::WriteMemory(HostReport::Actions a, std::uint32_t offset, const std::vector<std::uint8_t>& data)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto& mem = m_mem[static_cast<std::uint8_t>(a)];
        if (mem.size() < offset + data.size()) mem.resize(offset + data.size());
        std::copy(data.begin(), data.end(), mem.begin() + offset);
    }

    std::vector<std::uint8_t> SimDevice::Memory(HostReport::Actions a, std::uint32_t offset, std::size_t len) const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::uint8_t> out(len, 0);
        auto it = m_mem.find(static_cast<std::uint8_t>(a));
        if (it == m_mem.end()) return out;
        for (std::size_t i = 0; i < len && offset + i < it->second.size(); i++)
            out[i] = it->second[offset + i];
        return out;
    }

    void SimDevice::SetHandler(Handler h)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_handler = h ? std::make_shared<Handler>(std::move(h)) : nullptr;
    }

    void SimDevice::InjectFault(Fault f, int count)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (int i = 0; i < count; i++) m_faults.push_back(f);
    }

    void SimDevice::SetLatency(std::chrono::microseconds latency)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_latency = latency;
    }

    void SimDevice::SetChunkSize(std::size_t bytes)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_chunk = bytes;
    }

    void SimDevice::RaiseInterrupt(std::uint16_t addr, const std::vector<std::uint8_t>& payload)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        QueueLocked(Encode(ID_INTERRUPT, status::DataOk, 0, addr, payload));
    }

    std::vector<Request> SimDevice::Requests() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_log;
    }

    std::vector<Request> SimDevice::Requests(HostReport::Actions a) const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<Request> out;
        std::copy_if(m_log.begin(), m_log.end(), std::back_inserter(out), [a](const Request& r) { return r.action == a; });
        return out;
    }

    void SimDevice::ClearRequests()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_log.clear();
    }

    bool SimDevice::WaitForRequests(std::size_t n, std::chrono::milliseconds timeout) const
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        // Requests are logged before they are applied, so also wait for the device to finish them
        return m_cv.wait_for(lk, timeout, [&] { return m_log.size() >= n && m_processing == 0; });
    }

    std::size_t SimDevice::RejectedFrames() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_rejected;
    }

    void SimDevice::HostWrite(const std::uint8_t* data, std::size_t len)
    {
        std::vector<std::vector<std::uint8_t>> frames;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            for (std::size_t i = 0; i < len; i++) {
                const std::uint8_t b = data[i];
                // Firmware resynchronises by discarding bytes until a valid report ID
                if (m_rxFrame.empty() && b != ID_HOST_SYNTH && b != ID_HOST_CTRLR) continue;
                m_rxFrame.push_back(b);
                if (m_rxFrame.size() < HEADER_LEN) continue;

                const bool read = (m_rxFrame[1] & 0x80) != 0;
                const std::size_t len_field = m_rxFrame[3] | (m_rxFrame[4] << 8);
                const std::size_t payload_len = read ? 0 : std::min(len_field, PAYLOAD_MAX);
                if (m_rxFrame.size() == HEADER_LEN + payload_len + 2) {
                    frames.push_back(std::move(m_rxFrame));
                    m_rxFrame.clear();
                }
            }
        }
        for (auto& f : frames) ProcessFrame(f);
    }

    // The user handler is invoked without the lock held so it may call back into the device
    void SimDevice::ProcessFrame(const std::vector<std::uint8_t>& f)
    {
        Request r;
        const bool ctrl = f[0] == ID_HOST_CTRLR;
        r.target = ctrl ? Target::Controller : Target::Synthesiser;
        r.hdr = f[1];
        r.read = (f[1] & 0x80) != 0;
        r.context = f[2];
        r.len = static_cast<std::uint16_t>(f[3] | (f[4] << 8));
        r.addr = static_cast<std::uint16_t>(f[5] | (f[6] << 8));
        r.payload.assign(f.begin() + HEADER_LEN, f.end() - 2);

        std::uint8_t code = static_cast<std::uint8_t>((ctrl ? 0x10 : 0x00) | (f[1] & 0x0F));
        // Context bit 0 selects the 0x20 action bank (FW_UPGRADE, WAVE_SHAPING, CTRLR_FW_UPGRADE)
        if ((r.context & 1) && (code == 0x00 || code == 0x01 || code == 0x10)) code |= 0x20;
        r.action = static_cast<HostReport::Actions>(code);

        const std::uint8_t resp_id = ctrl ? ID_DEV_CTRLR : ID_DEV_SYNTH;
        const std::uint8_t action_bits = f[1] & 0x0F;
        const std::uint16_t crc_rx = static_cast<std::uint16_t>(f[f.size() - 2] | (f[f.size() - 1] << 8));
        const bool crc_ok = Crc16(f.data(), f.size() - 2) == crc_rx;

        std::shared_ptr<Handler> handler;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!crc_ok) {
                m_rejected++;
                QueueLocked(Encode(resp_id, action_bits, 0, r.addr, {}));
                m_cv.notify_all();
                return;
            }
            m_log.push_back(r);
            m_cv.notify_all();
            if (!m_present[Index(r.target)]) return;
            handler = m_handler;
            m_processing++;
        }

        std::optional<Response> resp;
        if (handler) resp = (*handler)(r);

        std::lock_guard<std::mutex> lk(m_mutex);
        // Waiters see this only once the lock is released, after the request has been applied
        m_processing--;
        m_cv.notify_all();
        if (!resp) resp = DefaultModelLocked(r);

        std::optional<Fault> fault;
        if (!m_faults.empty()) {
            fault = m_faults.front();
            m_faults.pop_front();
        }

        if (fault == Fault::Drop) return;

        std::uint8_t st = resp->status;
        if (fault == Fault::GeneralError) st = status::GeneralError;
        else if (fault == Fault::TxCrc) st = 0;
        else if (fault == Fault::NhfTimeout) st |= status::NhfTimeout;
        else if (fault == Fault::Alarm) st |= status::Alarm;

        if (fault == Fault::Garbage) QueueLocked({ 0xEE });
        std::uint16_t addr = resp->addr.value_or(r.addr);
        if (m_firmwareEcho && !resp->addr) {
            if (!(st & status::DataOk))
                addr = m_lastResponseAddr;
            else if (!r.read && r.action == HostReport::Actions::CTRLR_REG)
                addr = static_cast<std::uint16_t>(r.addr + r.len / 2);
        }
        m_lastResponseAddr = addr;
        auto bytes = Encode(resp_id, static_cast<std::uint8_t>(st | action_bits), 0, addr, resp->payload);
        if (fault == Fault::CorruptCrc) bytes.back() ^= 0xFF;
        QueueLocked(std::move(bytes));
    }

    Response SimDevice::DefaultModelLocked(const Request& r)
    {
        const auto code = static_cast<std::uint8_t>(r.action);
        if (std::find(m_unsupported.begin(), m_unsupported.end(), code) != m_unsupported.end())
            return Response::Error(status::GeneralError);

        if (r.action == HostReport::Actions::CTRLR_IMGIDX) return ImageIndexLocked(r);

        if (r.action == HostReport::Actions::CTRLR_REG || r.action == HostReport::Actions::SYNTH_REG) {
            auto& regs = m_regs[Index(r.target)];
            if (r.read) {
                std::vector<std::uint16_t> words;
                for (std::uint16_t i = 0; i < r.len / 2; i++)
                    words.push_back(RegLocked(r.target, static_cast<std::uint16_t>(r.addr + i)));
                return Response::Words(words);
            }
            auto words = r.Words();
            for (std::size_t i = 0; i < words.size(); i++)
                regs[static_cast<std::uint16_t>(r.addr + i)] = words[i];
            return Response::Ok();
        }

        std::size_t len = r.len;
        if (r.read && (r.action == HostReport::Actions::FW_UPGRADE || r.action == HostReport::Actions::CTRLR_FW_UPGRADE))
            len = r.addr == 1 ? 2 : 4;  // status (0), free space (1), checksum (2)

        std::uint32_t offset = r.addr;
        if (r.action == HostReport::Actions::CTRLR_IMAGE)
            offset = ((static_cast<std::uint32_t>(r.context) << 16) | r.addr) * IMAGE_TRANSFER_UNIT;

        auto& mem = m_mem[static_cast<std::uint8_t>(r.action)];
        if (r.read) {
            std::vector<std::uint8_t> out(len, 0);
            for (std::size_t i = 0; i < out.size() && offset + i < mem.size(); i++) out[i] = mem[offset + i];
            return Response::Ok(out);
        }
        if (mem.size() < offset + r.payload.size()) mem.resize(offset + r.payload.size());
        std::copy(r.payload.begin(), r.payload.end(), mem.begin() + offset);
        return Response::Ok();
    }

    Response SimDevice::ImageIndexLocked(const Request& r)
    {
        using Op = HostReport::ImageIndexOperations;
        const bool phantom = m_phantomSlot && m_images.empty();

        switch (static_cast<Op>(r.context)) {
        case Op::GET_TABLE_SIZE:
            return Response::Words({ static_cast<std::uint16_t>(phantom ? 1 : m_images.size()) });

        case Op::GET_ENTRY:
            if (phantom || r.addr >= m_images.size()) return Response::Error(status::GeneralError);
            return Response::Ok(m_images[r.addr].bytes);

        case Op::ADD_ENTRY: {
            // [UUID(16) | bytes(4) | points(4) | format(4) | name(16)]
            if (r.payload.size() < 28) return Response::Error(status::GeneralError);
            std::uint16_t handle = 0;
            while (std::any_of(m_images.begin(), m_images.end(), [&](const ImageEntry& e) { return e.handle == handle; }))
                handle++;
            const std::uint32_t size = ReadU32(r.payload, 16);
            const std::uint32_t address = m_nextImageAddress;
            m_nextImageAddress += static_cast<std::uint32_t>((size + IMAGE_TRANSFER_UNIT - 1) / IMAGE_TRANSFER_UNIT * IMAGE_TRANSFER_UNIT);

            ImageEntry e{ handle, std::vector<std::uint8_t>(r.payload.begin(), r.payload.begin() + 28) };
            AppendU32(e.bytes, address);
            e.bytes.push_back(IMAGE_ENTRY_STATUS);
            std::vector<std::uint8_t> name(r.payload.begin() + 28, r.payload.end());
            name.resize(IMAGE_ENTRY_NAME_LEN, ' ');
            e.bytes.insert(e.bytes.end(), name.begin(), name.end());
            m_images.push_back(std::move(e));
            m_phantomSlot = false;

            Response resp;
            AppendU32(resp.payload, address);
            resp.addr = handle;
            return resp;
        }

        case Op::CHECK_UUID: {
            auto it = std::find_if(m_images.begin(), m_images.end(), [&](const ImageEntry& e) {
                return r.payload.size() >= 16 && std::equal(r.payload.begin(), r.payload.begin() + 16, e.bytes.begin());
            });
            if (it == m_images.end()) return Response::Error(status::GeneralError);
            return Response::Words({ it->handle });
        }

        case Op::DEL_ENTRY: {
            auto it = std::find_if(m_images.begin(), m_images.end(), [&](const ImageEntry& e) { return e.handle == r.addr; });
            if (it == m_images.end()) return Response::Error(status::GeneralError);
            m_images.erase(it);
            return Response::Ok();
        }

        case Op::ERASE_ALL:
            m_images.clear();
            m_nextImageAddress = 0;
            m_phantomSlot = false;
            return Response::Ok();
        }
        return Response::Error(status::GeneralError);
    }

    std::vector<std::uint8_t> SimDevice::Encode(std::uint8_t id, std::uint8_t hdr, std::uint8_t context,
        std::uint16_t addr, const std::vector<std::uint8_t>& payload)
    {
        const std::uint16_t len = static_cast<std::uint16_t>(payload.size());
        std::vector<std::uint8_t> b = {
            id, hdr, context,
            static_cast<std::uint8_t>(len & 0xFF), static_cast<std::uint8_t>(len >> 8),
            static_cast<std::uint8_t>(addr & 0xFF), static_cast<std::uint8_t>(addr >> 8)
        };
        b.insert(b.end(), payload.begin(), payload.end());
        const std::uint16_t crc = Crc16(b.data(), b.size());
        b.push_back(static_cast<std::uint8_t>(crc & 0xFF));
        b.push_back(static_cast<std::uint8_t>(crc >> 8));
        return b;
    }

    void SimDevice::InjectRaw(const std::vector<std::uint8_t>& bytes)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        QueueLocked(bytes);
    }

    void SimDevice::DropOutput(std::size_t skip, std::size_t count)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_dropSkip = skip;
        m_dropCount = count;
    }

    void SimDevice::QueueLocked(std::vector<std::uint8_t> bytes)
    {
        if (m_dropCount) {
            std::vector<std::uint8_t> kept;
            for (auto b : bytes) {
                if (m_dropSkip) { m_dropSkip--; kept.push_back(b); }
                else if (m_dropCount) m_dropCount--;
                else kept.push_back(b);
            }
            bytes.swap(kept);
            if (bytes.empty()) return;
        }
        auto ready = Clock::now() + m_latency;
        // Preserve ordering even if latency was reduced between responses
        if (!m_tx.empty()) ready = std::max(ready, m_tx.back().ready);
        m_tx.push_back({ ready, std::move(bytes) });
        m_cv.notify_all();
    }

    std::size_t SimDevice::HostRead(std::uint8_t* buf, std::size_t max, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        const auto deadline = Clock::now() + timeout;
        for (;;) {
            if (m_wake) {
                m_wake = false;
                return 0;
            }
            const auto now = Clock::now();
            if (!m_tx.empty() && m_tx.front().ready <= now) break;
            if (now >= deadline) return 0;
            const auto until = m_tx.empty() ? deadline : std::min(deadline, m_tx.front().ready);
            m_cv.wait_until(lk, until);
        }

        const std::size_t limit = m_chunk ? std::min(max, m_chunk) : max;
        const auto now = Clock::now();
        std::size_t n = 0;
        while (n < limit && !m_tx.empty() && m_tx.front().ready <= now) {
            auto& front = m_tx.front().bytes;
            const std::size_t take = std::min(limit - n, front.size() - m_txOffset);
            std::copy_n(front.begin() + m_txOffset, take, buf + n);
            n += take;
            m_txOffset += take;
            if (m_txOffset == front.size()) {
                m_tx.pop_front();
                m_txOffset = 0;
            }
        }
        return n;
    }

    void SimDevice::ResetLink()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_rxFrame.clear();
        m_tx.clear();
        m_txOffset = 0;
        m_wake = false;
    }

    void SimDevice::Wake()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_wake = true;
        m_cv.notify_all();
    }

}
}
