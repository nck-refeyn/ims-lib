// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// HIL backend for real hardware: discovers the system the same way SDK users do.

#include "HilTarget.h"
#include "ConnectionList.h"

#include <cstdlib>

namespace hil {

    namespace {
        std::unique_ptr<iMS::ConnectionList> g_list;
        std::unique_ptr<Target> g_target;

        std::string Env(const char* name)
        {
            const char* v = std::getenv(name);
            return v ? v : "";
        }
    }

    Target* Acquire()
    {
        if (g_target) return g_target.get();

        if (!g_list) g_list = std::make_unique<iMS::ConnectionList>();

        std::shared_ptr<iMS::IMSSystem> ims;
        const auto intf = Env("IMS_HIL_INTERFACE");
        const auto addr = Env("IMS_HIL_ADDRESS");
        if (!intf.empty()) {
            std::vector<std::string> hints;
            if (!addr.empty()) hints.push_back(addr);
            ims = g_list->Scan(intf, hints);
        } else {
            auto found = g_list->Scan();
            if (!found.empty()) ims = found.front();
        }
        if (!ims) return nullptr;

        ims->Connect();
        if (!ims->Open()) return nullptr;

        g_target = std::make_unique<Target>(Target{ ims, Describe(*ims), false });
        return g_target.get();
    }

    void Release()
    {
        if (g_target) g_target->ims->Disconnect();
        g_target.reset();
    }

}
