// Copyright 2026 Isomet (UK) Ltd. All rights reserved.

#include "TestSupport.h"

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string>

namespace ims_test {

    void ConfigureLogging()
    {
        namespace logging = boost::log;
        using sev = logging::trivial::severity_level;

        const char* env = std::getenv("IMS_TEST_LOG");
        const std::string level = env ? env : "";
        if (level.empty() || level == "off") {
            logging::core::get()->set_logging_enabled(false);
            return;
        }

        sev min = sev::warning;
        if (level == "trace") min = sev::trace;
        else if (level == "debug") min = sev::debug;
        else if (level == "info") min = sev::info;
        else if (level == "error") min = sev::error;
        logging::core::get()->set_filter(logging::trivial::severity >= min);
    }

    void EventRecorder::EventAction(void*, const int message, const int param)
    {
        Record({ message, param, 0, {} });
    }

    void EventRecorder::EventAction(void*, const int message, const int param, const int param2)
    {
        Record({ message, param, param2, {} });
    }

    void EventRecorder::EventAction(void*, const int message, const double param)
    {
        Record({ message, static_cast<int>(param), 0, {} });
    }

    void EventRecorder::EventAction(void*, const int message, const int param, const std::vector<std::uint8_t> data)
    {
        Record({ message, param, 0, data });
    }

    void EventRecorder::Record(Event e)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_events.push_back(std::move(e));
        m_cv.notify_all();
    }

    std::size_t EventRecorder::Count(int message) const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return std::count_if(m_events.begin(), m_events.end(), [&](const Event& e) { return e.message == message; });
    }

    std::vector<EventRecorder::Event> EventRecorder::Events(int message) const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<Event> out;
        std::copy_if(m_events.begin(), m_events.end(), std::back_inserter(out), [&](const Event& e) { return e.message == message; });
        return out;
    }

    bool EventRecorder::WaitFor(int message, std::size_t n, std::chrono::milliseconds timeout) const
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [&] {
            return static_cast<std::size_t>(std::count_if(m_events.begin(), m_events.end(),
                [&](const Event& e) { return e.message == message; })) >= n;
        });
    }

    int EventRecorder::WaitForAny(std::initializer_list<int> messages, std::chrono::milliseconds timeout) const
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        int found = -1;
        m_cv.wait_for(lk, timeout, [&] {
            for (const auto& e : m_events)
                for (int m : messages)
                    if (e.message == m) { found = m; return true; }
            return false;
        });
        return found;
    }

}
