/*-----------------------------------------------------------------------------
/ Title      : iMS System Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ConnectionManager/src/IMSSystem.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2023-11-24 08:01:48 +0000 (Fri, 24 Nov 2023) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 589 $
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

#include "IMSSystem.h"
#include "IConnectionManager.h"
#include "FileSystem_p.h"
#include "Image_p.h"
#include "readonlymemvfs.h"
#include "CS_ETH.h"
#include "CS_RS422.h"
#include "PrivateUtil.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>

#if defined _WIN32 && defined _DEBUG
#include "crtdbg.h"
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace iMS
{
	class IMSOption::Impl
	{
	public:
		Impl() {}
		Impl(std::string& name) : name(name) {}
		~Impl() {}

		std::string name;
	};

	IMSOption::IMSOption() : p_Impl(new Impl())
	{
	}

	IMSOption::IMSOption(std::string &name) : p_Impl(new Impl(name))
	{
	}

	IMSOption::~IMSOption()
	{
		delete p_Impl;
		p_Impl = nullptr;
	}

	IMSOption::IMSOption(const IMSOption &rhs) : p_Impl(new Impl(rhs.p_Impl->name))
	{
	}

	IMSOption &IMSOption::operator =(const IMSOption &rhs)
	{
		if (this == &rhs) return *this;
		p_Impl->name = rhs.p_Impl->name;
		return *this;
	}

	const std::string& IMSOption::Name() const
	{
		return p_Impl->name;
	}

	class IMSSystem::Impl
	{
	public:
		Impl(IMSSystem* parent) : Impl(parent, std::string(), nullptr) {}
		Impl(IMSSystem* parent, std::string connString, std::shared_ptr<IConnectionManager> conn) :
			m_parent(parent), m_connString(connString), m_conn(conn) {
#if defined (_WIN32)
			CS_RS422 cs_rs422;
			m_settings.emplace(cs_rs422.Ident(), DeviceInterfaceSettings(true, 16, 16));
#endif
#if defined (_WIN32) || defined (__QNXNTO__) || defined(__linux__)
			CS_ETH cs_eth;
			m_settings.emplace(cs_eth.Ident(), DeviceInterfaceSettings(true, 32, 13));
#endif
//			BOOST_LOG_SEV(lg::get(), sev::trace) << "IMSSystem Constructor" << std::endl;;

		}
		~Impl() {
//			BOOST_LOG_SEV(lg::get(), sev::trace) << "IMSSystem Destructor" << std::endl;
		};

		IMSSystem * m_parent;
		IMSController m_Ctlr;
		IMSSynthesiser m_Synth;
		std::string m_connString;
		std::shared_ptr<IConnectionManager> m_conn;

		bool AddDevice(std::uint16_t magicID, std::string type = "");

		class DeviceInterfaceSettings
		{
		public:
			DeviceInterfaceSettings();
			DeviceInterfaceSettings(bool settings, unsigned int start_addr, unsigned int length);

			// True if configuring device side settings is possible
			bool HasSettings() const;

			// Byte Address in non-volatile memory at which device side settings data starts
			unsigned int StartAddr() const;

			// Number of bytes occupied by device side settings
			unsigned int Length() const;
		private:
			bool m_settings;
			unsigned int m_start_addr;
			unsigned int m_length;
		};
		typedef std::map<std::string, DeviceInterfaceSettings> DeviceSettingsMap;

		// Return a struct of data representing the configuration of client side storage of connection settings
		const DeviceInterfaceSettings& Settings(const std::string& intf) const;
	private:
		DeviceSettingsMap m_settings;
	};

	IMSSystem::IMSSystem() : IMSSystem(nullptr, std::string()) {}

	IMSSystem::IMSSystem(const std::shared_ptr<IConnectionManager> conn, const std::string& str) : p_Impl(new Impl(this, str, conn))	{
	}

	IMSSystem::~IMSSystem()	{ delete p_Impl; p_Impl = nullptr; }

	// Copy & Assignment Constructors
	IMSSystem::IMSSystem(const IMSSystem &rhs) : p_Impl(new Impl(this))
	{
		//p_Impl->m_parent = rhs.p_Impl->m_parent;
		p_Impl->m_Ctlr = rhs.p_Impl->m_Ctlr;
		p_Impl->m_Synth = rhs.p_Impl->m_Synth;
		p_Impl->m_conn = rhs.p_Impl->m_conn;
		p_Impl->m_connString = rhs.p_Impl->m_connString;
	};

	IMSSystem& IMSSystem::operator =(const IMSSystem &rhs)
	{
		if (this == &rhs) return *this;
		//p_Impl->m_parent = rhs.p_Impl->m_parent;
		p_Impl->m_Ctlr = rhs.p_Impl->m_Ctlr;
		p_Impl->m_Synth = rhs.p_Impl->m_Synth;
		p_Impl->m_conn = rhs.p_Impl->m_conn;
		p_Impl->m_connString = rhs.p_Impl->m_connString;
		return *this;
	};

	IMSSystem::Impl::DeviceInterfaceSettings::DeviceInterfaceSettings() :
		m_settings(false), m_start_addr(0), m_length(0) {}

	IMSSystem::Impl::DeviceInterfaceSettings::DeviceInterfaceSettings(bool settings, unsigned int start_addr, unsigned int length) :
		m_settings(settings), m_start_addr(start_addr), m_length(length) {}

	bool IMSSystem::Impl::DeviceInterfaceSettings::HasSettings() const { return m_settings; }
	unsigned int IMSSystem::Impl::DeviceInterfaceSettings::StartAddr() const { return m_start_addr; }
	unsigned int IMSSystem::Impl::DeviceInterfaceSettings::Length() const { return m_length; }
	const IMSSystem::Impl::DeviceInterfaceSettings& IMSSystem::Impl::Settings(const std::string& intf) const {
		return (m_settings.at(intf));
	}

	//static std::vector<std::uint16_t> fpga_build;
	//static FileSystemTable synth_fst;

	static int hwlist_callback(void *data, int argc, char **argv, char **azColName) {
		IMSSystem *system = (IMSSystem*)data;
		//for (i = 0; i < argc; i++)
		//{
		//	std::cout << azColName[i] << " = " << (argv[i] ? argv[i] : "NULL") << std::endl;
		//}
		if (argc <= 1) return -1;

		if (std::string(azColName[0]) == "Type") {
			if (std::string(argv[0]) == "Controller")
			{
				// Create strings and capabilities struct from database
				IMSController::Capabilities cap;
				std::string desc;
				std::string model;
				FWVersion ver = system->Ctlr().GetVersion();

				for (int i = 1; i < argc; i++)
				{
					if (argv[i] != nullptr)
					{
						// Get String arg
						std::string s(argv[i]);
						char* p;

						// Convert to bool
						bool b;
						b = (s == "TRUE" || s == "True" || s == "true");

						// And try parsing to int
						bool iparam_ok = true;
						int iparam = strtol(s.c_str(), &p, 0);
						if (*p) {
							iparam_ok = false;
						}
/*						try
						{
							iparam = std::stoi(s);
						}
						catch (std::invalid_argument&)
						{
							iparam_ok = false;
						}
						catch (std::out_of_range&)
						{
							iparam_ok = false;
						}*/
						if (iparam_ok) {
							// In a boolean field, if a number is present it represents the minimum major version number from which the capability was added
							b = (ver.major >= iparam);
						}

						if (std::string(azColName[i]) == "ModelNum") {
							model = s;
						}
						else if (std::string(azColName[i]) == "Description") {
							desc = s;
						}
						else if ((std::string(azColName[i]) == "nSynth") && iparam_ok) {
							cap.nSynthInterfaces = iparam;
						}
						else if (std::string(azColName[i]) == "FastTransfer") {
							cap.FastImageTransfer = b;
						}
						else if ((std::string(azColName[i]) == "MaxImageSize") && iparam_ok) {
							cap.MaxImageSize = iparam;
						}
						else if (std::string(azColName[i]) == "SimultaneousPlayback") {
							cap.SimultaneousPlayback = b;
						}
						else if ((std::string(azColName[i]) == "MaxImageRate") && iparam_ok) {
							cap.MaxImageRate = iparam;
						}
						else if (std::string(azColName[i]) == "RemoteUpgrade") {
							cap.RemoteUpgrade = b;
						}
					}
				}

				system->Ctlr(IMSController(model, desc, cap, ver, ImageTable()));
				BOOST_LOG_SEV(lg::get(), sev::info) << "Added Controller " << model << std::endl;
			}
			else if (std::string(argv[0]) == "Synthesiser")
			{
				// Create strings and capabilities struct from database
				IMSSynthesiser::Capabilities cap;
				std::string desc;
				std::string model;
				FWVersion ver = system->Synth().GetVersion();

				for (int i = 1; i < argc; i++)
				{
					if (argv[i] != nullptr)
					{
						// Get String arg
						std::string s(argv[i]);
						char* p;

						// Convert to bool
						bool b;
						if (s == "TRUE" || s == "True" || s == "true") b = true; else b = false;

						// try parsing to int
						bool iparam_ok = true;
						int iparam = strtol(s.c_str(), &p, 0);
						if (*p) {
							iparam_ok = false;
						}
/*						try 
						{
							iparam = std::stoi(s);
						}
						catch (std::invalid_argument&)
						{
							iparam_ok = false;
						}
						catch (std::out_of_range&)
						{
							iparam_ok = false;
						}*/
						if (iparam_ok) {
							// In a boolean field, if a number is present it represents the minimum major version number from which the capability was added
							b = (ver.major >= iparam);
						}

						// and to double
						bool dparam_ok = true;
						double dparam =	strtod(s.c_str(), &p);
						if (*p) {
							dparam_ok = false;
						}
/*						try
						{
							dparam = std::stod(s);
						}
						catch (std::invalid_argument&)
						{
							dparam_ok = false;
						}
						catch (std::out_of_range&)
						{
							dparam_ok = false;
						}*/

						if (std::string(azColName[i]) == "ModelNum") {
							model = s;
						}
						else if (std::string(azColName[i]) == "Description") {
							desc = s;
						}
						else if ((std::string(azColName[i]) == "MaxImageRate") && iparam_ok) {
							cap.MaxImageRate = iparam;
						}
						else if ((std::string(azColName[i]) == "LowerFreq") && dparam_ok) {
							cap.lowerFrequency = dparam;
						}
						else if ((std::string(azColName[i]) == "UpperFreq") && dparam_ok) {
							cap.upperFrequency = dparam;
						}
						else if ((std::string(azColName[i]) == "FreqBits") && iparam_ok) {
							cap.freqBits = iparam;
						}
						else if ((std::string(azColName[i]) == "AmplBits") && iparam_ok) {
							cap.amplBits = iparam;
						}
						else if ((std::string(azColName[i]) == "PhsBits") && iparam_ok) {
							cap.phaseBits = iparam;
						}
						else if ((std::string(azColName[i]) == "LUTDepth") && iparam_ok) {
							cap.LUTDepth = iparam;
						}
						else if ((std::string(azColName[i]) == "LUTAmplBits") && iparam_ok) {
							cap.LUTAmplBits = iparam;
						}
						else if ((std::string(azColName[i]) == "LUTPhaseBits") && iparam_ok) {
							cap.LUTPhaseBits = iparam;
						}
						else if ((std::string(azColName[i]) == "LUTSyncAnlgBits") && iparam_ok) {
							cap.LUTSyncABits = iparam;
						}
						else if ((std::string(azColName[i]) == "LUTSyncDigBits") && iparam_ok) {
							cap.LUTSyncDBits = iparam;
						}
						else if ((std::string(azColName[i]) == "SysClock") && dparam_ok) {
							cap.sysClock = dparam;
							cap.syncClock = dparam / 4.0;
						}
						else if ((std::string(azColName[i]) == "Channels") && iparam_ok) {
							cap.channels = iparam;
						}
						else if (std::string(azColName[i]) == "RemoteUpgrade") {
							cap.RemoteUpgrade = b;
						}
						else if ((std::string(azColName[i]) == "ChannelComp") && iparam_ok) {
							cap.ChannelComp = (ver.revision >= iparam) ? true : false;
						}
					}
				}

				system->Synth(IMSSynthesiser(model, desc, cap, ver, FileSystemTable(), nullptr));
				BOOST_LOG_SEV(lg::get(), sev::info) << "Added Synthesiser " << model << std::endl;
			}
		}
		else {
			BOOST_LOG_SEV(lg::get(), sev::warning) << "Unknown category " << azColName[0] << std::endl;			
			return -1;
		}
		return 0;
 	}

	bool IMSSystem::Impl::AddDevice(std::uint16_t magicID, std::string type)
	{
		sqlite3 *imshw = nullptr;
		int rc;

		imshw = get_db();
		if (imshw == nullptr) {
			BOOST_LOG_SEV(lg::get(), sev::error) << "imshw db = null" << std::endl;
			return false;
		}

		std::string getType("SELECT Type, * FROM hwlist WHERE Magic = ");
		getType += std::to_string(magicID);
        if (type != "") {
            getType += " AND Type = \"";
            getType += type;
            getType += "\"";
        }

		rc = sqlite3_exec(imshw, getType.c_str(), hwlist_callback, (void *)m_parent, nullptr);
		if (rc != SQLITE_OK) {
			sqlite3_close(imshw);
			BOOST_LOG_SEV(lg::get(), sev::error) << "imshw db err: " << rc << std::endl;
			return false;
		}

		sqlite3_close(imshw);
		return true;
	}

	bool IMSSystem::Initialise()
	{
		HostReport *iorpt;

        if (p_Impl->m_conn.get() == nullptr) return false;

		std::uint16_t ctrlr_magic=0xFFFF, synth_magic=0xFFFF;
		{
			// Read Controller Magic Number
			iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, 0);
			DeviceReport Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);
			if (Resp.Done()) {
				ctrlr_magic = Resp.Payload<std::uint16_t>();
				delete iorpt;
				// Probe for FPGA Version and Build Date/Time
				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, 5);
				ReportFields f = iorpt->Fields();
				f.len = 8;  // Read 4 successive registers
				iorpt->Fields(f);
				Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);
				if (Resp.Done()) {
					// Create initial Synth model with version data for parsing database
					std::vector<std::uint16_t> fpga_build = Resp.Payload<std::vector<std::uint16_t>>();
					fpga_build.resize(4);
					this->Ctlr(IMSController(std::string(""),
						std::string(""),
						IMSController::Capabilities(),
						FWVersion(fpga_build),
						ImageTable()));

					if (!p_Impl->AddDevice(ctrlr_magic, "Controller")) {
						BOOST_LOG_SEV(lg::get(), sev::error) << "Unable to access database" << std::endl;
						delete iorpt;
						return false;
					} 
					else {
						// Add FPGA Build
						ImageTableReader imgtr(shared_from_this());
						ImageTable ctlr_imgt = imgtr.Readback();
						const IMSController& c = this->Ctlr();
						IMSController Ctlr(c.Model(), c.Description(), c.GetCap(), c.GetVersion(), ctlr_imgt);
						this->Ctlr(Ctlr);
					}
				}
				delete iorpt;
			} else delete iorpt;
		}
		// Read Synthesiser Magic Number
		{
			iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, 0);
			DeviceReport Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);
			if (Resp.Done()) {
				synth_magic = Resp.Payload<std::uint16_t>();
				delete iorpt;
				// Probe for FPGA Version and Build Date/Time
				iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::READ, 5);
				ReportFields f = iorpt->Fields();
				f.len = 8;  // Read 4 successive registers
				iorpt->Fields(f);
				Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);
				if (Resp.Done()) {
					bool ret = true;

					// Create initial Synth model with version data for parsing database
					std::vector<std::uint16_t> fpga_build = Resp.Payload<std::vector<std::uint16_t>>();
					fpga_build.resize(4);

                    // Add version number
                    this->Synth(IMSSynthesiser(std::string(""),
                        std::string(""),
                        IMSSynthesiser::Capabilities(),
                        FWVersion(fpga_build),
                        FileSystemTable(),
                        nullptr));

                    // Populate capabilities and other fields
                    ret = p_Impl->AddDevice(synth_magic, "Synthesiser");
					if (!ret)
					{
						BOOST_LOG_SEV(lg::get(), sev::error) << "Unable to access database" << std::endl;
						delete iorpt;
						return false;
					}
					else {
						BOOST_LOG_SEV(lg::get(), sev::info) << "IMSSystem: reading back FST" << std::endl;
						FileSystemTableReader fstr(shared_from_this());
						FileSystemTable synth_fst = fstr.Readback();
						const IMSSynthesiser& s = this->Synth();
						IMSOption addon;

						IMSSynthesiser::Capabilities cap = s.GetCap();
						for (int i = 0; i < MAX_FST_ENTRIES; i++) {
							FileSystemTypes type = synth_fst[i].Type();
							std::string name = synth_fst[i].Name();
							if (type == FileSystemTypes::USER_DATA)
							{
								name.resize(3);
								// Detect Frequency doubled synth
								if (name.compare(std::string("FX2")) == 0)
								{
									addon = IMSOption(name);
									cap.sysClock = MHz(cap.sysClock * 2.0);
									cap.lowerFrequency = MHz(cap.lowerFrequency * 2.0);
									cap.upperFrequency = MHz(cap.upperFrequency * 2.0);
								}
							}
						}
						IMSSynthesiser Synth(s.Model(), s.Description(), cap, FWVersion(fpga_build), synth_fst, std::make_shared<IMSOption>(addon));
						this->Synth(Synth);

						FileSystemManager fsm(shared_from_this());
						for (int i = 0; i < MAX_FST_ENTRIES; i++) {
							FileSystemTypes type = synth_fst[i].Type();
							std::string name = synth_fst[i].Name();
							if ((type == FileSystemTypes::DDS_SCRIPT))
							{
								name.resize(7);
								// Remove any remnants of Enhanced Tone Mode
								if ((name.compare(std::string("alwaoww")) == 0) ||
									(name.compare(std::string("stoplsm")) == 0))
								{
									fsm.Delete(i);
								}
							}
						}
					}
				}
			}
			delete iorpt;
		}
		BOOST_LOG_SEV(lg::get(), sev::info) << "IMSSystem::Initialise() Complete" << std::endl;
		return true;
	}

	void IMSSystem::Connect()
	{
        if (p_Impl->m_conn.get() == nullptr) return;
		p_Impl->m_conn->Connect(p_Impl->m_connString);
	}

	void IMSSystem::Disconnect() 
	{
        if (p_Impl->m_conn.get() == nullptr) return;
		p_Impl->m_conn->Disconnect();
	}
	
	void IMSSystem::SetTimeouts(int send_timeout_ms, int rx_timeout_ms, int free_timeout_ms, int discover_timeout_ms)
	{
        if (p_Impl->m_conn.get() == nullptr) return;
		p_Impl->m_conn->SetTimeouts(send_timeout_ms, rx_timeout_ms, free_timeout_ms, discover_timeout_ms);
	}

	bool IMSSystem::Open() const
	{
        if (p_Impl->m_conn.get() == nullptr) return false;
		return p_Impl->m_conn->Open();
	}
	void IMSSystem::Ctlr(const IMSController& c)
	{
		p_Impl->m_Ctlr = c;
	}

	void IMSSystem::Synth(const IMSSynthesiser& s)
	{
		p_Impl->m_Synth = s;
	}

	bool IMSSystem::operator==(IMSSystem const& rhs) const {
		return p_Impl->m_connString == rhs.p_Impl->m_connString;
	}

	bool IMSSystem::ApplySettings(const IConnectionSettings& settings)
	{
		if (!Open()) return false;
		std::string module = settings.Ident();
		auto intf = p_Impl->m_Ctlr.Interfaces();
		if (std::find(std::begin(intf), std::end(intf), module) != std::end(intf)) {
			auto cfg = p_Impl->Settings(module);
			if (!cfg.HasSettings()) return false;
			HostReport *iorpt;
			iorpt = new HostReport(HostReport::Actions::CTRLR_SETTINGS, HostReport::Dir::WRITE, cfg.StartAddr());
			ReportFields fields = iorpt->Fields();
			fields.len = cfg.Length();
			iorpt->Fields(fields);
			iorpt->Payload<std::vector<unsigned char>>(settings.ProcessData());
			DeviceReport Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);

			if (!Resp.Done()) {
				return false;
			}
		}
		else {
			return false;
		}
		return true;
	}

	bool IMSSystem::RetrieveSettings(IConnectionSettings& settings)
	{
		if (!Open()) return false;
		std::string module = settings.Ident();
		auto intf = p_Impl->m_Ctlr.Interfaces();
		if (std::find(std::begin(intf), std::end(intf), module) != std::end(intf)) {
			auto cfg = p_Impl->Settings(module);
			if (!cfg.HasSettings()) return false;
			HostReport *iorpt;
			iorpt = new HostReport(HostReport::Actions::CTRLR_SETTINGS, HostReport::Dir::READ, cfg.StartAddr());
			ReportFields fields = iorpt->Fields();
			fields.len = cfg.Length();
			iorpt->Fields(fields);
			DeviceReport Resp = p_Impl->m_conn->SendMsgBlocking(*iorpt);

			if (!Resp.Done()) {
				return false;
			}
			settings.ProcessData(Resp.Payload<std::vector<unsigned char>>());
		}
		else {
			return false;
		}
		return true;
	}

	const IMSController& IMSSystem::Ctlr() const
	{
		return (p_Impl->m_Ctlr);
	}

	const IMSSynthesiser& IMSSystem::Synth() const
	{
		return (p_Impl->m_Synth);
	}

	const std::string& IMSSystem::ConnPort() const
	{
		return p_Impl->m_connString;
	}

	const std::shared_ptr<IConnectionManager> IMSSystem::Connection() const
	{
		return p_Impl->m_conn;
	}

	class IMSController::Impl
	{
	public:
		Impl()
			: m_caps(Capabilities()), m_desc("Invalid"), m_model("Null"), m_fwVer(FWVersion()), m_valid(false) {
			initSettingsList();
		};
		Impl(const std::string& model, const std::string& desc, const Capabilities& caps, const FWVersion& ver, const ImageTable& imgtable)
			: m_caps(caps), m_desc(desc), m_model(model), m_fwVer(ver), m_valid(true), m_imgtable(imgtable) {
			initSettingsList();
		};
		~Impl() {};

		void initSettingsList()
		{
#if defined (_WIN32)
			CS_RS422 cs_rs422;
			m_settingsNames.push_back(cs_rs422.Ident());
#endif
#if defined (_WIN32) || defined (__QNXNTO__) || defined(__linux__)
			CS_ETH cs_eth;
			m_settingsNames.push_back(cs_eth.Ident());
#endif
		}

		Capabilities m_caps;
		std::string m_desc;
		std::string m_model;
		FWVersion m_fwVer;
		bool m_valid{ false };
		ImageTable m_imgtable;
		ListBase<std::string> m_settingsNames;
	};

	IMSController::IMSController() : p_Impl(new Impl()) {}

	IMSController::IMSController(const std::string& model, const std::string& desc, const Capabilities& caps, const FWVersion& ver, const ImageTable& tbl)
		: p_Impl(new Impl(model, desc, caps, ver, tbl)) {}

	IMSController::~IMSController() { delete p_Impl; p_Impl = nullptr; }

	// Copy & Assignment Constructors
	IMSController::IMSController(const IMSController &rhs) : p_Impl(new Impl())
	{
		p_Impl->m_valid = rhs.p_Impl->m_valid;
		p_Impl->m_caps = rhs.p_Impl->m_caps;
		p_Impl->m_desc = rhs.p_Impl->m_desc;
		p_Impl->m_model = rhs.p_Impl->m_model;
		p_Impl->m_fwVer = rhs.p_Impl->m_fwVer;
		p_Impl->m_imgtable = rhs.p_Impl->m_imgtable;
	}
	IMSController& IMSController::operator =(const IMSController &rhs)
	{
		if (this == &rhs) return *this;
		p_Impl->m_valid = rhs.p_Impl->m_valid;
		p_Impl->m_caps = rhs.p_Impl->m_caps;
		p_Impl->m_desc = rhs.p_Impl->m_desc;
		p_Impl->m_model = rhs.p_Impl->m_model;
		p_Impl->m_fwVer = rhs.p_Impl->m_fwVer;
		p_Impl->m_imgtable = rhs.p_Impl->m_imgtable;
		return *this;
	}

	const IMSController::Capabilities IMSController::GetCap() const
	{
		return p_Impl->m_caps;
	}

	const std::string& IMSController::Description() const
	{
		return p_Impl->m_desc;
	}

	const std::string& IMSController::Model() const
	{
		return p_Impl->m_model;
	}

	const FWVersion& IMSController::GetVersion() const
	{
		return p_Impl->m_fwVer;
	}

	const ImageTable& IMSController::ImgTable() const
	{
		return p_Impl->m_imgtable;
	}

	const bool IMSController::IsValid() const
	{
		return p_Impl->m_valid;
	}

	const ListBase<std::string>& IMSController::Interfaces() const
	{
		return p_Impl->m_settingsNames;
	}

	class IMSSynthesiser::Impl
	{
	public:
		Impl()
			: m_caps(Capabilities()), m_desc("Invalid"), m_model("Null"), m_fwVer(FWVersion()), m_valid(false), m_addon(IMSOption()) {}
		Impl(const std::string& model, const std::string& desc, const Capabilities& caps, const FWVersion& ver, const FileSystemTable& fst, const std::shared_ptr<const IMSOption>& addon)
			: m_caps(caps), m_desc(desc), m_model(model), m_fwVer(ver), m_valid(true), m_fst(fst) {
			if (addon != nullptr) m_addon = *addon;
		}
		~Impl() {}

		Capabilities m_caps;
		std::string m_desc;
		std::string m_model;
		FWVersion m_fwVer;
		bool m_valid{ false };
		FileSystemTable m_fst;
		IMSOption m_addon;
	};

	IMSSynthesiser::IMSSynthesiser() : p_Impl(new Impl()) {}

	IMSSynthesiser::~IMSSynthesiser() { delete p_Impl; p_Impl = nullptr; }

	IMSSynthesiser::IMSSynthesiser(const std::string& model, const std::string& desc, const Capabilities& caps, const FWVersion& ver, const FileSystemTable& fst, const std::shared_ptr<const IMSOption>& addon)
		: p_Impl(new Impl(model, desc, caps, ver, fst, addon)) {};

	// Copy & Assignment Constructors
	IMSSynthesiser::IMSSynthesiser(const IMSSynthesiser &rhs) : p_Impl(new Impl())
	{
		p_Impl->m_valid = rhs.p_Impl->m_valid;
		p_Impl->m_caps = rhs.p_Impl->m_caps;
		p_Impl->m_desc = rhs.p_Impl->m_desc;
		p_Impl->m_model = rhs.p_Impl->m_model;
		p_Impl->m_fwVer = rhs.p_Impl->m_fwVer;
		p_Impl->m_fst = rhs.p_Impl->m_fst;
		p_Impl->m_addon = rhs.p_Impl->m_addon;
	}

	IMSSynthesiser& IMSSynthesiser::operator =(const IMSSynthesiser &rhs)
	{
		if (this == &rhs) return *this;
		p_Impl->m_valid = rhs.p_Impl->m_valid;
		p_Impl->m_caps = rhs.p_Impl->m_caps;
		p_Impl->m_desc = rhs.p_Impl->m_desc;
		p_Impl->m_model = rhs.p_Impl->m_model;
		p_Impl->m_fwVer = rhs.p_Impl->m_fwVer;
		p_Impl->m_fst = rhs.p_Impl->m_fst;
		p_Impl->m_addon = rhs.p_Impl->m_addon;
		return *this;
	}

	std::shared_ptr<const IMSOption> IMSSynthesiser::AddOn() const
	{
		if (p_Impl->m_addon.Name().empty()) return(nullptr);
		return (std::make_shared<const IMSOption>(p_Impl->m_addon));
	}

	const IMSSynthesiser::Capabilities IMSSynthesiser::GetCap() const
	{
		return p_Impl->m_caps;
	}

	const std::string& IMSSynthesiser::Description() const
	{
		return p_Impl->m_desc;
	}

	const std::string& IMSSynthesiser::Model() const
	{
		return p_Impl->m_model;
	}

	const FWVersion& IMSSynthesiser::GetVersion() const
	{
		return p_Impl->m_fwVer;
	}

	const bool IMSSynthesiser::IsValid() const
	{
		return p_Impl->m_valid;
	}

	const FileSystemTable& IMSSynthesiser::FST() const
	{
		return p_Impl->m_fst;
	}

	FWVersion::FWVersion() {};

	FWVersion::FWVersion(const std::vector<std::uint16_t>& data) :
		major((data.size() == 4) ? (data[0] >> 8) & 0xFF : -1),
		minor((data.size() == 4) ? (data[0] & 0xFF) : -1),
		revision((data.size() == 4) ? data[1] : -1),
		build_date{}
	{
		// The build date needs all four words; reading them from a shorter vector is undefined
		if (data.size() >= 4)
			build_date = { 0, (data[3] & 0xFF), ((data[3] >> 8) & 0xFF), (data[2] & 0x1F),
				((data[2] >> 5) & 0xF) - 1, 100 + ((data[2] >> 9) & 0x7F), 1, 1, 0 };
	}

	std::ostream& operator <<(std::ostream& stream, const FWVersion& ver) {
		char time[100];//"%a %b %d %R %Y
		std::strftime(time, sizeof(time), "  %a %b %d %Y %H:%M %Z", &ver.build_date);
		stream << ver.major << "." << ver.minor << "." << ver.revision << time;
		return stream;
	}
}
