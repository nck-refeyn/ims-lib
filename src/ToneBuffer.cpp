    /*-----------------------------------------------------------------------------
/ Title      : Isomet ToneBuffer Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ToneBuffer/src/ToneBuffer.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2016-02-24
/ Last update: $Date: 2024-12-18 16:58:21 +0000 (Wed, 18 Dec 2024) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 648 $
/------------------------------------------------------------------------------
/ Description:
/------------------------------------------------------------------------------
/ Copyright (c) 2016 Isomet (UK) Ltd. All Rights Reserved.
/------------------------------------------------------------------------------
/ Revisions  :
/ Date        Version  Author  Description
/ 2016-02-24  1.0      dc      Created
/
/----------------------------------------------------------------------------*/

#include "ToneBuffer.h"
#include "IEventTrigger.h"
#include "Message.h"
#include "IConnectionManager.h"
#include "Image.h"
#include "FileSystem.h"
#include "FileSystem_p.h"
#include "IMSTypeDefs_p.h"
#include "IMSConstants.h"
#include "PrivateUtil.h"

#include <mutex>
#include <condition_variable>
#include <thread>
#include <list>
#include <iostream>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace iMS {

	class ToneBufferEventTrigger :
		public IEventTrigger
	{
	public:
		ToneBufferEventTrigger() { updateCount(ToneBufferEvents::Count); }
		~ToneBufferEventTrigger() {};
	};


	class ToneBuffer::Impl
	{
	public:
		Impl(const std::string& name = "") : m_name(name), tag(boost::uuids::random_generator()()), tagDirty(false) {};
		Impl(const TBEntry& tbe, const std::string& name = "") : m_name(name), tag(boost::uuids::random_generator()()), tagDirty(false) { array.fill(tbe); };
		~Impl() {}

		// Update the UUID whenever the image has been modified
		void updateUUID() {
			//this->tag = boost::uuids::random_generator()();
			tagDirty = true;
		}
		void refreshTag() {
			if (tagDirty) {
				this->tag = boost::uuids::random_generator()();
				tagDirty = false;
			}
		}

		bool tagDirty;
		boost::uuids::uuid tag;

		TBArray array;
		std::string m_name;
	};

	ToneBuffer::ToneBuffer(const std::string& name) : p_Impl(new Impl(name)) {};
	// Fill Constructor
	ToneBuffer::ToneBuffer(const TBEntry& tbe, const std::string& name) : p_Impl(new Impl(tbe, name)) {};
	// Non-volatile Memory Constructor
	ToneBuffer::ToneBuffer(const int entry, const std::string& name) : p_Impl(new Impl(name)) {
		// Add filesystem read stuff here...
	};

	ToneBuffer::~ToneBuffer() { delete p_Impl; p_Impl = nullptr; };

	// Copy & Assignment Constructors
	ToneBuffer::ToneBuffer(const ToneBuffer &rhs) : p_Impl(new Impl())
	{
		p_Impl->tag = rhs.p_Impl->tag;
		p_Impl->array = rhs.p_Impl->array;
		p_Impl->m_name = rhs.p_Impl->m_name;
	};

	ToneBuffer& ToneBuffer::operator =(const ToneBuffer &rhs)
	{
		if (this == &rhs) return *this;
		p_Impl->tag = rhs.p_Impl->tag;
		p_Impl->array = rhs.p_Impl->array;
		p_Impl->m_name = rhs.p_Impl->m_name;
		return *this;
	};

	bool ToneBuffer::operator==(ToneBuffer const& rhs) const {
		return (p_Impl->array == rhs.p_Impl->array && p_Impl->m_name == rhs.p_Impl->m_name);
	}

	ToneBuffer::iterator ToneBuffer::begin() 	{ return p_Impl->array.begin(); }
	ToneBuffer::iterator ToneBuffer::end()   	{ return p_Impl->array.end(); }
	ToneBuffer::const_iterator ToneBuffer::begin() const 	{ return p_Impl->array.cbegin(); }
	ToneBuffer::const_iterator ToneBuffer::end() const  	{ return p_Impl->array.cend(); }
	ToneBuffer::const_iterator ToneBuffer::cbegin() const 	{ return begin(); }
	ToneBuffer::const_iterator ToneBuffer::cend() const  	{ return end(); }

	const std::array<std::uint8_t, 16> ToneBuffer::UUID() const {
		p_Impl->refreshTag();
		boost::uuids::uuid u = p_Impl->tag;
		std::array<std::uint8_t, 16> v;
		std::copy_n(u.begin(), 16, v.begin());
		return v;
	}

	// Random Access (read-only) to a TBEntry in the ToneBuffer
	const TBEntry& ToneBuffer::operator[](std::size_t idx) const
	{
		// out_of_range exception thrown if idx greater than ToneBuffer size
		return p_Impl->array.at(idx);
	}

	// Random Access to a TBEntry in the ToneBuffer
	TBEntry& ToneBuffer::operator[](std::size_t idx)
	{
		this->p_Impl->updateUUID();
		// out_of_range exception thrown if idx greater than ToneBuffer size
		return p_Impl->array.at(idx);
	}

	const std::size_t ToneBuffer::Size() const { return p_Impl->array.size(); };

	const std::string& ToneBuffer::Name() const
	{
		return p_Impl->m_name;
	}

	std::string& ToneBuffer::Name()
	{
		return p_Impl->m_name;
	}

	class ToneBufferDownload::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const ToneBuffer& tb);
		~Impl();

		std::weak_ptr<IMSSystem> m_ims;
		const ToneBuffer& m_tb;

		ToneBufferEventTrigger m_Event;

		void AddPointToVector(std::shared_ptr<IMSSystem> ims, std::vector<std::uint8_t>&, const TBEntry&, bool toEEPROM);

		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(ToneBufferDownload::Impl* dl) : m_parent(dl) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			ToneBufferDownload::Impl* m_parent;
		};
		ResponseReceiver* Receiver;

		ToneBuffer::const_iterator first;
		ToneBuffer::const_iterator last;
		int offset;

		std::list <MessageHandle> dl_list;
		MessageHandle dl_final;
		mutable std::mutex dl_list_mutex;
        std::condition_variable dl_list_cv;     // signals when dl_list has room        
        size_t dl_list_watermark = 16;          // tune this to the link capacity (messages)

        LazyWorker downloadWorker;
        LazyWorker rxWorker;

        bool downloadRequested{ false };
        void DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);

		std::deque<int> rxok_list;
		std::deque<int> rxerr_list;
	    void RxWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);

		FileSystemWriter* fsw;
	};

	ToneBufferDownload::Impl::Impl(std::shared_ptr<IMSSystem> ims, const ToneBuffer& tb) :
		m_ims(ims), m_tb(tb), Receiver(new ResponseReceiver(this)),
        downloadWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            DownloadWorkerLoop(running, cond, mtx);
        }),
        rxWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            RxWorkerLoop(running, cond, mtx);
        })
	{
		// Subscribe listeners
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);
	}

	ToneBufferDownload::Impl::~Impl() {
		// Join the workers before the lists and event trigger they use are destroyed
		downloadWorker.stop();
		rxWorker.stop();

		// Unsubscribe listener
        with_locked(m_ims, [this](std::shared_ptr<IMSSystem> ims) {         
            auto conn = ims->Connection();
            conn->MessageEventUnsubscribe(MessageEvents::SEND_ERROR, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
            conn->MessageEventUnsubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);
        });

		delete Receiver;
	}

	void ToneBufferDownload::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
		switch (message)
		{
		case (MessageEvents::RESPONSE_RECEIVED) :
		case (MessageEvents::RESPONSE_ERROR_VALID) : {

			// Add response to verify list for checking by rx processing thread
			{
				std::unique_lock<std::mutex> lck{ m_parent->rxWorker.mutex() };
				m_parent->rxok_list.push_back(param);
			}
            m_parent->rxWorker.notify();
			break;
		}
		case (MessageEvents::TIMED_OUT_ON_SEND) :
		case (MessageEvents::SEND_ERROR) :
		case (MessageEvents::RESPONSE_TIMED_OUT) :
		case (MessageEvents::RESPONSE_ERROR_CRC) :
		case (MessageEvents::RESPONSE_ERROR_INVALID) : {

			// Add error to list and trigger processing thread if handle exists
			{
				std::unique_lock<std::mutex> lck{ m_parent->rxWorker.mutex() };
				m_parent->rxerr_list.push_back(param);
			}
            m_parent->rxWorker.notify();
			break;
		}
		}
	}

	ToneBufferDownload::ToneBufferDownload(std::shared_ptr<IMSSystem> ims, const ToneBuffer& tb) : p_Impl(new Impl(ims, tb))	{}

	ToneBufferDownload::~ToneBufferDownload() { delete p_Impl; p_Impl = nullptr; }

	bool ToneBufferDownload::StartDownload()
	{
        BOOST_LOG_SEV(lg::get(), sev::trace) << "ToneBufferDownload::StartDownload()";
		return this->StartDownload(p_Impl->m_tb.cbegin(), p_Impl->m_tb.cend());
	}

	bool ToneBufferDownload::StartDownload(ToneBuffer::const_iterator single)
	{
        BOOST_LOG_SEV(lg::get(), sev::trace) << "ToneBufferDownload::StartDownload(ToneBuffer::const_iterator single)";
		return this->StartDownload(single, single+1);
	}

    bool ToneBufferDownload::StartDownload(std::size_t index)
    {
        BOOST_LOG_SEV(lg::get(), sev::trace) << "ToneBufferDownload::StartDownload(" << index << ")";
        const ToneBuffer::const_iterator& single = p_Impl->m_tb.cbegin() + index;
        return this->StartDownload(single, single+1);
    }

    bool ToneBufferDownload::StartDownload(std::size_t index, std::size_t count)
    {
        BOOST_LOG_SEV(lg::get(), sev::trace) << "ToneBufferDownload::StartDownload(" << index << ", " << count << ")";
        const ToneBuffer::const_iterator& single = p_Impl->m_tb.cbegin() + index;
        return this->StartDownload(single, single+count);
    }

	bool ToneBufferDownload::StartDownload(ToneBuffer::const_iterator first, ToneBuffer::const_iterator last)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            BOOST_LOG_SEV(lg::get(), sev::trace) << "ToneBufferDownload::StartDownload(ToneBuffer::const_iterator first, ToneBuffer::const_iterator last)";
            p_Impl->first = first;
            p_Impl->last = last;
            p_Impl->offset = static_cast<int>(std::distance(p_Impl->m_tb.cbegin(), first));

            // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;

            p_Impl->rxWorker.start();
            p_Impl->downloadWorker.start();

            int retries=10;
            while (retries)
            {
                std::unique_lock<std::mutex> lck{ p_Impl->downloadWorker.mutex(), std::try_to_lock };

                if (!lck.owns_lock()) {
                    if (!--retries) return false;
                    // Mutex lock failed, Downloader must be busy, try again later
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                std::unique_lock<std::mutex> rxlck{ p_Impl->rxWorker.mutex(), std::try_to_lock };

                if (!rxlck.owns_lock()) {
                    if (!--retries) return false;
                    // Mutex lock failed, Rx must be busy, try again later
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                p_Impl->dl_list.clear();
                p_Impl->downloadRequested = true;
                lck.unlock();

                break;
            }
            
            p_Impl->downloadWorker.notify();
            return true;   
        }).value_or(false); 
	}

	void ToneBufferDownload::ToneBufferDownloadEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event.Subscribe(message, handler);
	}

	void ToneBufferDownload::ToneBufferDownloadEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event.Unsubscribe(message, handler);
	}

	void ToneBufferDownload::Impl::AddPointToVector(std::shared_ptr<IMSSystem> ims, std::vector<std::uint8_t>& ltb_data, const TBEntry& tbe, bool toEEPROM)
	{
		int freqBits = ims->Synth().GetCap().freqBits;
		// Note that for-construct here enters infinite loop as can't increment past 4
		RFChannel chan(RFChannel::min);
		do {
			unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, tbe.GetFAP(chan).freq);
			if (!toEEPROM) {
				if ((chan == RFChannel::min) || (freqBits <= 16))
				{
					// The first two entries were previously the lower frequency bits but became unused.  Now using them for sync data (not to EEPROM)
					unsigned int syncd = tbe.GetSyncD();
					std::uint16_t syncd_mod = syncd & ((1 << ims->Synth().GetCap().LUTSyncDBits) - 1);
					ltb_data.push_back(static_cast<std::uint8_t>(syncd_mod & 0xFF));
					ltb_data.push_back(static_cast<std::uint8_t>((syncd_mod >> 8) & 0xFF));
				}
				else {
					ltb_data.push_back(static_cast<std::uint8_t>((freq >> (freqBits - 32)) & 0xFF));
					ltb_data.push_back(static_cast<std::uint8_t>((freq >> (freqBits - 24)) & 0xFF));
				}
			}
			ltb_data.push_back(static_cast<std::uint8_t>((freq >> (freqBits - 16)) & 0xFF));
			ltb_data.push_back(static_cast<std::uint8_t>((freq >> (freqBits - 8)) & 0xFF));
			std::uint16_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, tbe.GetFAP(chan).ampl);
			ltb_data.push_back(static_cast<std::uint8_t>(ampl & 0xFF));
			ltb_data.push_back(static_cast<std::uint8_t>((ampl >> 8) & 0xFF));
			std::uint16_t phase = PhaseRenderer::RenderAsImagePoint(ims, tbe.GetFAP(chan).phase);
			ltb_data.push_back(static_cast<std::uint8_t>(phase & 0xFF));
			ltb_data.push_back(static_cast<std::uint8_t>((phase >> 8) & 0xFF));
		} while (chan++ < RFChannel::max);
	}

	// ToneBuffer Downloading Thread
	void ToneBufferDownload::Impl::DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
	{
		while (true) {
			std::unique_lock<std::mutex> lck{ mtx };
			cond.wait(lck, [this, &running]() {
                return downloadRequested || !running;
            });

			// Allow thread to terminate 
			if (!running) break;
            downloadRequested = false;

            auto ims = m_ims.lock();
            if (!ims) break;
            auto conn = ims->Connection();

			// Download loop
			HostReport *iorpt;
			int index = offset;
			int length = static_cast<int>(std::distance(first, last));
			int buf_bytes = 1024;

			dl_final = NullMessage;

			std::vector<std::uint8_t> ltb_data;
			ToneBuffer::const_iterator it = first;
			MessageHandle h2;

			while (((index - offset) < length) && (it < last) && running)
			{
				// Add one TBEntry to vector: build packet data
				AddPointToVector(ims, ltb_data, (*it), false);

                if (buf_bytes <= 0)
                {
                    buf_bytes = 1024;
                }
                buf_bytes -= static_cast<int>(ltb_data.size());                

				iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_ProgSyncDig);
				iorpt->Payload<std::vector<std::uint8_t>>(ltb_data);
				MessageHandle h = conn->SendMsg(*iorpt);
				delete iorpt;

                // THROTTLE: wait until dl_list has room (atomically)
                {
                    std::unique_lock<std::mutex> dllck{ dl_list_mutex };
                    dl_list_cv.wait(dllck, [&]() {
                        return (!running) || (dl_list.size() < dl_list_watermark);
                    });
                    if (!running) {
                        // optionally clean up or break out to terminate quickly
                        dllck.unlock();
                        break;
                    }
                    dl_list.push_back(h);
                    // dllck.unlock(); // unlocks at scope exit
                }

				ltb_data.clear();

				if (ims->Synth().GetCap().freqBits > 16)
				{
					unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, it->GetFAP(RFChannel::min).freq);
					ltb_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 32)) & 0xFF));
					ltb_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 24)) & 0xFF));
					iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_ProgFreq0L);
					iorpt->Payload<std::vector<std::uint8_t>>(ltb_data);
					conn->SendMsg(*iorpt);
					delete iorpt;
				}

				ltb_data.clear();

				// Send message to program entry into an LTB index
				iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_ProgLocal);
				iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(index++));
				h2 = conn->SendMsg(*iorpt);
				delete iorpt;

				++it;

				// Add message handles to download list so we can check the responses
				{
				    std::unique_lock<std::mutex> dllck{ dl_list_mutex };
        			if ((it == last) || ((index - offset) >= length)) dl_final = h2;
        			dl_list.push_back(h2);
                }

				// Not a great hack: adding a gap ensures packets aren't coalesced (even with TCP_NODELAY enabled)
				//std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}

			// Release lock, wait for next download trigger
			lck.unlock();
		}
	}

	// ToneBuffer Readback Verify Data Processing Thread
	void ToneBufferDownload::Impl::RxWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
	{
		while (true) {
    		std::unique_lock<std::mutex> lck{ mtx };
            cond.wait(lck);

            // Allow thread to terminate 
			if (!running) break;
                        
			while (!rxok_list.empty())
			{
				int param = rxok_list.front();
				rxok_list.pop_front();
                
                {
                    std::unique_lock<std::mutex> dllck{ dl_list_mutex };

                    for (std::list<MessageHandle>::iterator iter = dl_list.begin();
                        iter != dl_list.end();)
                    {
                        MessageHandle handle = static_cast<MessageHandle>(*iter);
                        if (handle != (param))
                        {
                            ++iter;
                            continue;
                        }

                        // Remove from list
                        iter = dl_list.erase(iter);

                        // Download Finished?
                        if (handle == dl_final)
                        {
                            m_Event.Trigger<int>((void *)this, ToneBufferEvents::DOWNLOAD_FINISHED, 0);
                        }

                        // Notify producer that there is room for more messages
                        // (notify after each erase; could batch and notify once)
                        dl_list_cv.notify_one();
                    }
                }

			}

			while (!rxerr_list.empty())
			{
				int param = rxerr_list.front();
				rxerr_list.pop_front();

                {
                    std::unique_lock<std::mutex> dllck{ dl_list_mutex };

                    for (std::list<MessageHandle>::iterator iter = dl_list.begin();
                        iter != dl_list.end();)
                    {
                        MessageHandle handle = static_cast<MessageHandle>(*iter);
                        if (handle != (param))
                        {
                            ++iter;
                            continue;
                        }

                        // Download Finished?
                        if (handle == dl_final)
                        {
                            m_Event.Trigger<int>((void *)this, ToneBufferEvents::DOWNLOAD_FINISHED, 0);
                        }

                        // Remove from list
                        iter = dl_list.erase(iter);
                        m_Event.Trigger<int>((void *)this, ToneBufferEvents::DOWNLOAD_ERROR, handle);

                        // Notify producer it can continue
                        dl_list_cv.notify_one();                        
                    }
                }
			}

		}
	}

	const FileSystemIndex ToneBufferDownload::Store(const std::string& FileName, FileDefault def) const
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> FileSystemIndex
        {         
            FileSystemManager fsm(ims);
            std::uint32_t addr;

            std::vector<std::uint8_t> data;
            for (ToneBuffer::const_iterator it = p_Impl->m_tb.cbegin(); it != p_Impl->m_tb.cend(); ++it)
            {
                p_Impl->AddPointToVector(ims, data, (*it), true);
            }

            if (!fsm.FindSpace(addr, data)) return -1;
            FileSystemTableEntry fste(FileSystemTypes::TONE_BUFFER, addr, data.size(), def, FileName);
            p_Impl->fsw = new FileSystemWriter(ims, fste, data);

            FileSystemIndex result = p_Impl->fsw->Program();
            delete p_Impl->fsw;
            return result;
        }).value_or(false);          
	}

	class ToneSequenceEntry::Impl {
	public:
		Impl(SignalPath::ToneBufferControl tbc = SignalPath::ToneBufferControl::HOST, int initial_index = 0) : 
			index(initial_index), tbc(tbc) {}

		SignalPath::ToneBufferControl tbc;
		int index;
	};

	ToneSequenceEntry::ToneSequenceEntry() : p_Impl(new Impl()) {}
	ToneSequenceEntry::ToneSequenceEntry(const ToneBuffer& tb, SignalPath::ToneBufferControl tbc, const unsigned int initial_index) :
		p_Impl(new Impl(tbc, initial_index)), SequenceEntry(tb.UUID()) {}
	ToneSequenceEntry::ToneSequenceEntry(const SequenceEntry& entry) : SequenceEntry(entry), p_Impl(new Impl()) 
	{
		try {
			const ToneSequenceEntry& rhs = dynamic_cast<const ToneSequenceEntry&>(entry);

			p_Impl->tbc = rhs.p_Impl->tbc;
			p_Impl->index = rhs.p_Impl->index;
		}
		catch (std::bad_cast ex) {
			return;
		}
	}

	ToneSequenceEntry::ToneSequenceEntry(const ToneSequenceEntry& rhs) : p_Impl(new Impl()), SequenceEntry(rhs.UUID(), rhs.NumRpts())
	{
		p_Impl->index = rhs.p_Impl->index;
		p_Impl->tbc = rhs.p_Impl->tbc;
		this->SyncOutDelay() = rhs.SyncOutDelay();
		for (RFChannel ch = RFChannel::min; ; ch++) {
			this->SetFrequencyOffset(rhs.GetFrequencyOffset(ch), ch);
			if (ch == RFChannel::max) break;
		}
	}

	ToneSequenceEntry& ToneSequenceEntry::operator =(const ToneSequenceEntry& rhs)
	{
		if (this == &rhs) return *this;
		p_Impl->index = rhs.p_Impl->index;
		p_Impl->tbc = rhs.p_Impl->tbc;
		return *this;
	}

	ToneSequenceEntry::~ToneSequenceEntry() {
		delete p_Impl;
		p_Impl = nullptr;
	}

	bool ToneSequenceEntry::operator==(SequenceEntry const& rhs) const {
		try {
			const ToneSequenceEntry& tse = dynamic_cast<const ToneSequenceEntry&>(rhs);

			for (RFChannel ch = RFChannel::min; ; ch++) {
				if (SequenceEntry::GetFrequencyOffset(ch) != rhs.GetFrequencyOffset(ch))
					return false;
				if (ch == RFChannel::max) break;
			}
			return ((p_Impl->index == tse.p_Impl->index) &&
				(p_Impl->tbc == tse.p_Impl->tbc) &&
				(SequenceEntry::SyncOutDelay() == rhs.SyncOutDelay()) &&
				(SequenceEntry::UUID() == rhs.UUID()));
		}
		catch (std::bad_cast ex) {
			return false;
		}
	}

	SignalPath::ToneBufferControl ToneSequenceEntry::ControlSource() const {
		return p_Impl->tbc;
	}

	int ToneSequenceEntry::InitialIndex() const {
		return p_Impl->index;
	}

}
