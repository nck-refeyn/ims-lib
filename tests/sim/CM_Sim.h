// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// CM_Sim: a connection manager whose "wire" is a SimDevice. It derives from
// CM_Common exactly as the hardware transports do, so the library's message
// queueing, response parsing, timeout handling, event dispatch and memory
// transfer code all run unmodified against the simulated device.

#ifndef IMS_CM_SIM_H__
#define IMS_CM_SIM_H__

#include "CM_Common.h"
#include "SimDevice.h"

namespace iMS {
namespace sim {

    class CM_Sim : public CM_Common
    {
    public:
        static std::shared_ptr<CM_Sim> Create(std::shared_ptr<SimDevice> device);
        ~CM_Sim();

        const std::string& Ident() const override;
        std::vector<std::shared_ptr<IMSSystem>> Discover(const ListBase<std::string>& PortMask, std::shared_ptr<IConnectionSettings> settings) override;
        void Connect(const std::string& port) override;
        void Disconnect() override;
        void SetTimeouts(int send_timeout_ms, int rx_timeout_ms, int free_timeout_ms, int discover_timeout_ms) override;

        SimDevice& Device() { return *m_device; }

    private:
        explicit CM_Sim(std::shared_ptr<SimDevice> device);

        void MessageSender() override;
        void ResponseReceiver() override;

        std::shared_ptr<SimDevice> m_device;
        std::weak_ptr<CM_Sim> m_self;
        const std::string m_ident = "CM_SIM";
    };

}
}

#endif
