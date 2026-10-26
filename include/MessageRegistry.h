/*-----------------------------------------------------------------------------
/ Title      : Message Events Header
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/EventManager/h/MessageEvent.h $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2017-09-11 23:55:34 +0100 (Mon, 11 Sep 2017) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 300 $
/------------------------------------------------------------------------------
/ Description:
/------------------------------------------------------------------------------
/ Copyright (c) 2015 Isomet (UK) Ltd. All Rights Reserved.
/------------------------------------------------------------------------------
/ Revisions  :
/ Date        Version  Author  Description
/ 2015-04-09  1.0      dc      Created
/
/----------------------------------------------------------------------------*/


#pragma once

#include "Message.h"

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <functional>
#include <vector>

namespace iMS {

    // A plain mutex rather than std::shared_mutex: the critical sections are short, and the
    // winpthreads rwlock behind std::shared_mutex fails concurrent first use on MinGW.
    template<typename HandleType, typename MessageType>
    class MessageRegistry
    {
    public:
        using MessagePtr = std::shared_ptr<MessageType>;
        using MapType = std::map<HandleType, MessagePtr>;

        MessageRegistry() = default;
        ~MessageRegistry() = default;

        // Add or replace a message by handle
        void addMessage(HandleType handle, MessagePtr msg)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messages[handle] = std::move(msg);
        }

        MessagePtr findMessage(HandleType handle) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_messages.find(handle);
            if (it != m_messages.end()) {
                return it->second;
            }
            return nullptr;
        }

        void removeMessage(HandleType handle)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messages.erase(handle);
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messages.clear();
        }

        bool contains(HandleType handle) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_messages.find(handle) != m_messages.end();
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_messages.size();
        }

        // Every stored message, in handle order. The callback runs on a snapshot, without the lock.
        void forEachMessage(const std::function<void(const MessagePtr&)>& fn) const
        {
            std::vector<MessagePtr> snapshot;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                snapshot.reserve(m_messages.size());
                for (const auto& [handle, msg] : m_messages)
                    snapshot.push_back(msg);
            }
            for (const auto& msg : snapshot)
                fn(msg);
        }

        void forEachMessageMutable(const std::function<void(MessagePtr&)>& fn)
        {
            std::vector<MessagePtr> snapshot;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                snapshot.reserve(m_messages.size());
                for (auto& [handle, msg] : m_messages)
                    snapshot.push_back(msg);
            }
            for (auto& msg : snapshot)
                fn(msg);
        }

        void notifyAll() {
            // Taking the wait mutex orders this after any waiter's predicate check, so a waiter
            // that has just found the predicate false is already waiting and gets woken
            { std::lock_guard<std::mutex> lock(m_waitMutex); }
            m_cv.notify_all();
        }

        // Re-evaluates the predicate periodically as well as on notification, so a condition that
        // changes without a notification (e.g. a connection closing) cannot block the caller forever
        template<typename Pred>
        void waitUntil(Pred predicate) {
            std::unique_lock<std::mutex> lock(m_waitMutex);
            while (!predicate()) m_cv.wait_for(lock, std::chrono::milliseconds(50));
        }
    private:
        mutable std::mutex m_mutex;
        MapType m_messages;

        std::mutex m_waitMutex;
        std::condition_variable m_cv;
    };

}
