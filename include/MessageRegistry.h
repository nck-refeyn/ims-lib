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

#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <functional>

namespace iMS {

    // Messages are kept until freed by the connection manager. Pending messages (those that may
    // still receive a response) are also kept in a separate index, so the parser's per-pass work
    // depends on the number in flight rather than on everything sent recently.
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
            std::unique_lock lock(m_mutex);
            m_pending[handle] = msg;
            m_messages[handle] = std::move(msg);
        }

        // Find a message (shared read access)
        MessagePtr findMessage(HandleType handle) const
        {
            std::shared_lock lock(m_mutex);
            auto it = m_messages.find(handle);
            if (it != m_messages.end()) {
                return it->second;
            }
            return nullptr;
        }

        // Remove message by handle
        void removeMessage(HandleType handle)
        {
            std::unique_lock lock(m_mutex);
            m_messages.erase(handle);
            m_pending.erase(handle);
        }

        // Clear all
        void clear()
        {
            std::unique_lock lock(m_mutex);
            m_messages.clear();
            m_pending.clear();
        }

        // Check existence
        bool contains(HandleType handle) const
        {
            std::shared_lock lock(m_mutex);
            return m_messages.find(handle) != m_messages.end();
        }

        // Count (number of stored messages)
        size_t size() const
        {
            std::shared_lock lock(m_mutex);
            return m_messages.size();
        }

        // Safe iteration (read-only)
        void forEachMessage(const std::function<void(const MessagePtr&)>& fn) const
        {
            std::vector<MessagePtr> snapshot;

            {
                std::shared_lock lock(m_mutex);

                snapshot.reserve(m_messages.size());
                for (const auto& [handle, msg] : m_messages)
                    snapshot.push_back(msg);
            } // lock released here

            for (const auto& msg : snapshot)
                fn(msg);
        }

        // Optional: safe modification iteration.
        void forEachMessageMutable(const std::function<void(MessagePtr&)>& fn)
        {
            std::vector<MessagePtr> snapshot;

            {
                std::shared_lock lock(m_mutex);

                snapshot.reserve(m_messages.size());
                for (auto& [handle, msg] : m_messages)
                    snapshot.push_back(msg);
            } // lock released

            for (auto& msg : snapshot)
                fn(msg);
        }

        // Messages that may still receive a response, in handle order. Completed messages are
        // dropped from the pending index here, so it stays the size of what is in flight.
        void forEachPending(const std::function<void(const MessagePtr&)>& fn)
        {
            std::vector<MessagePtr> snapshot;
            {
                std::unique_lock lock(m_mutex);
                for (auto it = m_pending.begin(); it != m_pending.end();) {
                    if (it->second->isComplete()) {
                        it = m_pending.erase(it);
                    } else {
                        snapshot.push_back(it->second);
                        ++it;
                    }
                }
            }
            for (const auto& msg : snapshot)
                fn(msg);
        }

        // Removes every message for which pred returns true; pred runs under the lock
        void removeIf(const std::function<bool(const MessagePtr&)>& pred)
        {
            std::unique_lock lock(m_mutex);
            for (auto it = m_messages.begin(); it != m_messages.end();) {
                if (pred(it->second)) {
                    m_pending.erase(it->first);
                    it = m_messages.erase(it);
                } else {
                    ++it;
                }
            }
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
            std::unique_lock lock(m_waitMutex);
            while (!predicate()) m_cv.wait_for(lock, std::chrono::milliseconds(50));
        }        
    private:
        mutable std::shared_mutex m_mutex;
        MapType m_messages;
        MapType m_pending;

        std::mutex m_waitMutex;
        std::condition_variable m_cv;        
    };

}