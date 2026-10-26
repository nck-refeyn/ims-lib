/*-----------------------------------------------------------------------------
/ Title      : Connection List Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ConnectionManager/src/ConnectionList.cpp $
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

#include "IConnectionManager.h"
#include "ConnectionList.h"
#if defined (_WIN32)
#include "CM_FTDI.h"
#include "CM_CYUSB.h"
#include "CM_RS422.h"
#endif
#if defined (_WIN32) || defined (__QNXNTO__) || defined (__linux__)
#include "CM_ENET.h"
#endif

#include "PrivateUtil.h"  // for logging
#include "LibVersion.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/log/utility/setup/from_settings.hpp>
#include <boost/log/utility/setup/settings_parser.hpp>
#include "boost/filesystem.hpp"

#include <memory>

#if defined(_WIN32)
#include "Shlobj.h"
#endif

#ifdef _DEBUG
#include "crtdbg.h"
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

#ifdef _WIN32
#include "resource.h"
#include "windows.h"
#else
extern "C" {
#include "settings_ini.h"
}
#endif

// Include symbol for the current memory address of the executing module (DLL or EXE)
// see: http://www.codeguru.com/cpp/w-p/dll/tips/article.php/c3635/Tip-Detecting-a-HMODULEHINSTANCE-Handle-Within-the-Module-Youre-Running-In.htm
#if (defined(_MSC_VER) && _MSC_VER >= 1300) || defined(__MINGW32__)    // for VC 7.0 / MinGW
// from ATL 7.0 sources
#ifndef _delayimp_h
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif
#endif

namespace pt = boost::property_tree;

static boost::filesystem::path get_settings_path()
{
	boost::filesystem::path settings_path;

#if defined(_WIN32)
	LPWSTR wszPath = NULL;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, NULL, &wszPath)))
	{
		settings_path = wszPath;
	}
	if (!boost::filesystem::exists(settings_path += L"\\Isomet")) {
		boost::filesystem::create_directory(settings_path);
	}
	if (!boost::filesystem::exists(settings_path += L"\\iMS_SDK")) {
		boost::filesystem::create_directory(settings_path);
	}
#else
	auto home = boost::filesystem::path(getenv("HOME"));
	if (home.size() < 1) {
		// strange there is no home folder, but we have to go somewhere so we'll put it in tmp 
		home = "/tmp";
	}
	settings_path += home;

	if (!boost::filesystem::exists(settings_path += "/.config")) {
		boost::filesystem::create_directory(settings_path);
	}
	if (!boost::filesystem::exists(settings_path += "/ims")) {
		boost::filesystem::create_directory(settings_path);
	}
#endif

	return settings_path;
}

static void init_logging()
{
/*	auto log_path = boost::filesystem::temp_directory_path();
	log_path += "/imslog_%5N.log";
	logging::add_file_log
	(
		keywords::file_name = log_path,
		keywords::rotation_size = 10 * 1024 * 1024,
		keywords::time_based_rotation = sinks::file::rotation_at_time_point(0, 0, 0),
		keywords::format = "[%TimeStamp%]: (%Severity%) %Message%",
		keywords::open_mode = (std::ios::out | std::ios::app)
	);

	logging::core::get()->set_filter
	(
		logging::trivial::severity >= logging::trivial::info
	);*/

	// Look for a configuration file and read logging settings
	boost::filesystem::path settings_path = get_settings_path();
#if defined(_WIN32)
	
	auto old_settings = settings_path;
	old_settings += L"\\settings.ini";
	if (boost::filesystem::exists(old_settings)) {
		boost::filesystem::remove(old_settings);
	}
	
	// Changed this as of SDK v1.8.4 so any new installs will pick up the new settings (with asynchronous = true, auto flush = false)
	if (!boost::filesystem::exists(settings_path += L"\\logging.ini")) 
	{
			// Write a default configuration file
		HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);

		HRSRC hr = ::FindResource(hModule, MAKEINTRESOURCE(IDR_SETTINGS1), L"SETTINGS");

		if (hr)
		{
			HGLOBAL hg = ::LoadResource(hModule, hr);

			if (hg)
			{
				LPVOID pBuffer = LockResource(hg);

				if (pBuffer)
				{
					DWORD dwSize = ::SizeofResource(hModule, hr);
#else
	if (!boost::filesystem::exists(settings_path += "/logging")) 
	{
		{
			{
				{
					void * pBuffer = (void *)settings_ini;
					unsigned int dwSize = settings_ini_len;
#endif
					if (0 != dwSize)
					{
						std::ofstream ofs(settings_path.string(), std::ios::binary);
						ofs.write((const char *)pBuffer, dwSize);
					}
				}
			}
		}
	}
	
	{
		boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
		boost::log::register_simple_filter_factory<boost::log::trivial::severity_level, char>("Severity");
		boost::log::add_common_attributes();

		// Open the settings file
		try {
			std::ifstream file(settings_path.string());
			auto settings = logging::parse_settings(file);
			boost::property_tree::ptree pt = settings.property_tree();
			auto log_path = boost::filesystem::temp_directory_path();
			namespace bfs = boost::filesystem;
			bfs::path slash("/");
			std::string logfilename = log_path.string() + slash.make_preferred().string() 
				+ bfs::path("imslog_%5N.log").string();
			if (boost::optional<std::string> fn = pt.get_optional<std::string>("Sinks.LogFile.FileName"))
			{
				logfilename = *fn;
				if ((fn->find('\\') == std::string::npos) && (fn->find('/') == std::string::npos))
				{
					// User didn't specify a path. Use temp folder
					logfilename = log_path.string() + slash.make_preferred().string() + logfilename;
				}
			}
			pt.put<std::string>("Sinks.LogFile.FileName", logfilename);
			settings.property_tree() = pt;
			logging::init_from_settings(settings);
			//logging::init_from_stream(file);

			BOOST_LOG_SEV(iMS::lg::get(), sev::info) << std::string(" << Start Logging >>");
			std::stringstream ss;
			ss << " << Using iMS Library Version " << iMS::LibVersion::GetVersion() << " >>";
			BOOST_LOG_SEV(iMS::lg::get(), sev::info) << ss.str();
		}
		catch (std::exception&)
		{
			logging::core::get()->set_logging_enabled(false);
		}

	}
}

static void stop_logging()
{
	BOOST_LOG_SEV(iMS::lg::get(), sev::info) << std::string(" << Stop Logging >>");

	auto core = logging::core::get();
	
	core->set_logging_enabled(false);
	core->flush();
	core->remove_all_sinks();
}

struct ConnectionControl
{
	ConnectionControl() :
		SendTimeout(200),
		RxTimeout(500),
		FreeTimeout(10000),
		DiscoveryTimeout(500),
		IncludeInScan(true) {}

	int SendTimeout;
	int RxTimeout;
	int FreeTimeout;
	int DiscoveryTimeout;
	bool IncludeInScan;
};

struct ConnectionControlMap
{
	typedef std::map<std::string, ConnectionControl> ControlMap_t;
	ControlMap_t mControlMap;
	std::string mFilename;

	void Load(const std::string& filename);
	void Save(const std::string& filename);
};

const pt::ptree& empty_ptree() {
	static pt::ptree t;
	return t;
}

void ConnectionControlMap::Load(const std::string& filename)
{
	mFilename = filename;
	pt::ptree tree;

	try {
		pt::read_xml(filename, tree);

		BOOST_FOREACH(const pt::ptree::value_type & v, tree.get_child("connection.modules", empty_ptree())) {
			const pt::ptree& attributes = v.second.get_child("<xmlattr>", empty_ptree());
			std::string module_name = "";
			BOOST_FOREACH(const pt::ptree::value_type & f, attributes) {
				if (!f.first.compare("Name")) module_name = f.second.data();
			}
			if (module_name != "") {
				ConnectionControl controlSettings;
				controlSettings.SendTimeout = v.second.get("send_timeout", 200);
				controlSettings.RxTimeout = v.second.get("recv_timeout", 500);
				controlSettings.FreeTimeout = v.second.get("free_timeout", 10000);
				controlSettings.DiscoveryTimeout = v.second.get("discover_timeout", 500);
				controlSettings.IncludeInScan = v.second.get("scan", true);

				mControlMap.emplace(module_name, controlSettings);

//				std::cout << "Module " << module_name << ": Send " << controlSettings.SendTimeout << " Rx " << controlSettings.RxTimeout << " Free " << controlSettings.FreeTimeout << " Disc " << controlSettings.DiscoveryTimeout << " Scan " << controlSettings.IncludeInScan << std::endl;
			}
		}
	}
	catch (pt::xml_parser::xml_parser_error ex)
	{
		// No file or invalid contents
		return;
	}

}

void ConnectionControlMap::Save(const std::string& filename)
{
	pt::ptree tree;

	BOOST_FOREACH(const ControlMap_t::value_type& v, mControlMap)
	{
		pt::ptree module_tree;
		module_tree.add("<xmlattr>.Name", v.first);
		module_tree.put("send_timeout", v.second.SendTimeout);
		module_tree.put("recv_timeout", v.second.RxTimeout);
		module_tree.put("free_timeout", v.second.FreeTimeout);
		module_tree.put("discover_timeout", v.second.DiscoveryTimeout);
		module_tree.put("scan", v.second.IncludeInScan);

		tree.add_child("connection.modules.module", module_tree);
	}

	try {
		pt::write_xml(filename, tree);
	}
	catch (pt::xml_parser::xml_parser_error ex)
	{
		// Unable to write file
		return;
	}
}

namespace iMS
{
	class ConnectionList::Impl
	{
	public:
		Impl();
		~Impl();

		// The list of connection types
		typedef std::list<std::shared_ptr<IConnectionManager>> ConnectionTypesList;
		std::unique_ptr<ConnectionTypesList> connList;
		ListBase<std::string> ModuleNames;

		typedef ConnectionTypesList::iterator iterator;
		typedef ConnectionTypesList::const_iterator const_iterator;
		ConnectionTypesList::iterator begin() 	{ return connList->begin(); }
		ConnectionTypesList::iterator end()   	{ return connList->end(); }

		typedef std::map<std::string, ConnectionConfig> ConnectionConfigMap;
		std::unique_ptr<ConnectionConfigMap> config_map;

		std::unique_ptr<ConnectionControlMap> control_map;

        typedef std::map<std::string, std::shared_ptr<IConnectionSettings>> ConnectionSettingsMap;
        std::unique_ptr<ConnectionSettingsMap> settings_map;
	};

	ConnectionList::Impl::Impl()
	{
		connList = std::make_unique<ConnectionTypesList>();
		config_map = std::make_unique<ConnectionConfigMap>();
		control_map = std::make_unique<ConnectionControlMap>();
        settings_map = std::make_unique<ConnectionSettingsMap>();

		init_logging();
		logging::add_common_attributes();

		boost::filesystem::path settings_path = get_settings_path();

#if defined(_WIN32)
		settings_path += L"\\connectionv2.xml";
#else
		settings_path += "/connectionv2.xml";
#endif
		control_map->Load(settings_path.string());

		BOOST_LOG_SEV(lg::get(), sev::trace) << std::string("ConnectionList::ConnectionList()");
	}

	ConnectionList::Impl::~Impl()
	{
		BOOST_LOG_SEV(lg::get(), sev::trace) << std::string("ConnectionList::~ConnectionList()");

		stop_logging();
	}

	ConnectionList::ConnectionConfig::ConnectionConfig() 
		: IncludeInScan(true)
	{}

	ConnectionList::ConnectionConfig::ConnectionConfig(bool inc)
		: IncludeInScan(inc)
	{}

	ConnectionList::ConnectionConfig::ConnectionConfig(const ListBase<std::string>& mask)
		: IncludeInScan(true), PortMask(mask)
	{}

	ConnectionList::ConnectionList(unsigned int max_discover_timeout_ms) : pImpl(new Impl())
	{
		// connList is a read-only list containing one element for each host connection type.
		// Each element must fully implement the IConnectionManager interface to define methods
		//   for discovering, connecting to and sending messages to/from the iMS.
		// Add in any additional concrete clases that are an implementation of the IConnectionManager interface below
#if defined (_WIN32) 
		try {
			auto module = CM_FTDI::Create ();
			pImpl->connList->push_back(module);
		}
		catch (const std::exception& ex)
        {
            BOOST_LOG_SEV(lg::get(), sev::warning) << "Failed to initialise CM_FTDI Module: " << ex.what();
        }
#endif
#if defined (_WIN32) 
		try {
			auto module = CM_CYUSB::Create ();
			pImpl->connList->push_back(module);
		}
		catch (const std::exception& ex)
        {
            BOOST_LOG_SEV(lg::get(), sev::warning) << "Failed to initialise CM_CYUSB Module: " << ex.what();
        }
#endif
#if defined (_WIN32) 
		try {
			auto module = CM_RS422::Create ();
			pImpl->connList->push_back(module);
		}
		catch (const std::exception& ex)
        {
            BOOST_LOG_SEV(lg::get(), sev::warning) << "Failed to initialise CM_RS422 Module: " << ex.what();
        }
#endif
#if defined (_WIN32) || defined (__QNXNTO__) || defined (__linux__)
		try {
			auto module = CM_ENET::Create ();
			pImpl->connList->push_back(module);
		}
		catch (const std::exception& ex)
        {
            BOOST_LOG_SEV(lg::get(), sev::warning) << "Failed to initialise CM_ENET Module: " << ex.what();
        }
#endif

		for (ConnectionList::Impl::const_iterator iter = pImpl->begin();
			iter != pImpl->end();
			iter++)
		{
			// Apply Connection Settings (Creates default entry if it doesn't exist)
			const auto& settings = pImpl->control_map->mControlMap[(*iter)->Ident()];

            int discovery_timeout = settings.DiscoveryTimeout;
            if (discovery_timeout > max_discover_timeout_ms) discovery_timeout = (int)max_discover_timeout_ms;
			(*iter)->SetTimeouts(settings.SendTimeout, settings.RxTimeout, settings.FreeTimeout, discovery_timeout);
			pImpl->config_map->emplace((*iter)->Ident(), ConnectionConfig(settings.IncludeInScan));
            pImpl->settings_map->emplace((*iter)->Ident(), nullptr);
			pImpl->ModuleNames.push_back((*iter)->Ident());
		}

		pImpl->control_map->Save(pImpl->control_map->mFilename);
	}

	ConnectionList::~ConnectionList()
	{
		delete pImpl;
	}

	ConnectionList::ConnectionConfig& ConnectionList::Config(const std::string& module)
	{
		return (*pImpl->config_map)[module];
	}

	const ListBase<std::string>& ConnectionList::Modules() const
	{
		return pImpl->ModuleNames;
	}

    void ConnectionList::Settings(const std::string& module, const IConnectionSettings* settings)
    {
        auto iter = pImpl->settings_map->find(module);
        if (iter != pImpl->settings_map->end() )
        {
            iter->second = settings ? settings->Clone() : nullptr;
        }
    }

	std::vector<std::shared_ptr<IMSSystem>> ConnectionList::Scan()
	{
		std::vector<std::shared_ptr<IMSSystem>> fulliMSList;
		for (ConnectionList::Impl::const_iterator iter = pImpl->begin();
			iter != pImpl->end();
			iter++)
		{
			auto object = *iter;

			if ((*pImpl->config_map)[object->Ident()].IncludeInScan) {
				BOOST_LOG_SEV(lg::get(), sev::info) << "Scan(" << object->Ident() << ") start" << std::endl;
				std::vector<std::shared_ptr<IMSSystem>> newiMSList = object->Discover((*pImpl->config_map)[object->Ident()].PortMask, (*pImpl->settings_map)[object->Ident()]);

				// Add newly found iMS's to the full list of iMS's
				fulliMSList.insert(fulliMSList.end(), newiMSList.begin(), newiMSList.end());
				BOOST_LOG_SEV(lg::get(), sev::info) << "Scan(" << object->Ident() << ") finish: found " << newiMSList.size() << std::endl;
			} else {
				BOOST_LOG_SEV(lg::get(), sev::info) << "Scan(" << object->Ident() << ") disabled" << std::endl;
			}
		}
		return fulliMSList;
	}

    std::shared_ptr<IMSSystem> ConnectionList::Scan(
        const std::string& interfaceName,
        const std::vector<std::string>& addressHints
    ) {
        std::vector<std::shared_ptr<IMSSystem>> candidates;

        ListBase<std::string> PortMask;
        for (auto const& s : addressHints) { PortMask.push_back(s);}

        BOOST_LOG_SEV(lg::get(), sev::info) << "Scan(" << interfaceName << ") start" << std::endl;
        for (ConnectionList::Impl::const_iterator iter = pImpl->begin();
			iter != pImpl->end();
			iter++)
		{
			auto object = *iter;

            if (object->Ident() == interfaceName) {
                candidates = object->Discover(PortMask, (*pImpl->settings_map)[object->Ident()]);
                break;
            }
        }

        BOOST_LOG_SEV(lg::get(), sev::info) << "Scan(" << interfaceName << ") finish: found " << candidates.size() << std::endl;
        if (candidates.empty()) return nullptr;
        return candidates.front();
    }

    std::shared_ptr<IMSSystem> ConnectionList::Find(
        const std::string& interfaceName,
        const std::string& systemId,
        const std::vector<std::string>& addressHints
    ) {
        std::vector<std::shared_ptr<IMSSystem>> candidates;

        ListBase<std::string> PortMask;
        for (auto const& s : addressHints) { PortMask.push_back(s);}

        BOOST_LOG_SEV(lg::get(), sev::info) << "Find(" << interfaceName << ") start: looking for " << systemId << std::endl;
        for (ConnectionList::Impl::const_iterator iter = pImpl->begin();
			iter != pImpl->end();
			iter++)
		{
			auto object = *iter;

            if (object->Ident() == interfaceName) {
                candidates = object->Discover(PortMask, (*pImpl->settings_map)[object->Ident()]);
                break;
            }
        }

        BOOST_LOG_SEV(lg::get(), sev::info) << "Find(" << interfaceName << ") finish: found " << candidates.size() << std::endl;
        for (auto&& ims: candidates) {
            if (ims && ims->ConnPort().find(systemId) != std::string::npos) {
                BOOST_LOG_SEV(lg::get(), sev::info) << "System " << systemId << " found OK!" << std::endl;
                return ims;
            }
        }

        BOOST_LOG_SEV(lg::get(), sev::info) << "System " << systemId << " NOT found!" << std::endl;
        return nullptr;
    }

}
