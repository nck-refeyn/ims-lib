// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "CM_Sim.h"
#include "IMSSystem.h"

namespace iMS {
namespace sim {

    CM_Sim::CM_Sim(std::shared_ptr<SimDevice> device) : m_device(std::move(device))
    {
        sendTimeout = std::chrono::milliseconds(100);
        rxTimeout = std::chrono::milliseconds(500);
        autoFreeTimeout = std::chrono::milliseconds(10000);
        connSettings = nullptr;
    }

    CM_Sim::~CM_Sim()
    {
        if (DeviceIsOpen) this->Disconnect();
    }

    std::shared_ptr<CM_Sim> CM_Sim::Create(std::shared_ptr<SimDevice> device)
    {
        auto instance = std::shared_ptr<CM_Sim>(new CM_Sim(std::move(device)));
        instance->m_self = instance;
        return instance;
    }

    const std::string& CM_Sim::Ident() const
    {
        return m_ident;
    }

    std::vector<std::shared_ptr<IMSSystem>> CM_Sim::Discover(const ListBase<std::string>&, std::shared_ptr<IConnectionSettings> settings)
    {
        connSettings = settings;
        std::vector<std::shared_ptr<IMSSystem>> found;
        this->Connect("SIM0");
        if (DeviceIsOpen) {
            auto ims = IMSSystem::Create(m_self.lock(), std::string("SIM0"));
            ims->Initialise();
            if (ims->Ctlr().IsValid() || ims->Synth().IsValid()) found.push_back(ims);
        }
        this->Disconnect();
        return found;
    }

    void CM_Sim::Connect(const std::string&)
    {
        if (DeviceIsOpen) return;

        m_device->ResetLink();
        m_msgRegistry.clear();
        while (!m_queue.empty()) m_queue.pop();
        {
            std::lock_guard<std::mutex> lock(m_rxmutex);
            m_rxCharQueue.clear();
        }

        DeviceIsOpen = true;
        senderThread = std::thread(&CM_Sim::MessageSender, this);
        receiverThread = std::thread(&CM_Sim::ResponseReceiver, this);
        parserThread = std::thread(&CM_Sim::MessageListManager, this);
        memoryTransferThread = std::thread(&CM_Common::MemoryTransfer, this);
    }

    void CM_Sim::Disconnect()
    {
        if (!DeviceIsOpen) return;

        // Same shutdown sequence as the hardware transports: disable interrupts, then drain
        HostReport iorpt(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
        iorpt.Payload<int>(0);
        this->SendMsg(iorpt);

        // Outstanding messages complete or time out within rxTimeout; allow some slack on top
        const auto deadline = std::chrono::steady_clock::now() + rxTimeout + std::chrono::seconds(1);
        for (;;) {
            bool pending;
            {
                std::lock_guard<std::mutex> lck{ m_txmutex };
                pending = !m_queue.empty();
            }
            m_msgRegistry.forEachMessage([&](const std::shared_ptr<Message>& msg) {
                if (!msg->isComplete()) pending = true;
            });
            if (!pending || std::chrono::steady_clock::now() > deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SignalClosed();
        m_device->Wake();

        senderThread.join();
        receiverThread.join();
        parserThread.join();
        memoryTransferThread.join();
    }

    void CM_Sim::SetTimeouts(int send_timeout_ms, int rx_timeout_ms, int free_timeout_ms, int)
    {
        sendTimeout = std::chrono::milliseconds(send_timeout_ms);
        rxTimeout = std::chrono::milliseconds(rx_timeout_ms);
        autoFreeTimeout = std::chrono::milliseconds(free_timeout_ms);
    }

    void CM_Sim::MessageSender()
    {
        while (DeviceIsOpen)
        {
            std::shared_ptr<Message> m;
            {
                std::unique_lock<std::mutex> lck{ m_txmutex };
                m_txcond.wait(lck, [&] { return !DeviceIsOpen || !m_queue.empty(); });
                if (!DeviceIsOpen) break;
                m = m_queue.front();
                m_queue.pop();
            }

            // Serialise first, as the hardware transports do: the parser may log (and so serialise)
            // any message it sees as SENT. Mark it sent before the bytes reach the device, which
            // can answer before HostWrite returns.
            const std::vector<std::uint8_t>& b = m->SerialStream();
            m->setStatus(Message::Status::SENT);
            m_device->HostWrite(b.data(), b.size());
        }
    }

    void CM_Sim::ResponseReceiver()
    {
        std::uint8_t buf[4096];
        while (DeviceIsOpen)
        {
            std::size_t n = m_device->HostRead(buf, sizeof(buf), std::chrono::milliseconds(20));
            if (n == 0) continue;
            {
                std::scoped_lock lock(m_rxmutex);
                m_rxCharQueue.insert(m_rxCharQueue.end(), buf, buf + n);
            }
            m_rxcond.notify_one();
        }
    }

}
}
