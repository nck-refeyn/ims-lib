/*-----------------------------------------------------------------------------
/ Title      : Host Reports (Host to iMS) Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Messaging/src/HostReport.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2020-11-20 16:06:38 +0000 (Fri, 20 Nov 2020) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 475 $
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

#include "HostReport.h"
#include "ReportManipulation.h"

#include <vector>

namespace iMS
{

	class HostReport::Impl
	{
	public:
		static const std::uint8_t ActionsMask = 0x0f;
		ReportSerializer serializer;
		bool isSerialized{ false };
	};

	HostReport::HostReport() : p_Impl(new Impl())  {}
	HostReport::HostReport(const ReportFields f) : IOReport(f), p_Impl(new Impl()) {}
	template <typename T>
	HostReport::HostReport(const ReportFields f, const T& t) : IOReport(f, t), p_Impl(new Impl()) {}
	HostReport::~HostReport() { delete p_Impl; p_Impl = nullptr; }
	
	// Copy Constructor
	HostReport::HostReport(const HostReport &rhs) : p_Impl(new Impl())
	{
		m_fields = rhs.m_fields;
		m_payload = rhs.m_payload;
		p_Impl->serializer = rhs.p_Impl->serializer;
	}

	// Assignment Constructor
	HostReport& HostReport::operator =(const HostReport &rhs)
	{
		if (this == &rhs) return *this;
		m_fields = rhs.m_fields;
		m_payload = rhs.m_payload;
		p_Impl->serializer = rhs.p_Impl->serializer;
		return *this;
	}

	const std::vector<std::uint8_t>& HostReport::SerialStream()
	{
		if (!p_Impl->isSerialized) {
			p_Impl->serializer.Serialize(this);
			p_Impl->isSerialized = true;
		}
		return p_Impl->serializer.Stream();
	}

	HostReport::HostReport(const HostReport::Actions actions, const HostReport::Dir dir, const std::uint16_t addr) : IOReport(), p_Impl(new Impl())
	{
		// Create header byte field from type and direction enums
		m_fields.hdr = (static_cast<std::uint8_t>(actions) & HostReport::Impl::ActionsMask);
		m_fields.hdr |= static_cast<std::uint8_t>(dir);

		// Assign Report ID type
		m_fields.ID = ((static_cast<std::uint8_t>(actions) & 0x10) ? ReportTypes::HOST_REPORT_ID_CTRLR : ReportTypes::HOST_REPORT_ID_SYNTH);
		m_fields.context = ((static_cast<std::uint8_t>(actions) & 0x20) ? 0x1 : 0);
		m_fields.addr = addr;

		switch (actions)
		{
		case HostReport::Actions::AOD_EEPROM:
		case HostReport::Actions::RFA_EEPROM:
		case HostReport::Actions::SYNTH_EEPROM: 
		case HostReport::Actions::CTRLR_SETTINGS: break;
		case HostReport::Actions::AOD_TEMP:
		case HostReport::Actions::RFA_TEMP:  {
			m_fields.len = 2;
			break;
		}
		case HostReport::Actions::ASYNC_DAC: {
			if (dir == HostReport::Dir::WRITE) {
				m_fields.len = 2;
			}
			else {
				// shouldn't read from DAC!
			}
		}
		case HostReport::Actions::PLL_REF:
		case HostReport::Actions::ASYNC_CONTROL:
		case HostReport::Actions::SYNTH_REG:
		case HostReport::Actions::FAN_CONTROL:
		case HostReport::Actions::CTRLR_REG: {
			m_fields.len = 2;
			break;
		}
		case HostReport::Actions::CTRLR_IMGDMA: {
			m_fields.len = 8;
			break;
		}
		case HostReport::Actions::CTRLR_IMAGE:
		case HostReport::Actions::LUT_ENTRY: {
			m_fields.len = 64;  // Suggest no larger than this
			break;
		}
		case HostReport::Actions::EXT_ADC: 
		case HostReport::Actions::RFA_ADC12: 
		case HostReport::Actions::RFA_ADC34: {
			if (dir == HostReport::Dir::WRITE) {
				// Writes to the ADC are sent to the 16-bit configuration register
				m_fields.len = 2;
			}
			else {
				// Each ADC has 8 channels of 2 bytes each which are read back in order
				m_fields.len = 16;
			}
			break;
		}
		case HostReport::Actions::RF_POWER: {
			m_fields.len = 1;
			break;
		}
		case HostReport::Actions::FW_UPGRADE: 
		case HostReport::Actions::CTRLR_FW_UPGRADE: {
			if (dir == HostReport::Dir::WRITE) {
				switch (addr) {
				case 0:
					m_fields.len = 1;
					break;
				case 1:
					// Write data buffer - must be updated by user code
					m_fields.len = 0;
					break;
				default: m_fields.len = 0;
				}
			}
			else {
				switch (addr) {
				case 0:
				case 2:
					m_fields.len = 4;
					break;
				case 1:
					m_fields.len = 2;
					break;
				default: m_fields.len = 0;
				}
			}
			break;
		}
		case HostReport::Actions::WAVE_SHAPING: {
			m_fields.len = 2;
			break;
		}
		default: break;
		}
	}

	HostReport::HostReport(const HostReport::Actions actions, const HostReport::Dir dir) :
		HostReport(actions, dir, 0) {};
}
