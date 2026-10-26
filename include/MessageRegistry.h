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
        }

        // Clear all
        void clear()
        {
            std::unique_lock lock(m_mutex);
            m_messages.clear();
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

        void notifyAll() {
            // Taking the wait mutex orders this after any waiter's predicate check, so a waiter
            // that has just found the predicate false is already waiting and gets woken
            { std::lock_guard<std::mutex> lock(m_waitMutex); }
            m_cv.notify_all();
        }        

        template<typename Pred>
        void waitUntil(Pred predicate) {
            std::unique_lock lock(m_waitMutex);
            m_cv.wait(lock, predicate);
        }        
    private:
        mutable std::shared_mutex m_mutex;
        MapType m_messages;

        std::mutex m_waitMutex;
        std::condition_variable m_cv;        
    };

}