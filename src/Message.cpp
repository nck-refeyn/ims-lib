/*-----------------------------------------------------------------------------
/ Title      : I/O Messages Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Messaging/src/Message.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2020-07-30 21:50:24 +0100 (Thu, 30 Jul 2020) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 465 $
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

#include "Message.h"

#if defined _WIN32 && defined _DEBUG
#include "crtdbg.h"
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace iMS
{

	Message::Message(HostReport const& Rpt)
	{
		// Copy report into local object
		this->m_rpt = Rpt;

		// Unique ID integer for each new instance
		m_id = mIDCount++;

		m_status.store(Status::UNSENT, std::memory_order_release);
	}

	Message::~Message()
	{
	}

	const char * Message::StatusEnumStrings[] = {
		"UNSENT",
		"SENT",
		"SEND_ERROR",
		"TIMEOUT_ON_SEND",
		"RX_PARTIAL",
		"INTERRUPT",
		"RX_OK",
		"CANCELLED",
		"TIMEOUT_ON_RXCV",
		"RX_ERROR_VALID",
		"RX_ERROR_INVALID",
		"PROCESSED_INTERRUPT"
	};

	// Initialise ID Counter
	std::atomic<MessageHandle> Message::mIDCount{ 1 };
	
	void Message::markSendTime()
	{
		m_tm_sent = std::chrono::high_resolution_clock::now();
	}

	void Message::markRecdTime()
	{
		m_tm_recd = std::chrono::high_resolution_clock::now();
	}

	static bool isTerminal(Message::Status s)
	{
		return s != Message::Status::UNSENT &&
			s != Message::Status::SENT &&
			s != Message::Status::RX_PARTIAL &&
			s != Message::Status::INTERRUPT;
	}

	void Message::setStatus(const Message::Status s)
	{
        // Transports mark a message SENT after writing it, by which time the parser may already
        // have completed it; a completed message never goes back to an in-progress state
        Status cur = m_status.load(std::memory_order_acquire);
        do {
            if (isTerminal(cur) && !isTerminal(s)) return;
            // The send time must be set before the new status is visible: the parser times out
            // SENT messages by their elapsed time, and an unset time reads as decades ago
            if ((cur == Status::UNSENT) && (s > Status::UNSENT)) markSendTime();
        } while (!m_status.compare_exchange_weak(cur, s, std::memory_order_acq_rel, std::memory_order_acquire));

        if (!isTerminal(cur) && isTerminal(s)) {
            markRecdTime();
            // Taking the wait mutex orders this after a waiter's predicate check, so the
            // notification cannot fall between that check and the wait
            { std::lock_guard<std::mutex> lock(m_waitMutex); }
            m_cv.notify_all();
        }
	}

	Message::Status Message::getStatus() const
	{
        return m_status.load(std::memory_order_acquire);
	}

    bool Message::isComplete() const {
        auto s = this->getStatus();
        return isTerminal(s);
    }

    // Wait until status changes to RX_OK or timeout
    bool Message::waitForCompletion(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_waitMutex);
        return m_cv.wait_for(lock, timeout, [&]{ 
			return isTerminal(m_status.load(std::memory_order_acquire));
        });
    }

    void Message::waitForCompletion() {
        std::unique_lock<std::mutex> lock(m_waitMutex);
        m_cv.wait(lock, [&]{ 
			return isTerminal(m_status.load(std::memory_order_acquire));
        });
    }

	std::string Message::getStatusText() const
	{
		return std::string(Message::StatusEnumStrings[(int)getStatus()]);
	}

	MessageHandle Message::getMessageHandle() const
	{
		return m_id;
	}

	std::chrono::milliseconds Message::MsgDuration() const
	{
		return (std::chrono::duration_cast<std::chrono::milliseconds>(m_tm_recd - m_tm_sent));
	}

	std::chrono::milliseconds Message::TimeElapsed() const
	{
		std::chrono::time_point<std::chrono::high_resolution_clock> m_tm_now = std::chrono::high_resolution_clock::now();
		return (std::chrono::duration_cast<std::chrono::milliseconds>(m_tm_now - m_tm_sent));
	}

	std::chrono::milliseconds Message::RxTimeSince(std::chrono::time_point<std::chrono::high_resolution_clock>& t) const
	{
		return (std::chrono::duration_cast<std::chrono::milliseconds>(m_tm_recd - t));
	}

	const std::vector<std::uint8_t>& Message::SerialStream()
	{
		return m_rpt.SerialStream();
	}

	void Message::Parse(const std::uint8_t rxchar)
	{
        {
            std::unique_lock<std::mutex> lock(m_parseMutex);
            m_resp.Parse(rxchar);
        }
	}

	char Message::Parse()
	{
		char c = ' ';
		while (!unparsed_buf.empty())
		{
			c = unparsed_buf.front();
			unparsed_buf.pop_front();
			m_resp.Parse(c);
		}
		return c;
	}

	void Message::ResetParser()
	{
		m_resp.ResetParser();
	}

	DeviceReport Message::Response() const
	{
        std::unique_lock<std::mutex> lock(m_parseMutex);
		return m_resp;
	}

	const ReportFields& Message::RequestFields() const
	{
		return m_rpt.Fields();
	}

	void Message::AssignResponse(const DeviceReport& resp)
	{
        std::unique_lock<std::mutex> lock(m_parseMutex);
		m_resp = resp;
	}

    // AddBuffer is not synchronised so it must be called from the same thread that does the parsing
	void Message::AddBuffer(const std::vector<std::uint8_t>& buf)
	{
		// Add the input buffer to the internal queue
		for (std::vector<std::uint8_t>::const_iterator it = buf.begin(); it != buf.end(); ++it)
		{
			unparsed_buf.push_back(*it);
		}
	}

	bool Message::HasData() const
	{
		return (!unparsed_buf.empty());
	}
}
