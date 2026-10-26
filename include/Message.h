/*-----------------------------------------------------------------------------
/ Title      : Messaging Header
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Messaging/h/Message.h $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2017-11-15 13:31:25 +0000 (Wed, 15 Nov 2017) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 314 $
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

#ifndef IMS_MESSAGE_H__
#define IMS_MESSAGE_H__

#include "DeviceReport.h"
#include "HostReport.h"

#include <chrono>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>

namespace iMS
{
	typedef int MessageHandle;
	const MessageHandle NullMessage = -1;

	class Message
	{
	public:
		Message(HostReport const& Rpt);
		~Message();

		//bool operator == (const Message m);

		// Don't forget to update strings in .cpp
		enum class Status { UNSENT, SENT, SEND_ERROR, TIMEOUT_ON_SEND, RX_PARTIAL, INTERRUPT, RX_OK, CANCELLED, TIMEOUT_ON_RXCV, RX_ERROR_VALID, RX_ERROR_INVALID, PROCESSED_INTERRUPT };

		void setStatus(const Status s);
		Status getStatus() const;
		std::string getStatusText() const;
		MessageHandle getMessageHandle() const;
        bool isComplete() const;
		std::chrono::milliseconds TimeElapsed() const;
		std::chrono::milliseconds MsgDuration() const;
		std::chrono::milliseconds RxTimeSince(std::chrono::time_point<std::chrono::high_resolution_clock>& t) const;
        bool waitForCompletion(std::chrono::milliseconds timeout);
        void waitForCompletion();

		// Mirror Host & Device Report methods
		const std::vector<std::uint8_t>& SerialStream();
		void Parse(const std::uint8_t rxchar);
		char Parse();
		void ResetParser();

		// Allow Connection Manager to commit a buffer of unparsed bytes to the message
		// (Only used by CMs that can uniquely identify received chars as belonging to this message)
		void AddBuffer(const std::vector<std::uint8_t>& buf);
		bool HasData() const;

		// Return the Received Report to allow user to access data
		DeviceReport Response() const;

		// Fields of the report sent, for matching responses to requests
		const ReportFields& RequestFields() const;
		// Give this message a response that was parsed into another one
		void AssignResponse(const DeviceReport& resp);

	private:
		static const char * StatusEnumStrings[] ;

		void markSendTime();
		void markRecdTime();

		//Message(const Message&);  // Prevent copying
		HostReport m_rpt;
		DeviceReport m_resp;
		MessageHandle m_id;
		// Messages are created on several threads (callers, memory transfer, event handlers)
		static std::atomic<MessageHandle> mIDCount;
		std::atomic<Status> m_status{Status::UNSENT};
		std::chrono::time_point<std::chrono::high_resolution_clock> m_tm_sent;
		std::chrono::time_point<std::chrono::high_resolution_clock> m_tm_recd;
		std::deque<std::uint8_t> unparsed_buf;

        // Synchronisation
        mutable std::mutex m_waitMutex;
		mutable std::mutex m_parseMutex;
		std::condition_variable m_cv;
	};

}

#endif
