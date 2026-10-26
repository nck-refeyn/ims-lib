/*-----------------------------------------------------------------------------
/ Title      : Voltage Controlled Oscillator Functions Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/SignalPath/src/VCO.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2025-12-019
/ Last update: $Date: 2020-06-05 07:45:07 +0100 (Fri, 05 Jun 2020) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 443 $
/------------------------------------------------------------------------------
/ Description:
/------------------------------------------------------------------------------
/ Copyright (c) 2025 Isomet (UK) Ltd. All Rights Reserved.
/------------------------------------------------------------------------------
/ Revisions  :
/ Date        Version  Author  Description
/ 2025-12-19  1.0      dc      Created
/
/----------------------------------------------------------------------------*/

#include "VCO.h"
#include "IMSConstants.h"
#include "IConnectionManager.h"
#include "PrivateUtil.h"
#include "IMSTypeDefs_p.h"
#include "MessageRegistry.h"

#define _USE_MATH_DEFINES
#include "math.h"

namespace iMS
{
	class VCOEventTrigger :
		public IEventTrigger
	{
	public:
		VCOEventTrigger() { updateCount(VCOEvents::Count); }
		~VCOEventTrigger() {};
	};

	class VCO::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const VCO* const);
		~Impl();

        std::map<MEASURE, Percent> m_measurements;
		std::weak_ptr<IMSSystem> m_ims;

        MessageRegistry<MessageHandle, HostReport> m_msgRegistry;
        // Held while a request is sent and its handle recorded, and while a response event is
        // handled, so a response arriving before its handle is recorded is not ignored
        std::recursive_mutex m_TrackMutex;
        class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(VCO::Impl* sf) : m_parent(sf) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			VCO::Impl* m_parent;
		};
		ResponseReceiver* Receiver;
        VCOEventTrigger m_Event;
	private:
		const VCO * const m_parent;
    };

	VCO::Impl::Impl(std::shared_ptr<IMSSystem> ims, const VCO* const vco) :
		m_ims(ims), Receiver(new ResponseReceiver(this)), m_parent(vco)
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
    }

	VCO::Impl::~Impl()
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

	VCO::VCO(std::shared_ptr<IMSSystem> ims) : p_Impl(new Impl(ims, this))
	{
	}

	VCO::~VCO()
	{
		delete p_Impl;
		p_Impl = nullptr;
	}

	void VCO::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
		std::lock_guard<std::recursive_mutex> track(m_parent->m_TrackMutex);
		switch (message)
		{
		case (MessageEvents::RESPONSE_RECEIVED) :
		case (MessageEvents::RESPONSE_ERROR_VALID) : {

			// Check for response and send to user code
			{
				if (m_parent->m_msgRegistry.size() > 0) {
                    auto m = m_parent->m_msgRegistry.findMessage(param);
                    if (m) {
                        with_locked(m_parent->m_ims, [&](std::shared_ptr<IMSSystem> ims) { 
                            auto conn = ims->Connection();
                            const DeviceReport& Resp = conn->Response(param);
                            auto v = Resp.Payload<std::vector<std::uint16_t>>();

                            m_parent->m_measurements[VCO::MEASURE::ANLG_INPUT_A_VOLTS] = Percent(100.0 * (double)v[0] / 65535.0);
                            m_parent->m_measurements[VCO::MEASURE::ANLG_INPUT_B_VOLTS] = Percent(100.0 * (double)v[1] / 65535.0);
                            m_parent->m_measurements[VCO::MEASURE::ANLG_INPUT_A_PROCESSED] = Percent(100.0 * (double)v[2] / 65535.0);
                            m_parent->m_measurements[VCO::MEASURE::ANLG_INPUT_B_PROCESSED] = Percent(100.0 * (double)v[3] / 65535.0);

                        });
                        m_parent->m_Event.Trigger<int>((void*)m_parent, VCOEvents::VCO_UPDATE_AVAILABLE, 0);
                        m_parent->m_Event.Trigger<double>((void*)m_parent, VCOEvents::VCO_UPDATE_AVAILABLE, 0.0);
                        m_parent->m_msgRegistry.removeMessage(param);
                    }
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
				if (m_parent->m_msgRegistry.size() > 0) {
                    auto m = m_parent->m_msgRegistry.findMessage(param);
                    if (m) {
						m_parent->m_Event.Trigger<int>(this, VCOEvents::VCO_READ_FAILED, param);
                        m_parent->m_msgRegistry.removeMessage(param);
					}
				}
			}
			break;
		}
		}
    }

	bool VCO::ConfigureCICFilter(bool enable, unsigned int filterLength)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            // Configure CIC Length
            filterLength = filterLength > 10 ? 10 : filterLength < 0 ? 0 : filterLength;
            auto iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_CICLength);

            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(filterLength)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            // Configure CIC Bypass
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_CICBypass);

            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(enable ? 0 : 1)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}

	bool VCO::ConfigureIIRFilter(bool enable, double freqCutoff, unsigned int cascadeStages)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            constexpr double Fs = 4062.5; // ADC sampling frequency in kHz
            const double omega = 2.0 * M_PI * freqCutoff / Fs;
            const double cos_omega = std::cos(omega);

            //double alpha_app = 1.0 - std::exp(-omega);  // approx formula
            const double alpha_cor =
                cos_omega - 1 +
                std::sqrt(cos_omega * cos_omega - 4 * cos_omega + 3); // exact formula 

            bool prescale_en = false;
            double alpha_scaled = alpha_cor * (1 << 16);

            if (alpha_scaled <= 255) {
                alpha_scaled *= 256.0;
                prescale_en = true;
            }

            auto alpha_int = static_cast<std::uint32_t>(alpha_scaled);
            alpha_int = std::clamp(alpha_int, 1u, 0xFFFFu);

            // Configure IIR Cutoff Frequency
            auto iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IIRAlpha);

            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(alpha_int)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IIRAlphaPre);

            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(prescale_en?1:0)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            // Configure CIC Bypass
            cascadeStages = (!enable)
                ? 0
                : std::clamp(cascadeStages, 0u, 8u);

            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IIRCount);

            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(cascadeStages)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}
    
    bool VCO::SetFrequencyRange(MHz& lowerFreq, MHz& upperFreq, RFChannel ch)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            auto lf = ims->Synth().GetCap().lowerFrequency;
            auto uf = ims->Synth().GetCap().upperFrequency;
            double range = uf - lf;

            if ((lowerFreq < lf) || (upperFreq > uf)) return false;

            double scaling = (upperFreq - lowerFreq)/range;
            double shift = (lowerFreq - lf)/range;

            HostReport* iorpt;
            if ((ch == 1) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1FScale);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(scaling * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1FShift);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(shift * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            } 
            if ((ch == 2) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2FScale);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(scaling * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2FShift);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(shift * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }
            return true;
        }).value_or(false);
	}
 
    bool VCO::SetAmplitudeRange(Percent& lowerAmpl, Percent& upperAmpl, RFChannel ch)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            constexpr double range = 100.0;

            if ((lowerAmpl < 0.0) || (upperAmpl > 100.0)) return false;

            double scaling = (upperAmpl - lowerAmpl)/range;
            double shift = lowerAmpl/range;

            HostReport* iorpt;
            if ((ch == 1) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1AScale);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(scaling * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1AShift);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(shift * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            } 
            if ((ch == 2) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2AScale);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(scaling * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2AShift);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(shift * 0xFFFF)); 
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }
            return true;
        }).value_or(false);
	}
 
    bool VCO::ApplyDigitalGain(VCOGain gain)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            HostReport* iorpt;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_DigGain);
            iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(gain)); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);
	}

    bool VCO::Route(VCOOutput output, VCOInput input)
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            HostReport* iorpt;
            std::uint16_t mask = 0; 
            int shift = 0;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IO_Config_Mask);
            switch(output)
            {
                case VCOOutput::CH1_FREQUENCY: mask = 0x1; shift = 0; break;
                case VCOOutput::CH1_AMPLITUDE: mask = 0x4; shift = 2; break;
                case VCOOutput::CH2_FREQUENCY: mask = 0x10; shift = 4; break;
                case VCOOutput::CH2_AMPLITUDE: mask = 0x40; shift = 6; break;
                default: return false;
            }
            iorpt->Payload<std::uint16_t>(mask); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            std::uint16_t data;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_VCOMux);
            switch(input)
            {
                case VCOInput::A: data = 0; break;
                case VCOInput::B: data = (1 << shift); break;
                default: return false;
            }
            iorpt->Payload<std::uint16_t>(data); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);
    }

    bool VCO::TrackingMode(VCOOutput output, VCOTracking func)
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            // Set Constant bit
            HostReport* iorpt;
            std::uint16_t mask = 0; 
            int shift = 0;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IO_Config_Mask);
            switch(output)
            {
                case VCOOutput::CH1_FREQUENCY: mask = 0x2; shift = 0; break;
                case VCOOutput::CH1_AMPLITUDE: mask = 0x8; shift = 2; break;
                case VCOOutput::CH2_FREQUENCY: mask = 0x20; shift = 4; break;
                case VCOOutput::CH2_AMPLITUDE: mask = 0x80; shift = 6; break;
                default: return false;
            }
            iorpt->Payload<std::uint16_t>(mask); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            std::uint16_t data;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_VCOMux);
            switch(func)
            {
                case VCOTracking::CONSTANT: data = (2 << shift); break;
                default: data = 0;
            }
            iorpt->Payload<std::uint16_t>(data); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            // Set tracking bits
            if (func == VCOTracking::CONSTANT) return true;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IO_Config_Mask);
            switch(output)
            {
                case VCOOutput::CH1_FREQUENCY: mask = 0x03; break;
                case VCOOutput::CH1_AMPLITUDE: mask = 0x0C; break;
                case VCOOutput::CH2_FREQUENCY: mask = 0x30; break;
                case VCOOutput::CH2_AMPLITUDE: mask = 0xC0; break;
                default: return false;
            }
            iorpt->Payload<std::uint16_t>(mask); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_VCOTrack);
            switch(func)
            {
                case VCOTracking::TRACK: data = (1 << shift); break;
                case VCOTracking::HOLD: data = 0; break;
                case VCOTracking::PIN_CONTROLLED: data = (2 << shift); break;
                default: return false;
            }
            iorpt->Payload<std::uint16_t>(data); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);        
    }

    bool VCO::RFMute(VCOMute mute, RFChannel ch)
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();
            
            // Set Constant bit
            HostReport* iorpt;
            std::uint16_t mask = 0; 

            // Set tracking and mute bits
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_IO_Config_Mask);
            if (ch.IsAll()) {
                mask = 0xF00;
            }
            else if (ch == 1) {
                mask = 0x300;
            }
            else if (ch == 2) {
                mask = 0xC00;
            }
            else {return false;}
            iorpt->Payload<std::uint16_t>(mask); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;

            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_VCOTrack);
            std::uint16_t data;
            if (mute == VCOMute::MUTE) {
                data = 0x500;
            } else if (mute == VCOMute::PIN_CONTROLLED) {
                data = 0xA00;
            } else {
                data = 0;
            }
            iorpt->Payload<std::uint16_t>(data); 
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);  
    }

    bool VCO::SetConstantFrequency(MHz freq, RFChannel ch)
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            unsigned int freq_word = FrequencyRenderer::RenderAsImagePoint(ims, freq);

            std::vector<std::uint16_t> freq_data;
    		freq_data.push_back(static_cast<std::uint16_t>(freq_word & 0xFFFF));
		    freq_data.push_back(static_cast<std::uint16_t>((freq_word >> 16) & 0xFFFF));

            HostReport* iorpt;
            if ((ch == 1) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1FConstHi);
                iorpt->Payload<std::vector<std::uint16_t>>(freq_data);

				ReportFields f = iorpt->Fields();
				f.len = static_cast<std::uint16_t>(freq_data.size() * sizeof(std::uint16_t));
				iorpt->Fields(f);

				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					return false;
				}
				delete iorpt;

                this->TrackingMode(VCOOutput::CH1_FREQUENCY, VCOTracking::CONSTANT);
            }            
            if ((ch == 2) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2FConstHi);
                iorpt->Payload<std::vector<std::uint16_t>>(freq_data);

				ReportFields f = iorpt->Fields();
				f.len = static_cast<std::uint16_t>(freq_data.size() * sizeof(std::uint16_t));
				iorpt->Fields(f);

				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					return false;
				}
				delete iorpt;

                this->TrackingMode(VCOOutput::CH2_FREQUENCY, VCOTracking::CONSTANT);
            }
            return true;         
        }).value_or(false);                
    }

    bool VCO::SetConstantAmplitude(Percent ampl, RFChannel ch)
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            unsigned int ampl_word = AmplitudeRenderer::RenderAsImagePoint(ims, ampl);

            HostReport* iorpt;
            if ((ch == 1) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch1AConst);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(ampl_word));

				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					return false;
				}
				delete iorpt;

                this->TrackingMode(VCOOutput::CH1_AMPLITUDE, VCOTracking::CONSTANT);
            }            
            if ((ch == 2) || ch.IsAll()) {
                iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Ch2AConst);
                iorpt->Payload<std::uint16_t>(static_cast<std::uint16_t>(ampl_word));

				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					return false;
				}
				delete iorpt;

                this->TrackingMode(VCOOutput::CH2_AMPLITUDE, VCOTracking::CONSTANT);
            }
            return true;
        }).value_or(false);   
    }

    bool VCO::SaveStartupState()
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            HostReport* iorpt;
            iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_VCOSaveStartup);
            iorpt->Payload<std::uint16_t>(1);

            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);         
    }

    bool VCO::ReadVoltageInput()
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {   
             // Make sure Synthesiser is present
            if (!ims->Synth().IsValid()) return false;
            auto conn = ims->Connection();

            std::shared_ptr<HostReport> iorpt;
            iorpt = std::make_shared<HostReport>(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, SYNTH_REG_VCOMonCh1);

            ReportFields f = iorpt->Fields();
            f.len = static_cast<std::uint16_t>((int)MEASURE::Count * sizeof(std::uint16_t));
            iorpt->Fields(f);

            std::lock_guard<std::recursive_mutex> track(p_Impl->m_TrackMutex);
            auto h = conn->SendMsg(*iorpt);
            if (NullMessage == h)
            {
                return false;
            }
            p_Impl->m_msgRegistry.addMessage(h, iorpt);
            return true;

        }).value_or(false);           
    }

    const std::map<VCO::MEASURE, Percent>& VCO::GetVoltageInputData() const
    {
        return p_Impl->m_measurements;
    }

    std::map<std::string, Percent> VCO::GetVoltageInputDataStr() const
    {
        std::map<std::string, Percent> out;
        for (auto& [k, v] : GetVoltageInputData())
        {
            switch(k)
            {
                case MEASURE::ANLG_INPUT_A_VOLTS: out["Voltage Input Ch A"] = v; break;
                case MEASURE::ANLG_INPUT_B_VOLTS: out["Voltage Input Ch B"] = v; break;
                case MEASURE::ANLG_INPUT_A_PROCESSED: out["Processed Value Ch A"] = v; break;
                case MEASURE::ANLG_INPUT_B_PROCESSED: out["Processed Value Ch B"] = v; break;
            }
        }
        return out;
    }

	void VCO::VCOEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event.Subscribe(message, handler);
	}

	void VCO::VCOEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event.Unsubscribe(message, handler);
	}

}