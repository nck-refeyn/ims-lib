// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#ifndef IMS_TEST_SUPPORT_H__
#define IMS_TEST_SUPPORT_H__

#include "IEventHandler.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <vector>

namespace ims_test {

    // Library logging is silenced unless IMS_TEST_LOG is set to one of
    // trace|debug|info|warning|error, in which case it goes to the console.
    void ConfigureLogging();

    // Records every event delivered to it; safe to use across library threads.
    class EventRecorder : public iMS::IEventHandler
    {
    public:
        struct Event {
            int message;
            int param;
            int param2;
            std::vector<std::uint8_t> data;
        };

        void EventAction(void* sender, const int message, const int param = 0) override;
        void EventAction(void* sender, const int message, const int param, const int param2) override;
        void EventAction(void* sender, const int message, const double param) override;
        void EventAction(void* sender, const int message, const int param, const std::vector<std::uint8_t> data) override;

        std::size_t Count(int message) const;
        std::vector<Event> Events(int message) const;
        // Wait until at least `n` events of type `message` have been seen
        bool WaitFor(int message, std::size_t n = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) const;
        // Wait for the first event of any of the given types; returns its type, or -1 on timeout
        int WaitForAny(std::initializer_list<int> messages, std::chrono::milliseconds timeout) const;

    private:
        void Record(Event e);

        mutable std::mutex m_mutex;
        mutable std::condition_variable m_cv;
        std::vector<Event> m_events;
    };

}

#endif
