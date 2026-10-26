/*-----------------------------------------------------------------------------
/ Title      : Private Utility Functions Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Other/src/PrivateUtil.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2022-02-23 14:10:12 +0000 (Wed, 23 Feb 2022) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 521 $
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

#include <array>
#include <vector>
#include <cstdint>
#include <string>

#include "PrivateUtil.h"
#include "readonlymemvfs.h"

#ifdef _WIN32
#include "resource.h"
#include "windows.h"
#else
extern "C" {
#include "imshw.h"
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

namespace iMS {

	// The Debug Logger
//	boost::log::sources::severity_logger_mt< logging::trivial::severity_level > lg;

	// Convert Variable To Bytes Vector
	/////////////////////////////////////
	template <typename T>
	std::vector<std::uint8_t> VarToBytes(const T &data)
	{
		unsigned char *bytes = ((unsigned char*)(&data));
		std::vector<unsigned char> output;

		for (unsigned int i = 0; i < sizeof(data); i++)
			output.push_back(bytes[i]);

		return output;
	}

	template std::vector<std::uint8_t> VarToBytes<std::uint8_t>(const std::uint8_t& data);
	template std::vector<std::uint8_t> VarToBytes<std::uint16_t>(const std::uint16_t& data);
	template std::vector<std::uint8_t> VarToBytes<std::uint32_t>(const std::uint32_t& data);
	template std::vector<std::uint8_t> VarToBytes<std::int8_t>(const std::int8_t& data);
	template std::vector<std::uint8_t> VarToBytes<std::int16_t>(const std::int16_t& data);
	template std::vector<std::uint8_t> VarToBytes<int>(const int& data);
	template std::vector<std::uint8_t> VarToBytes<double>(const double& data);
	template std::vector<std::uint8_t> VarToBytes<float>(const float& data);
	template std::vector<std::uint8_t> VarToBytes<char>(const char& data);

	// Convert Bytes Vector to variable
	//////////////////////////////////////
	template <typename T>
	T BytesToVar(const std::vector<std::uint8_t> &input_bytes)
	{
		if ((sizeof(T) <= input_bytes.size()) && (!input_bytes.empty()))
		{
			const T * pT = reinterpret_cast<const T*>(&input_bytes[0]);
			return *pT;
		}
		else
		{
			return (T());
		}
	}

	template std::uint8_t BytesToVar<std::uint8_t>(const std::vector<std::uint8_t> &input_bytes);
	template std::uint16_t BytesToVar<std::uint16_t>(const std::vector<std::uint8_t> &input_bytes);
	template std::uint32_t BytesToVar<std::uint32_t>(const std::vector<std::uint8_t> &input_bytes);
	template std::int8_t BytesToVar<std::int8_t>(const std::vector<std::uint8_t> &input_bytes);
	template std::int16_t BytesToVar<std::int16_t>(const std::vector<std::uint8_t> &input_bytes);
	template int BytesToVar<int>(const std::vector<std::uint8_t> &input_bytes);
	template double BytesToVar<double>(const std::vector<std::uint8_t> &input_bytes);
	template float BytesToVar<float>(const std::vector<std::uint8_t> &input_bytes);
	template char BytesToVar<char>(const std::vector<std::uint8_t> &input_bytes);

	// Convert Unsigned Integer to Char Array in (Little-)Endian-Safe way
	/////////////////////////////////////////////////////////////////////
	template <typename T>
	T PCharToUInt(const char * const c)
	{
		T d = 0;
		for (int i = 0; i < (int)sizeof(d); i++)
		{
			d |= (static_cast<T>(*(c + i)) & 0xFF) << (8 * i);
		}
		return d;
	}

	template std::uint8_t PCharToUInt<std::uint8_t>(const char * const c);
	template std::uint16_t PCharToUInt<std::uint16_t>(const char * const c);
	template std::uint32_t PCharToUInt<std::uint32_t>(const char * const c);
	template std::uint64_t PCharToUInt<std::uint64_t>(const char * const c);

	// Convert Char Array to Unsigned Integer in (Little-)Endian-Safe way
	/////////////////////////////////////////////////////////////////////
	template <typename T>
	void UIntToPChar(char * const c, const T& t)
	{
		T d = t;
		for (int i = 0; i < (int)sizeof(d); i++)
		{
			(*(c + i)) = (d & 0xFF);
			d >>= 8;
		}
	}

	template void UIntToPChar<std::uint8_t>(char * const c, const std::uint8_t& t);
	template void UIntToPChar<std::uint16_t>(char * const c, const std::uint16_t& t);
	template void UIntToPChar<std::uint32_t>(char * const c, const std::uint32_t& t);
	template void UIntToPChar<std::uint64_t>(char * const c, const std::uint64_t& t);

	std::string UUIDToStr(const std::array<std::uint8_t, 16>& arr)
	{
		std::string s;
		for (std::array<std::uint8_t, 16>::const_iterator it = arr.cbegin(); it != arr.cend(); ++it)
		{
			unsigned char c = (*it & 0xF0) >> 4;
			if (c > 9) c += ('a' - 10);
			else c += '0';
			s += c;
			c = (*it & 0xF);
			if (c > 9) c += ('a' - 10);
			else c += '0';
			s += c;
		}
		return s;
	}

	std::array<std::uint8_t, 16> StrToUUID(const std::string& str)
	{
		std::array<std::uint8_t, 16> arr;
		std::string s(str);
		s.resize(32, '0');
		std::uint8_t c = 0;
		int i = 0;
		for (std::string::const_iterator it = s.cbegin(); it != s.cend(); ++it)
		{
			std::uint8_t c2 = *it;
			if (c2 >= '0' && c2 <= '9') {
				c2 -= '0';
			} else if(c2 >= 'a' && c2 <= 'f') {
				c2 -= 'a';
			}
			else if (c2 >= 'A' && c2 <= 'F') {
				c2 -= 'A';
			}
			else return std::array<std::uint8_t, 16>();
			if (!(i % 2)) {
				c = (c2 & 0xF) << 4;
				i++;
			}
			else {
				c |= (c2 & 0xF);
				arr[i++/2] = c;
			}
		}
		return arr;
	}

	sqlite3* get_db()
	{
		sqlite3 *db = nullptr;
		void* pBuffer = NULL;
		char errcode = 0;

		//boost::filesystem::path tempPath = boost::filesystem::temp_directory_path();
		//std::string tempPathString = tempPath.generic_string<std::string>();
#ifdef _WIN32
		HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);

		// credit: http://www.tolon.co.uk/2012/04/in-memory-sqlite/
		HRSRC hr = ::FindResource(hModule, MAKEINTRESOURCE(IDR_RCDATA1), RT_RCDATA);

		if (hr)
		{
			HGLOBAL hg = ::LoadResource(hModule, hr);

			if (hg)
			{
				DWORD dwSize = ::SizeofResource(hModule, hr);
				pBuffer = ::LockResource(hg);
#else
		{
			{
				pBuffer = (void *)imshw_db;
				unsigned int dwSize = imshw_db_len;
#endif
				set_mem_db(pBuffer, dwSize);

				int nInitResult = readonlymemvfs_init();
				//assert(nInitResult == SQLITE_OK);
				if (nInitResult != SQLITE_OK) {
					return nullptr;
				}
				errcode = sqlite3_open_v2("imshw-db", &db,
					SQLITE_OPEN_READONLY, READONLY_MEM_VFS_NAME);
			}
		}

		if (errcode) {
			// MSVC complains of potentially uninitialized local pointer but this can be harmlessly assigned to nullptr (as above)
			// as SQLITE mandates that this will only result in a nop
			sqlite3_close(db);
			return nullptr;
		}
		return db;
	}

    bool float_compare(double a, double b, double epsilon) { 
        return std::fabs(a - b) < epsilon; 
    }

    LazyWorker::LazyWorker(WorkerFunc func)
        : workerFunc(std::move(func)) {}

    LazyWorker::~LazyWorker() {
        stop();
    }

    void LazyWorker::start() {
        std::call_once(startFlag, [this]() {
            running = true;
            workerThread = std::thread([this]() {
                // Signal ready
                {
                    std::lock_guard<std::mutex> lck(readyMutex);
                    ready = true;
                }
                readyCond.notify_one();

                // Run worker loop
                workerFunc(running, workCond, workMutex);
            });
        });

        // Wait until ready
        {
            std::unique_lock<std::mutex> lck(readyMutex);
            if (!ready) {
                readyCond.wait(lck, [this]() { return ready; });
            }
        }
    }

    void LazyWorker::notify() {
        workCond.notify_one();
    }

    void LazyWorker::stop() {
        if (started()) {
            {
                std::lock_guard<std::mutex> lck(workMutex);
                running = false;
            }
            workCond.notify_one();
            if (workerThread.joinable()) {
                workerThread.join();
            }
        }
    }

    bool LazyWorker::started() const {
        return workerThread.joinable();
    }

    std::mutex& LazyWorker::mutex() {
        return workMutex;
    }    
 
}
