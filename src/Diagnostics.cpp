/*-----------------------------------------------------------------------------
/ Title      : Diagnostics Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Diagnostics/src/Diagnostics.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2024-12-18 16:57:01 +0000 (Wed, 18 Dec 2024) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 647 $
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

#include "Diagnostics.h"
#include "IEventTrigger.h"
#include "IConnectionManager.h"
#include "PrivateUtil.h"

#include <mutex>
#include <thread>
#include <condition_variable>

namespace iMS
{

	const std::uint16_t SYNTH_REG_syn_sram_base = 208;
	const std::uint16_t SYNTH_REG_aod_sram_base = 224;
	const std::uint16_t SYNTH_REG_rfa_sram_base = 240;

	const int ACR_rfaadc_convbusy_bit = 15;

	// RF ADC Channel Numbers
	const int RFADC_pfwd_ch1 = 0;
	const int RFADC_pfwd_ch2 = 1;
	const int RFADC_pfwd_ch3 = 8;
	const int RFADC_pfwd_ch4 = 9;
	const int RFADC_prefl_ch1 = 2;
	const int RFADC_prefl_ch2 = 3;
	const int RFADC_prefl_ch3 = 10;
	const int RFADC_prefl_ch4 = 11;
	const int RFADC_idc_ch1 = 4;
	const int RFADC_idc_ch2 = 5;
	const int RFADC_idc_ch3 = 12;
	const int RFADC_idc_ch4 = 13;

	class DiagnosticsEventTrigger :
		public IEventTrigger
	{
	public:
		DiagnosticsEventTrigger() { updateCount(DiagnosticsEvents::Count); }
		~DiagnosticsEventTrigger() {};
	};

	class Diagnostics::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const Diagnostics* const);
		~Impl();

		std::list<MessageHandle> m_DiagReadList;
		mutable std::mutex m_DiagList_mutex;

		std::weak_ptr<IMSSystem> m_ims;
		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(Diagnostics::Impl* sf) : m_parent(sf) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			Diagnostics::Impl* m_parent;
		};
		ResponseReceiver* Receiver;

		DiagnosticsEventTrigger m_Event;

		bool ADCActive;
		void ADCWorker();
		std::thread ADCThread;
		mutable std::mutex m_adcmutex;
		std::condition_variable m_adccond;

		std::map<MEASURE, Percent> m_measurements;
		std::map<MEASURE, Percent> m_measurements_copy;
	private:
		const Diagnostics * const m_parent;
	};

	Diagnostics::Impl::Impl(std::shared_ptr<IMSSystem> ims, const Diagnostics* diag) : m_ims(ims),
			Receiver(new ResponseReceiver(this)), m_parent(diag)
	{
		// Subscribe listener
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);

		// Configure ADCs
		HostReport *iorpt;
		iorpt = new HostReport(HostReport::Actions::RFA_ADC12, HostReport::Dir::WRITE, 0);
		iorpt->Payload<std::uint16_t>(0xBDA);
		conn->SendMsg(*iorpt);
		delete iorpt;

		iorpt = new HostReport(HostReport::Actions::RFA_ADC34, HostReport::Dir::WRITE, 0);
		iorpt->Payload<std::uint16_t>(0xBDA);
		conn->SendMsg(*iorpt);
		delete iorpt;
	}

	Diagnostics::Impl::~Impl()
	{
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

	void Diagnostics::Impl::ADCWorker()
	{
		while (ADCActive)
		{
			std::unique_lock<std::mutex> lck{ m_adcmutex };
			m_adccond.wait(lck);
			if (!ADCActive) {
				lck.unlock();
				break;
			}

            auto ims = m_ims.lock();
            if (!ims) {
                lck.unlock();
				break;
            }
            auto conn = ims->Connection();

			HostReport *iorpt;
			DeviceReport Resp;

			std::uint16_t busy;
			do {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
				iorpt = new HostReport(HostReport::Actions::ASYNC_CONTROL, HostReport::Dir::READ, 0xFFFF);
				Resp = conn->SendMsgBlocking(*iorpt);
				if (!Resp.Done())
				{
					delete iorpt;
					m_Event.Trigger<int>((void *)m_parent, DiagnosticsEvents::DIAG_READ_FAILED, 0);
					break;
				}
				delete iorpt;
				busy = Resp.Payload<std::uint16_t>();
			} while ((busy & (1 << ACR_rfaadc_convbusy_bit)) != 0);
			if (!Resp.Done()) continue;

			iorpt = new HostReport(HostReport::Actions::RFA_ADC12, HostReport::Dir::READ, 0);
			ReportFields f = iorpt->Fields();
			f.len = 16;
			iorpt->Fields(f);
			Resp = conn->SendMsgBlocking(*iorpt);
			if (!Resp.Done())
			{
				delete iorpt;
				m_Event.Trigger<int>((void *)m_parent, DiagnosticsEvents::DIAG_READ_FAILED, 0);
				continue;
			}
			delete iorpt;
			std::vector<std::uint16_t> data12 = Resp.Payload<std::vector<std::uint16_t>>();

			iorpt = new HostReport(HostReport::Actions::RFA_ADC34, HostReport::Dir::READ, 0);
			f = iorpt->Fields();
			f.len = 16;
			iorpt->Fields(f);
			Resp = conn->SendMsgBlocking(*iorpt);
			if (!Resp.Done())
			{
				delete iorpt;
				m_Event.Trigger<int>((void *)m_parent, DiagnosticsEvents::DIAG_READ_FAILED, 0);
				continue;
			}
			delete iorpt;
			std::vector<std::uint16_t> data34 = Resp.Payload<std::vector<std::uint16_t>>();

			m_measurements.clear();

			int channel = 0;
			std::vector<int> chan_errors;
			for (std::vector<std::uint16_t>::const_iterator iter = data12.cbegin(); iter != data12.cend(); ++iter)
			{
				//int channel = ((*iter) & 0xF000) >> 12;  // upper nibble not compatible with decoding without modifying channel constants
				if ((*iter) & 0x8000)
				{
					// Top bit set if diagnostics ADC failed to ack
					chan_errors.push_back(channel);
				}
				double value = ((double)((*iter) & 0xFFF) * 100.0 / 4095.0);
				switch (channel++) {
					case RFADC_pfwd_ch1: m_measurements[MEASURE::FORWARD_POWER_CH1] = value; break;
					case RFADC_pfwd_ch2: m_measurements[MEASURE::FORWARD_POWER_CH2] = value; break;
					case RFADC_prefl_ch1: m_measurements[MEASURE::REFLECTED_POWER_CH1] = value; break;
					case RFADC_prefl_ch2: m_measurements[MEASURE::REFLECTED_POWER_CH2] = value; break;
					case RFADC_idc_ch1: m_measurements[MEASURE::DC_CURRENT_CH1] = value; break;
					case RFADC_idc_ch2: m_measurements[MEASURE::DC_CURRENT_CH2] = value; break;
				}
			}
			channel = 8;
			for (std::vector<std::uint16_t>::const_iterator iter = data34.cbegin(); iter != data34.cend(); ++iter)
			{
				//int channel = (((*iter) & 0xF000) >> 12) + 8;
				double value = ((double)((*iter) & 0xFFF) * 100.0 / 4095.0);
				if ((*iter) & 0x8000)
				{
					// Top bit set if diagnostics ADC failed to ack
					chan_errors.push_back(channel);
				}
				switch (channel++) {
				case RFADC_pfwd_ch3: m_measurements[MEASURE::FORWARD_POWER_CH3] = value; break;
				case RFADC_pfwd_ch4: m_measurements[MEASURE::FORWARD_POWER_CH4] = value; break;
				case RFADC_prefl_ch3: m_measurements[MEASURE::REFLECTED_POWER_CH3] = value; break;
				case RFADC_prefl_ch4: m_measurements[MEASURE::REFLECTED_POWER_CH4] = value; break;
				case RFADC_idc_ch3: m_measurements[MEASURE::DC_CURRENT_CH3] = value; break;
				case RFADC_idc_ch4: m_measurements[MEASURE::DC_CURRENT_CH4] = value; break;
				}
			}

			if (!chan_errors.empty()) {
				// Restore previous readings
				m_measurements = m_measurements_copy;
			}

			lck.unlock();

			if (!chan_errors.empty()) {
				for (auto& iter : chan_errors) {
					m_Event.Trigger<int>((void*)m_parent, DiagnosticsEvents::DIAG_CHANNEL_ERROR, iter);
				}
			}
			else {
				m_Event.Trigger<int>((void*)m_parent, DiagnosticsEvents::DIAGNOSTICS_UPDATE_AVAILABLE, 0);
				m_Event.Trigger<double>((void*)m_parent, DiagnosticsEvents::DIAGNOSTICS_UPDATE_AVAILABLE, 0.0);
			}
		}
	}

	Diagnostics::Diagnostics(std::shared_ptr<IMSSystem> iMS) : p_Impl(new Impl(iMS, this)) {
		// Start ADC listening thread
		p_Impl->ADCActive = true;
		p_Impl->ADCThread = std::thread(&Impl::ADCWorker, p_Impl);
	}

	Diagnostics::~Diagnostics() {
		// Stop thread
		p_Impl->ADCActive = false;
		p_Impl->m_adccond.notify_one();
		p_Impl->ADCThread.join();
		
		delete p_Impl;
		p_Impl = nullptr;
	}

	void Diagnostics::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
		switch (message)
		{
		case (MessageEvents::RESPONSE_RECEIVED) :
		case (MessageEvents::RESPONSE_ERROR_VALID) : {

			// Check for response and send to user code
			{
				{  // the list is checked under m_DiagList_mutex below
                    with_locked(m_parent->m_ims, [&](std::shared_ptr<IMSSystem> ims) {   
                        auto conn = ims->Connection();
                        std::unique_lock<std::mutex> lck{ m_parent->m_DiagList_mutex };
                        for (std::list<MessageHandle>::iterator it = m_parent->m_DiagReadList.begin(); it != m_parent->m_DiagReadList.end();)
                        {
                            if (param != (*it)) {
                                ++it;
                                continue;
                            }
                            
                            const DeviceReport& Resp = conn->Response(param);
                            switch (Resp.Fields().hdr & 0xF)
                            {
                                case (static_cast<int>(HostReport::Actions::AOD_TEMP)) : {
                                    std::uint16_t Temperature = Resp.Payload<std::uint16_t>();
                                    double d = static_cast<double>(Temperature);
                                    // Convert from 2's comp representation
                                    if (d > 32767.0) {
                                        d -= 65536.0;
                                    }
                                    d /= 256.0;
                                    m_parent->m_Event.Trigger<double>(this, DiagnosticsEvents::AOD_TEMP_UPDATE, d);
                                    break;
                                }
                                case (static_cast<int>(HostReport::Actions::RFA_TEMP)) : {
                                    std::uint16_t Temperature = Resp.Payload<std::uint16_t>();
                                    double d = static_cast<double>(Temperature);
                                    // Convert from 2's comp representation
                                    if (d > 32767.0) {
                                        d -= 65536.0;
                                    }
                                    d /= 256.0;
                                    m_parent->m_Event.Trigger<double>(this, DiagnosticsEvents::RFA_TEMP_UPDATE, d);
                                    break;
                                }
                                case (static_cast<int>(HostReport::Actions::SYNTH_REG)) : {
                                    std::vector<std::uint16_t> data = Resp.Payload< std::vector<std::uint16_t> >();
                                    double hours = (double)(((std::uint32_t)data[1] << 16) + data[0]) / 10.0;
                                    switch (Resp.Fields().addr)
                                    {
                                        case SYNTH_REG_syn_sram_base: m_parent->m_Event.Trigger<double>(this, DiagnosticsEvents::SYN_LOGGED_HOURS, hours); break;
                                        case SYNTH_REG_aod_sram_base: m_parent->m_Event.Trigger<double>(this, DiagnosticsEvents::AOD_LOGGED_HOURS, hours); break;
                                        case SYNTH_REG_rfa_sram_base: m_parent->m_Event.Trigger<double>(this, DiagnosticsEvents::RFA_LOGGED_HOURS, hours); break;
                                        default: break;
                                    }
                                    break;
                                }
                            }
                            it = m_parent->m_DiagReadList.erase(it);
                        }
                    });
				}
			}
			break;
		}
		case (MessageEvents::TIMED_OUT_ON_SEND) :
		case (MessageEvents::SEND_ERROR) :
		case (MessageEvents::RESPONSE_TIMED_OUT) :
		case (MessageEvents::RESPONSE_ERROR_CRC) :
		case (MessageEvents::RESPONSE_ERROR_INVALID) : {

			// Check for response and send to user code
			{
				{  // the list is checked under m_DiagList_mutex below
					std::unique_lock<std::mutex> lck{ m_parent->m_DiagList_mutex };
					for (std::list<MessageHandle>::iterator it = m_parent->m_DiagReadList.begin(); it != m_parent->m_DiagReadList.end();)
					{
						if (param != (*it)) {
							++it;
							continue;
						}
						m_parent->m_Event.Trigger<int>(this, DiagnosticsEvents::DIAG_READ_FAILED, param);
						it = m_parent->m_DiagReadList.erase(it);
					}
					lck.unlock();
				}
			}
			break;
		}
		}
	}

	bool Diagnostics::GetTemperature(const TARGET& tgt) const
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport *iorpt;

            if (tgt == TARGET::AO_DEVICE) {
                iorpt = new HostReport(HostReport::Actions::AOD_TEMP, HostReport::Dir::READ, 0);
            }
            else if (tgt == TARGET::RF_AMPLIFIER) {
                iorpt = new HostReport(HostReport::Actions::RFA_TEMP, HostReport::Dir::READ, 0);
            }
            else return false;

            // Record the handle under the list mutex before the response handler can look for it
            std::unique_lock<std::mutex> lck{ p_Impl->m_DiagList_mutex };
            MessageHandle h = conn->SendMsg(*iorpt);
            if (NullMessage == h)
            {
                lck.unlock();
                delete iorpt;
                return false;
            }
            else {
                p_Impl->m_DiagReadList.push_back(h);
                lck.unlock();
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}

	bool Diagnostics::GetLoggedHours(const TARGET& tgt) const
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport *iorpt;

            if (tgt == TARGET::AO_DEVICE) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, SYNTH_REG_aod_sram_base);
            }
            else if (tgt == TARGET::RF_AMPLIFIER) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, SYNTH_REG_rfa_sram_base);
            }
            else if (tgt == TARGET::SYNTH) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, SYNTH_REG_syn_sram_base);
            }
            else return false;
            ReportFields f = iorpt->Fields();
            f.len = 4;
            iorpt->Fields(f);

            // Record the handle under the list mutex before the response handler can look for it
            std::unique_lock<std::mutex> lck{ p_Impl->m_DiagList_mutex };
            MessageHandle h = conn->SendMsg(*iorpt);
            if (NullMessage == h)
            {
                lck.unlock();
                delete iorpt;
                return false;
            }
            else {
                p_Impl->m_DiagReadList.push_back(h);
                lck.unlock();
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}

	bool Diagnostics::UpdateDiagnostics()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport *iorpt;

            std::uint16_t data = (1 << ACR_rfaadc_convbusy_bit);

            MessageHandle h;
            std::unique_lock<std::mutex> lck{ p_Impl->m_adcmutex, std::try_to_lock };
            if (lck.owns_lock())
            {
                // Take a copy of the measurements to return before the update is available
                p_Impl->m_measurements_copy.clear();
                p_Impl->m_measurements_copy = p_Impl->m_measurements;

                iorpt = new HostReport(HostReport::Actions::ASYNC_CONTROL, HostReport::Dir::WRITE, data);
                iorpt->Payload<std::uint16_t>(data);
                h = conn->SendMsg(*iorpt);

                lck.unlock();
            }
            else {
                // Update already in progress
                return true;
            }

            if (NullMessage == h)
            {
                delete iorpt;
                return false;
            }
            else {
                p_Impl->m_adccond.notify_one();
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}

	const std::map<Diagnostics::MEASURE, Percent>& Diagnostics::GetDiagnosticsData() const
	{
		std::unique_lock<std::mutex> lck{ p_Impl->m_adcmutex, std::try_to_lock };
		if (lck.owns_lock()) {
			return p_Impl->m_measurements;
		}
		else {
			// Busy retrieving data but thankfully we took a copy of the old data.
			return p_Impl->m_measurements_copy;
		}
	}

    std::map<std::string, Percent> Diagnostics::GetDiagnosticsDataStr() const
    {
        std::map<std::string, Percent> out;
        for (auto& [k, v] : GetDiagnosticsData())
        {
            switch(k)
            {
                case MEASURE::FORWARD_POWER_CH1: out["Forward Power Ch 1"] = v; break;
                case MEASURE::FORWARD_POWER_CH2: out["Forward Power Ch 2"] = v; break;
                case MEASURE::FORWARD_POWER_CH3: out["Forward Power Ch 3"] = v; break;
                case MEASURE::FORWARD_POWER_CH4: out["Forward Power Ch 4"] = v; break;
                case MEASURE::REFLECTED_POWER_CH1: out["Reflected Power Ch 1"] = v; break;
                case MEASURE::REFLECTED_POWER_CH2: out["Reflected Power Ch 2"] = v; break;
                case MEASURE::REFLECTED_POWER_CH3: out["Reflected Power Ch 3"] = v; break;
                case MEASURE::REFLECTED_POWER_CH4: out["Reflected Power Ch 4"] = v; break;
                case MEASURE::DC_CURRENT_CH1: out["DC Current Ch 1"] = v; break;
                case MEASURE::DC_CURRENT_CH2: out["DC Current Ch 2"] = v; break;
                case MEASURE::DC_CURRENT_CH3: out["DC Current Ch 3"] = v; break;
                case MEASURE::DC_CURRENT_CH4: out["DC Current Ch 4"] = v; break;
            }
        }
        return out;
    }    

	void Diagnostics::DiagnosticsEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event.Subscribe(message, handler);
	}

	void Diagnostics::DiagnosticsEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event.Unsubscribe(message, handler);
	}


}
