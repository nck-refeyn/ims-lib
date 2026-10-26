/*-----------------------------------------------------------------------------
/ Title      : Connection Manager Header - Common Interface
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ConnectionManager/h/CM_Common.h $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2021-09-13 10:43:17 +0100 (Mon, 13 Sep 2021) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 501 $
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

#ifndef IMS_CM_COMMON_H__
#define IMS_CM_COMMON_H__

#include "IConnectionManager.h"
#include "MessageRegistry.h"
#include "PrivateUtil.h"  // for logging

#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <queue>
#include <array>
#include <variant>

using HRClock = std::chrono::high_resolution_clock;
using us = std::chrono::microseconds;

namespace iMS {

	enum class _FastTransferStatus {
		IDLE,
		DOWNLOADING,
		UPLOADING
	};

    enum class _ConnectionStatus {
        UNKNOWN,
        ALIVE,
        LOST
    };
        
    // This is another ABC that inherits its interface and adds some data and member functions
	//	that are common to all CM types
	class CM_Common : public IConnectionManager
	{
	public:
		CM_Common();

		void MessageEventSubscribe(const int message, IEventHandler* handler);
		void MessageEventUnsubscribe(const int message, const IEventHandler* handler);
		bool MemoryDownload(boost::container::deque<std::uint8_t>& arr, std::uint32_t start_addr, int image_index, const std::array<std::uint8_t, 16>& uuid);
		bool MemoryUpload(boost::container::deque<std::uint8_t>& arr, std::uint32_t start_addr, int len, int image_index, const std::array<std::uint8_t, 16>& uuid);
        void MemoryTransfer();
		int MemoryProgress();

		// Send an I/O Report
		virtual MessageHandle SendMsg(HostReport const& Rpt);
		virtual DeviceReport SendMsgBlocking(HostReport const& Rpt);

		const DeviceReport Response(const MessageHandle) const;
		const bool& Open() const;

	protected:
        struct DefaultPolicy {
            DefaultPolicy(uint32_t _addr, int _index) : addr(_addr), index(_index) {}

            static constexpr int DL_TRANSFER_SIZE = 64;
            static constexpr int UL_TRANSFER_SIZE = 64;
            static constexpr int TRANSFER_UNIT    = 64;
            static constexpr long DMA_MAX_TRANSACTION_SIZE = 1024;

            // Optional extra fields
            uint32_t addr = 0;
            int index = 0;
        };
        // Declare a class that stores information pertaining to the status of any fast memory transfers
		template <typename Policy = DefaultPolicy> 
        class FastTransfer {
        public:
            FastTransfer(boost::container::deque<uint8_t>& data, int len,
                        const Policy& policy = Policy{})
                : m_data(data), m_len(len), m_policy(policy),
                m_transCount(((m_len - 1) / Policy::TRANSFER_UNIT) + 1) 
            {
                m_data_it = m_data.cbegin();
                m_currentTrans = 0;
                startNextTransaction();
            }

            void startNextTransaction() {
                if (m_currentTrans < m_transCount) {
                    m_currentTrans++;
                    m_transBytesRemaining = (m_currentTrans == m_transCount)
                        ? (m_len - ((m_currentTrans - 1) * Policy::TRANSFER_UNIT))
                        : Policy::TRANSFER_UNIT;
                }
            }

            // Common members
            boost::container::deque<uint8_t>& m_data;
            const int m_len;
            typename boost::container::deque<uint8_t>::const_iterator m_data_it;

            const unsigned int m_transCount;
            unsigned int m_currentTrans;
            unsigned int m_transBytesRemaining;

            Policy m_policy;  // holds addr, index, uuid, etc.
        };

		MessageEventTrigger mMsgEvent;

		bool DeviceIsOpen{ false };

		// Message Sending Thread
		std::chrono::milliseconds sendTimeout;
		std::thread senderThread;
		std::queue<std::shared_ptr<Message>> m_queue;
		mutable std::mutex m_txmutex;
		std::condition_variable m_txcond;
		virtual void MessageSender() = 0;

		// Message Receiving Thread
		std::thread receiverThread;
		std::deque<char> m_rxCharQueue;
		mutable std::mutex m_rxmutex;
		std::condition_variable m_rxcond;
		virtual void ResponseReceiver() = 0;

		// Message List Manager Thread
		std::thread parserThread;
		std::chrono::milliseconds rxTimeout;
		std::chrono::milliseconds autoFreeTimeout;
        MessageRegistry<MessageHandle, Message> m_msgRegistry;
		virtual void MessageListManager();

		// Memory Transfer Thread
        std::atomic<_FastTransferStatus> FastTransferStatus{ _FastTransferStatus::IDLE };
		FastTransfer<DefaultPolicy> *fti = nullptr;
		std::thread memoryTransferThread;
		mutable std::mutex m_tfrmutex;
		std::condition_variable m_tfrcond;

        std::atomic<_ConnectionStatus> m_status {_ConnectionStatus::UNKNOWN};

        std::shared_ptr<IConnectionSettings> connSettings;

        // Clears DeviceIsOpen and wakes the threads waiting on CM_Common's condition variables, for
        // Disconnect(). Notifying without their mutexes could let a thread that has just found the
        // connection open start waiting after the notification, and never be joined.
        void SignalClosed();

    private:
    	template <typename T, typename T2 = int>
        struct triggerEvents
        {
            MessageEvents::Events e;
            T p;
            T2 p2;
            int count;
            triggerEvents(MessageEvents::Events e, T p) : e(e), p(p), p2(0), count(1) {};
            triggerEvents(MessageEvents::Events e, T p, T2 p2) : e(e), p(p), p2(p2), count(2) {};
        };

        void LogAndNotify(
            sev::severity_level level,
            const std::shared_ptr<Message>& msg,
            const std::string& prefix,
            bool trace = true,
            bool notify = true);

        template<typename P>
        void LogNotifyEvent(
            sev::severity_level level,
            const std::shared_ptr<Message>& msg,
            const std::string& prefix,
            MessageEvents::Events evType,
            const P& param);

        struct PendingEvent {
            MessageEvents::Events type;  // which event

            std::variant<
                int,
                std::pair<int, int>,
                std::pair<int, std::vector<std::uint8_t>>
            > payload;           // what data to send
        };

        template <typename P>
        void PushEvent(MessageEvents::Events e, const P& payload);
        void PushInterruptEvent(int p, const std::vector<uint8_t>& data);

		std::deque<std::uint8_t> m_glblRx;            
		// Record any trigger events that occur during processing 
		std::vector<triggerEvents<int>> m_Events;
		std::vector<triggerEvents<int, std::vector<std::uint8_t>>> m_Vevents;
        std::vector<PendingEvent> m_PendingEvents;

        void HandleResponseDone(std::shared_ptr<Message> m);
        bool HandleUnexpectedChar(std::shared_ptr<Message> m, char c);
        void HandleTimeoutsAndCleanup();
        void TriggerPendingEvents();
        // Receive path: frames reports out of m_glblRx (resynchronising past damaged bytes), then
        // gives each to the request it answers
        void ProcessReceivedReports();
        void DispatchReport(const DeviceReport& rpt);
        void DiscardRxByte();
        void HandleBufferedMessage(const std::shared_ptr<Message>& m);
        std::chrono::steady_clock::time_point m_lastRxBytes{};
        bool m_discardingRx = false;
        std::chrono::steady_clock::time_point m_lastCleanup{};
	};


}
#endif
