/*-----------------------------------------------------------------------------
/ Title      : Connection Manager Implementation for RS422 Connections
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ConnectionManager/src/CM_RS422.cpp $
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

#if defined(_WIN32)

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <codecvt>
#include <atomic>

#include "CM_RS422.h"
#include "CS_RS422.h"
#include "IMSSystem.h"
#include "PrivateUtil.h"

#ifdef WIN32
#define CENUMERATESERIAL_USE_STL
#include "atlbase.h"
#include "setupapi.h"
#include "enumser.h"
#endif

#if defined _WIN32 && defined _DEBUG
#include "crtdbg.h"
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace iMS {

	// All private data and member functions contained within Impl class
	class CM_RS422::Impl
	{
	public:
		Impl();
		~Impl();

		static const int MaxMessageSize = 73;
		static const int RxBufferSize = 4096;

		const std::string Ident = "CM_SERIAL";
		ListBase<std::string> PortMask;

		std::vector<std::shared_ptr<IMSSystem>> ListConnectedDevices();

		std::map<std::string, std::string>* rs422_list = nullptr;
		HANDLE fp;

		/*std::deque<std::shared_ptr<CM_RS422::MsgContext>> m_rxBuf;
		int m_rxbuf_size;
		mutable std::mutex m_rxBufmutex;
		std::condition_variable m_rxBufcond;*/
		mutable std::timed_mutex m_rdwrmutex;

		// Interrupt receiving thread
		std::thread interruptThread;
		std::shared_ptr<std::vector<uint8_t>> interruptData;

#ifdef WIN32
        HANDLE hShutdown;
#endif
        void SetOwner(std::shared_ptr<IConnectionManager> mgr) {m_parent = mgr;}
	private:
		std::shared_ptr<IConnectionManager> m_parent;
	};

	CM_RS422::Impl::Impl()
	{
	}

	CM_RS422::Impl::~Impl()
	{
		if (rs422_list != nullptr) {
			delete rs422_list;
			rs422_list = nullptr;
		}
	}
	
    std::shared_ptr<IConnectionManager> CM_RS422::Create() {
        auto instance = std::shared_ptr<CM_RS422>(new CM_RS422());
        instance->pImpl->SetOwner(instance);  // safe now
        return instance;
    }

	std::vector<std::shared_ptr<IMSSystem>> CM_RS422::Impl::ListConnectedDevices()
	{
		//CEnumerateSerial::CPortsArray ports;
		CEnumerateSerial::CNamesArray names;
		std::vector<std::shared_ptr<IMSSystem>> IMSList;
		std::string port;

		// If we have an open connection, we can't allow the driver to be opened again to look for devices
		if (m_parent->Open()) return (std::vector<std::shared_ptr<IMSSystem>>());

#ifdef WIN32
		// Get available ports
		if (CEnumerateSerial::UsingRegistry(names))
		{
#ifdef CENUMERATESERIAL_USE_STL
			for (int i = 0; i < (int)names.size(); i++) {
				//				_tprintf(_T("%s\n"), names[i].c_str());
//				swprintf_s(port, L"\\\\.\\%s", names[i].c_str());       //create a comport name, from COM0 to COM31.  Port numbers greater than 9 require backslash prefix
#else
			for (i = 0; i<names.GetSize(); i++) {
				//				_tprintf(_T("%s\n"), names[i].operator LPCTSTR());
				//swprintf_s(port, L"\\\\.\\%s",  names[i].operator LPCTSTR());       //create a comport name, from COM0 to COM31.  Port numbers greater than 9 require backslash prefix
#endif
#ifdef _UNICODE
				using convert_type = std::codecvt_utf8<wchar_t>;
				std::wstring_convert<convert_type, wchar_t> converter;
				port = converter.to_bytes(names[i]);
#else
				port = names[i];
#endif
				if (!this->PortMask.empty())
				{
					// User supplied a mask list. Check if this interface is included
					bool match = false;
					for (std::list<std::string>::const_iterator it = this->PortMask.cbegin(); it != this->PortMask.cend(); ++it)
					{
						if (port == *it) {
							match = true;
							break;
						}
					}
					if (!match) continue;
				}

				const std::string false_serial("iMSP9999");
				(*rs422_list)[false_serial] = port;

				// Then open device and send a Magic Number query message
				//ftStatus = FT_OpenEx(devInfo.SerialNumber, FT_OPEN_BY_SERIAL_NUMBER, &ftdiDevice);
				m_parent->Connect(false_serial);
				rs422_list->erase(false_serial);

				if (!m_parent->Open())
					continue;

				HostReport *iorpt;
				iorpt = new HostReport(HostReport::Actions::CTRLR_SETTINGS, HostReport::Dir::READ, 0);
				ReportFields fields = iorpt->Fields();
				fields.len = 16; // Read first 16 bytes of controller settings
				iorpt->Fields(fields);
				DeviceReport Resp = m_parent->SendMsgBlocking(*iorpt);

				if (!Resp.Done()) {
					m_parent->Disconnect();
					continue;
				}

				const std::string serial = Resp.Payload<std::string>() + ":" + port;

				// Store serial number along with USB Device count index in cyusb_list map
				(*rs422_list)[serial] = port;

				if (m_parent->Open())
				{
					// Found a suitable connection interface, let's query its contents.
					// An FTDI based board can have a maximum of one controller and one synthesiser.
					auto thisiMS = IMSSystem::Create (m_parent, serial);

					thisiMS->Initialise();

					if (thisiMS->Ctlr().IsValid() || thisiMS->Synth().IsValid()) {
						IMSList.push_back(thisiMS);
					}
				}

				m_parent->Disconnect();
			}
		}
		//else
			//_tprintf(_T("CEnumerateSerial::UsingRegistry failed, Error:%u\n"), GetLastError());

#endif
		return IMSList;
	}

	// Default Constructor
	CM_RS422::CM_RS422() : pImpl(new CM_RS422::Impl())
	{
		sendTimeout = std::chrono::milliseconds(100);
		rxTimeout = std::chrono::milliseconds(1000);
		autoFreeTimeout = std::chrono::milliseconds(10000);
        connSettings = nullptr;
	}

	CM_RS422::~CM_RS422()
	{
		this->Disconnect();
		 
		delete pImpl;
		pImpl = NULL;
	}

	const std::string& CM_RS422::Ident() const
	{
		return pImpl->Ident;
	}

	std::vector<std::shared_ptr<IMSSystem>> CM_RS422::Discover(const ListBase<std::string>& PortMask, std::shared_ptr<IConnectionSettings> settings)
	{
//		std::cout << "CM_RS422::Discover()" << std::endl;
        connSettings = settings;
		pImpl->PortMask = PortMask;
		pImpl->rs422_list = new std::map<std::string, std::string>();
		std::vector<std::shared_ptr<IMSSystem>> v = pImpl->ListConnectedDevices();
		return v;
	}

	void CM_RS422::Connect(const std::string& serial)
	{
		if (!DeviceIsOpen)
		{
            CS_RS422* cs_rs422 = nullptr;
            if (connSettings && connSettings->Ident() == "CS_RS422")
                cs_rs422 = (CS_RS422*)connSettings.get();

			// If connecting without first performing a scan, carry it out here
			if (pImpl->rs422_list == nullptr) {
				pImpl->rs422_list = new std::map<std::string, std::string>();
                pImpl->ListConnectedDevices();
            }

			if (pImpl->fp != NULL)
				this->Disconnect();

			std::string portname;
			// Remove trailing interface descriptor, if present
			std::string serial_ = serial.substr(0, serial.find_first_of(":"));

			// Look for serial number in internal list
			if (pImpl->rs422_list->count(serial_) > 0) {
				// Found it
				portname = (*pImpl->rs422_list)[serial_];
			}
			else {
				// search for the word "COM" and if found, substring from it to the end of the string.
				std::size_t found = serial.find("COM");
				if (found != std::string::npos) {
					portname = serial.substr(found, std::string::npos);
				}
			}

			if (portname.empty()) return;

			portname.insert(0, "\\\\.\\");
#ifdef UNICODE
			const char* cs = portname.c_str();
			size_t wn = mbsrtowcs(NULL, &cs, 0, NULL);
			if (wn == size_t(-1)) return;

			wchar_t* port = new wchar_t[wn + 1]();
			wn = mbsrtowcs(port, &cs, wn + 1, NULL);
#else 
			const char* port = portname.c_str();
#endif

			pImpl->fp = CreateFile(port,
				GENERIC_READ | GENERIC_WRITE,
				0, /* exclusive access */
				NULL, /* no security attrs */
				OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED,   /* Or NULL for non-overlapped */
				NULL
				);

            // Used to unblock the waiting threads
            pImpl->hShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);

#ifdef UNICODE
			delete[] port;
#endif

			if (pImpl->fp == INVALID_HANDLE_VALUE) return; //Not present on this port

			COMMTIMEOUTS ctmoNew = { 0 };

			GetCommTimeouts(pImpl->fp, &ctmoNew);
			ctmoNew.ReadIntervalTimeout = 1;
			ctmoNew.ReadTotalTimeoutConstant = 1;
			ctmoNew.ReadTotalTimeoutMultiplier = 1;
			ctmoNew.WriteTotalTimeoutMultiplier = 20;
			ctmoNew.WriteTotalTimeoutConstant = 250;
			if (!SetCommTimeouts(pImpl->fp, &ctmoNew))
			{
				// We need timeouts.
				CloseHandle(pImpl->fp);
				return;
			}

			//Get DCB and set appropriate values, store old DCB for when program exits
			DCB stDeviceControl = { 0 };
			stDeviceControl.DCBlength = sizeof(stDeviceControl);

			if (GetCommState(pImpl->fp, &stDeviceControl))
			{
                if (cs_rs422) {
                    BYTE ByteSize, Parity, StopBits;
                    unsigned char d, p, s;
                    switch(cs_rs422->Parity()) {
                        case CS_RS422::ParitySetting::NONE: Parity = NOPARITY; p = 'N'; break;
                        case CS_RS422::ParitySetting::ODD: Parity = ODDPARITY; p = 'O'; break;
                        case CS_RS422::ParitySetting::EVEN: Parity = EVENPARITY; p = 'E'; break;
                        default: Parity = NOPARITY; break;
                    }
                    switch(cs_rs422->DataBits()) {
                        case CS_RS422::DataBitsSetting::BITS_7: ByteSize = 7; d = '7'; break;
                        case CS_RS422::DataBitsSetting::BITS_8: ByteSize = 8; d = '8'; break;
                        default: ByteSize = 8; break;
                    }
                    switch(cs_rs422->StopBits()) {
                        case CS_RS422::StopBitsSetting::BITS_1: StopBits = ONESTOPBIT; s = '1'; break;
                        case CS_RS422::StopBitsSetting::BITS_2: StopBits = TWOSTOPBITS; s = '2'; break;
                        default: Parity = ONESTOPBIT; break;
                    }

                    BOOST_LOG_SEV(lg::get(), sev::debug) << "Attempting to connect to " << serial << " on port " << portname << " using custom connection settings (" << cs_rs422->BaudRate() << " " << d << p << s << ")" << std::endl;
                    stDeviceControl.BaudRate = cs_rs422->BaudRate();
                    stDeviceControl.Parity = Parity;
                    stDeviceControl.ByteSize = ByteSize;
                    stDeviceControl.StopBits = StopBits;
                } else {
                    BOOST_LOG_SEV(lg::get(), sev::debug) << "Attempting to connect to " << serial << " on port " << portname << " using default connection settings (115200 8N1)" << std::endl;
                    stDeviceControl.BaudRate = CBR_115200;
                    stDeviceControl.Parity = NOPARITY;
                    stDeviceControl.ByteSize = 8;
                    stDeviceControl.StopBits = ONESTOPBIT;
                }
			}
			else {
				CloseHandle(pImpl->fp);
				return;
			}

			if (!SetCommState(pImpl->fp, &stDeviceControl))
			{
				// We cannot set the serial port parameters, so close it
				CloseHandle(pImpl->fp);
				return;
			}

			DeviceIsOpen = true;

			// Clear Message Lists
			m_msgRegistry.clear();
			while (!m_queue.empty()) m_queue.pop();

            {
                std::lock_guard<std::mutex> lock(m_rxmutex);
                m_rxCharQueue.clear();
            }

			// Start Report Sending Thread
			senderThread = std::thread(&CM_RS422::MessageSender, this);

			// Start Response Receiver Thread
			receiverThread = std::thread(&CM_RS422::ResponseReceiver, this);

			// Start Response Parser Thread
			parserThread = std::thread(&CM_RS422::MessageListManager, this);

			// Start Memory Transferer Thread
			memoryTransferThread = std::thread(&CM_Common::MemoryTransfer, this);

			// Start Interrupt receiving thread
			pImpl->interruptThread = std::thread(&CM_RS422::InterruptReceiver, this);
		}
	}

	void CM_RS422::Disconnect()
	{
		if (DeviceIsOpen)
		{
			if (pImpl->fp == NULL) {
				SignalClosed();
                SetEvent(pImpl->hShutdown);
				return;
			}

            // True if we ever received a valid response from the device
            if (m_status == _ConnectionStatus::ALIVE) {
                // Disable Interrupts
                HostReport *iorpt;
                iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
                iorpt->Payload<int>(0);
                ReportFields f = iorpt->Fields();
                f.len = sizeof(int);
                iorpt->Fields(f);
                this->SendMsg(*iorpt);
                delete iorpt;
            }

			bool msg_pending{ true };
			while (msg_pending)
			{
				std::unique_lock <std::mutex> txlck{ m_txmutex };
				msg_pending = !m_queue.empty(); // wait for all messages to be sent
				txlck.unlock();
			}

			// wait for all messages to have been processed
			bool msg_waiting{ false };
			do
			{
                m_msgRegistry.forEachMessage([&](const std::shared_ptr<Message>& msg) {
                    if (!msg->isComplete())
					{
						msg_waiting = true;
					}
                });
				if (msg_waiting)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(25));
					msg_waiting = false;
				}
				else {
					break;
				}
			} while (1);

			// Stop Threads
			SignalClosed();  // must set this to cancel threads
            SetEvent(pImpl->hShutdown);

			senderThread.join();
			receiverThread.join();
			parserThread.join();
			memoryTransferThread.join();  // TODO: need to abort in-flight transfers
			pImpl->interruptThread.join();

			if (pImpl->fp != NULL)
			{
				CloseHandle(pImpl->fp);
				pImpl->fp = NULL;
			}
		}

	}

	void CM_RS422::SetTimeouts(int send_timeout_ms, int rx_timeout_ms, int free_timeout_ms, int discover_timeout_ms)
	{
		(void)discover_timeout_ms;
		sendTimeout = std::chrono::milliseconds(send_timeout_ms);
		rxTimeout = std::chrono::milliseconds(rx_timeout_ms);
		autoFreeTimeout = std::chrono::milliseconds(free_timeout_ms);
	}

	void CM_RS422::MessageSender()
	{
		while (DeviceIsOpen)
		{
            std::shared_ptr<Message> m;
            {
                std::unique_lock<std::mutex> lck{ m_txmutex };
                m_txcond.wait(lck, [&] {return !DeviceIsOpen || !m_queue.empty(); });

                // Allow thread to terminate or to process any notifications
                if (!DeviceIsOpen) break;

                m = m_queue.front();
                m_queue.pop();  // delete from queue
            }

            // Get HostReport bytes and send to device
            DWORD totalBytesWritten = 0;
            DWORD dwRes;
            std::vector<uint8_t> b = m->SerialStream();

            OVERLAPPED osWrite = { 0 };
            osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (!osWrite.hEvent) continue;
            
            auto tm_start = HRClock::now();
            while (totalBytesWritten < b.size() && (m->getStatus() != Message::Status::SEND_ERROR))
            {
                if ((std::chrono::duration_cast<std::chrono::milliseconds>(HRClock::now() - tm_start)) > sendTimeout)
                {
                    // Do something with timeout
                    m->setStatus(Message::Status::TIMEOUT_ON_SEND);
                    mMsgEvent.Trigger<int>(this, MessageEvents::TIMED_OUT_ON_SEND, m->getMessageHandle());  // Notify listeners

                    break;
                }

                // Timeout allows thread to cancel message
                std::unique_lock<std::timed_mutex> wrlck(pImpl->m_rdwrmutex, std::chrono::milliseconds(10));
                if (!wrlck.owns_lock()) {
                    continue;
                }

                DWORD bytesWritten = 0;
                if (!WriteFile(pImpl->fp, &b[totalBytesWritten], (DWORD)(b.size() - totalBytesWritten), &bytesWritten, &osWrite)) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        dwRes = WaitForSingleObject(osWrite.hEvent, INFINITE);
                        switch (dwRes)
                        {
                            // Event occurred.
                        case WAIT_OBJECT_0: {
                            if (!GetOverlappedResult(pImpl->fp, &osWrite, &bytesWritten, FALSE)) {
                                m->setStatus(Message::Status::SEND_ERROR);
                            }
                            break;
                        }
                        case WAIT_TIMEOUT: {	
                            break;
                        }
                        case WAIT_FAILED: 
                        default: {
                            m->setStatus(Message::Status::SEND_ERROR);
                            break;
                        }
                        }
                    }
                    else {
                        // WriteFile failed, but it isn't delayed. Report error and abort.
                        m->setStatus(Message::Status::SEND_ERROR);
                    }
                }

                // WriteFile completed immediately.
                totalBytesWritten += bytesWritten;
           
                if (!bytesWritten)
                {
                    m->setStatus(Message::Status::SEND_ERROR);
                }

            } 
            CloseHandle(osWrite.hEvent);

            if (m->getStatus() == Message::Status::UNSENT) {
                m->setStatus(Message::Status::SENT);
            } else if (m->getStatus() == Message::Status::SEND_ERROR) {
                mMsgEvent.Trigger<int>(this, MessageEvents::SEND_ERROR, m->getMessageHandle());
            }
		}
	}

#define USE_WAITCOMM_EVENT (0)

	void CM_RS422::ResponseReceiver()
	{
		unsigned char  chRead[Impl::RxBufferSize];
        OVERLAPPED osReader = { 0 };
        osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        HANDLE waitHandles[2] = { osReader.hEvent, pImpl->hShutdown };

#if USE_WAITCOMM_EVENT
        // Configure event mask so we get notified when chars arrive
        if (!SetCommMask(pImpl->fp, EV_RXCHAR)) {
            BOOST_LOG_SEV(lg::get(), sev::error) << "SetCommMask failed";
            CloseHandle(osReader.hEvent);
            return;
        }
#endif

        while (DeviceIsOpen)
        {
    		DWORD bytesRead = 0;
            ResetEvent(osReader.hEvent);

#if USE_WAITCOMM_EVENT
            //----------------------------------------------------
            // MODE A: WaitCommEvent signals us when data arrives
            //----------------------------------------------------
            DWORD evtMask = 0;
            BOOL ok = WaitCommEvent(pImpl->fp, &evtMask, &osReader);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                // Wait until event signalled
                if (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE) != WAIT_OBJECT_0) continue;

//                if (WaitForSingleObject(osReader.hEvent, INFINITE) != WAIT_OBJECT_0) continue;
                if (!GetOverlappedResult(pImpl->fp, &osReader, &evtMask, FALSE)) continue;
            }

            if (evtMask & EV_RXCHAR) {
                std::unique_lock<std::timed_mutex> rdlck(pImpl->m_rdwrmutex, std::chrono::milliseconds(10));
                if (!rdlck.owns_lock()) continue;

                // Now read what's available
                ReadFile(pImpl->fp, chRead, sizeof(chRead), &bytesRead, NULL);
            }

#else            
            //----------------------------------------------------
            // MODE B: Direct overlapped ReadFile
            //----------------------------------------------------
            {
                // Timeout allows thread to terminate
                std::unique_lock<std::timed_mutex> rdlck(pImpl->m_rdwrmutex, std::chrono::milliseconds(10));
                if (!rdlck.owns_lock()) continue;

                BOOL ok = ReadFile(pImpl->fp, chRead, sizeof(chRead), &bytesRead, &osReader);
                if (!ok) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        // Wait for read completion
                        rdlck.unlock();  // Allow MessageSender to write while we wait
                        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
                        rdlck.lock();
            
                        if (waitResult != WAIT_OBJECT_0) continue;

                        if (!GetOverlappedResult(pImpl->fp, &osReader, &bytesRead, FALSE)) continue;
                    } else {
                        continue;
                    }
                }
            }
#endif
            
            if (bytesRead > 0)
            {
                {
                    std::scoped_lock lock(m_rxmutex);
                    m_rxCharQueue.insert(m_rxCharQueue.end(), chRead, chRead + bytesRead);
                    // std::unique_lock<std::mutex> rxlck{ m_rxmutex };
                    // for (DWORD i = 0; i < bytesRead; i++) {
                    //     m_rxCharQueue.push(chRead[i]);
                    // }
                }
                m_rxcond.notify_one();

                // // Logging
                // std::stringstream ss;
                // for (DWORD i = 0; i < bytesRead; i++)
                //     ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(chRead[i]) << " ";
                // BOOST_LOG_SEV(lg::get(), sev::trace) << "RECD: " << ss.str();
            }
        }
        CloseHandle(osReader.hEvent);		
	}

	void CM_RS422::InterruptReceiver()
	{

	}

}

#endif
