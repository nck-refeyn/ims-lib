/*-----------------------------------------------------------------------------
/ Title      : Connection Manager Implementation for FTDI Connections
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ConnectionManager/src/CM_FTDI.cpp $
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

#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <atomic>

#include "CM_FTDI.h"
#include "IMSSystem.h"
#include "PrivateUtil.h"

// Workaround because FTDI Driver is not compatible with VC14.0 (2015)
#if (_MSC_VER >= 1900)
extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }
#endif

#if defined _WIN32 && defined _DEBUG
#include "crtdbg.h"
#define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

#if defined(_WIN32)

namespace iMS {

	class CM_FTDI::Impl
	{
	public:
		Impl(){}

		// Only accept FTDI Device number with Serial Numbers prefixed with these strings
		static const std::list<std::string> SerialNumberPrefix;

		const std::string Ident = "CM_USBLITE";

		FT_HANDLE ftdiDevice;
		std::vector<std::shared_ptr<IMSSystem>> ListFtUsbDevices();

		// Event used to receive notification from FTDI Device
#if defined(_WIN32)
		HANDLE hEvent;
        HANDLE hShutdown;
#elif defined(__linux__)
        struct FT_Event {
            pthread_cond_t eCondVar;
            pthread_mutex_t eMutex;
        };
        typedef FT_Event* FT_Event_Handle;
		FT_Event_Handle eh;
#endif
		// Interrupt receiving thread
		std::thread interruptThread;
		std::shared_ptr<std::vector<uint8_t>> interruptData;

        void SetOwner(std::shared_ptr<IConnectionManager> mgr) {m_parent = mgr;}
	private:
		std::shared_ptr<IConnectionManager> m_parent;
	};

	const std::list<std::string> CM_FTDI::Impl::SerialNumberPrefix = std::list<std::string>({ "iMS", "iDDS", "iCSA", "iVCS" });

	// Default Constructor
	CM_FTDI::CM_FTDI() : pImpl(new CM_FTDI::Impl())
	{
		sendTimeout = std::chrono::milliseconds(100);
		rxTimeout = std::chrono::milliseconds(500);
		autoFreeTimeout = std::chrono::milliseconds(10000);
        connSettings = nullptr;
	}
	 
	CM_FTDI::~CM_FTDI()
	{
		// v1.0.1 Clean up threads if destroyed unexpectedly
		if (DeviceIsOpen) this->Disconnect();

		delete pImpl;
		pImpl = nullptr;
	}

    std::shared_ptr<IConnectionManager> CM_FTDI::Create() {
        auto instance = std::shared_ptr<CM_FTDI>(new CM_FTDI());
        instance->pImpl->SetOwner(instance);  // safe now
        return instance;
    }

	const std::string& CM_FTDI::Ident() const
	{
		return pImpl->Ident;
	}

	std::vector<std::shared_ptr<IMSSystem>> CM_FTDI::Impl::ListFtUsbDevices()
	{
		FT_STATUS ftStatus = 0;
		DWORD numOfDevices = 0;

		std::vector<std::shared_ptr<IMSSystem>> IMSList;

		// Get number of FTDI Devices attached to system
		ftStatus = FT_CreateDeviceInfoList(&numOfDevices);

		for (DWORD iDev = 0; iDev < numOfDevices; ++iDev)
		{
			FT_DEVICE_LIST_INFO_NODE devInfo;
			memset(&devInfo, 0, sizeof(devInfo));

			ftStatus = FT_GetDeviceInfoDetail(iDev, &devInfo.Flags, &devInfo.Type, &devInfo.ID, &devInfo.LocId,
				devInfo.SerialNumber,
				devInfo.Description,
				&devInfo.ftHandle);

			if (FT_OK == ftStatus)
			{
				const std::string serial = devInfo.SerialNumber;
                BOOST_LOG_SEV(lg::get(), sev::trace) << "FTDI Serial Number: " << serial;
				const int flags = devInfo.Flags;  // 0 = closed, 1 = open

				// Check to see if Serial Number begins with one of the approved strings and device hasn't been opened by another process
				bool prefix_match = false;
				for (auto& prefix : SerialNumberPrefix) {
					if ((prefix.length() <= serial.length()) &&
						std::equal(prefix.begin(), prefix.end(), serial.begin()))
						prefix_match = true;
				}
				if (prefix_match &&	!(flags & 0x01))
				{
					// Then open device and send a Magic Number query message
					//ftStatus = FT_OpenEx(devInfo.SerialNumber, FT_OPEN_BY_SERIAL_NUMBER, &ftdiDevice);
					m_parent->Connect(serial);

					if (m_parent->Open())
					{
						// Found a suitable connection interface, let's query its contents.
						// An FTDI based board can have a maximum of one controller and one synthesiser.
						auto thisiMS = IMSSystem::Create (m_parent, devInfo.SerialNumber);

						thisiMS->Initialise();

						if (thisiMS->Ctlr().IsValid() || thisiMS->Synth().IsValid()) {
							IMSList.push_back(thisiMS);
						}
					}

					//FT_Close(ftdiDevice);
					m_parent->Disconnect();
				}
			}
		}
		return IMSList;
	}

	std::vector<std::shared_ptr<IMSSystem>> CM_FTDI::Discover(const ListBase<std::string>& PortMask, std::shared_ptr<IConnectionSettings> settings)
	{
//		std::cout << "CM_FTDI::Discover()" << std::endl;
        connSettings = settings;
		return pImpl->ListFtUsbDevices();
	}

	void CM_FTDI::Connect(const std::string& serial)
	{
		if (!DeviceIsOpen)
		{
			FT_STATUS ftStatus;

			// Open Device
			ftStatus = FT_OpenEx((PVOID)serial.c_str(), FT_OPEN_BY_SERIAL_NUMBER, &pImpl->ftdiDevice);
			if (ftStatus != FT_OK) {
				// FT_Open failed
				return;
			}
			DeviceIsOpen = true;

			ftStatus = FT_Purge(pImpl->ftdiDevice, FT_PURGE_RX | FT_PURGE_TX); // Purge both Rx and Tx buffers

            FT_SetLatencyTimer(pImpl->ftdiDevice, 1); // in ms

			// Assign Event Handle to receive notifications from device
			DWORD EventMask = FT_EVENT_RXCHAR;
#if defined(_WIN32)
			pImpl->hEvent = CreateEvent(
				NULL,
				false, // auto-reset event
				false, // non-signalled state
				NULL
				);
			ftStatus = FT_SetEventNotification(pImpl->ftdiDevice, EventMask, (PVOID)pImpl->hEvent);

            // Used to unblock the waiting threads
            pImpl->hShutdown = CreateEvent(NULL, TRUE, FALSE, NULL);
#elif defined(__linux__)
            // allocate and initialise POSIX event object
            pImpl->eh = new FT_Event;
            if (pthread_mutex_init(&pImpl->eh->eMutex, NULL) != 0) {
                // handle error...
            }
            if (pthread_cond_init(&pImpl->eh->eCondVar, NULL) != 0) {
                // handle error...
            }

            ftStatus = FT_SetEventNotification(pImpl->ftdiDevice, EventMask, (PVOID)pImpl->eh);
            // FT_SetEventNotification returns immediately.  When RXCHAR arrives, D2XX should signal the condvar.
#endif
			// Clear Message Lists
			m_msgRegistry.clear();
			while (!m_queue.empty()) m_queue.pop();

            {
                std::lock_guard<std::mutex> lock(m_rxmutex);
                m_rxCharQueue.clear();
            }

			// Start Report Sending Thread
			senderThread = std::thread(&CM_FTDI::MessageSender, this);

			// Start Response Receiver Thread
			receiverThread = std::thread(&CM_FTDI::ResponseReceiver, this);

			// Start Response Parser Thread
			parserThread = std::thread(&CM_FTDI::MessageListManager, this);

			// Start Memory Transferer Thread
			memoryTransferThread = std::thread(&CM_Common::MemoryTransfer, this);

			// Start Interrupt receiving thread
			pImpl->interruptThread = std::thread(&CM_FTDI::InterruptReceiver, this);
		}
	}

	void CM_FTDI::Disconnect()
	{
		if (DeviceIsOpen)
		{
				// Disable Interrupts
			HostReport *iorpt;
			iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
			iorpt->Payload<int>(0);
			ReportFields f = iorpt->Fields();
			f.len = sizeof(int);
			iorpt->Fields(f);
			this->SendMsg(*iorpt);
			delete iorpt;

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
#if defined(_WIN32)
            SetEvent(pImpl->hShutdown);
#elif defined(__linux__)
            if (pImpl->eh) {
                // Wake the waiting ResponseReceiver loop
                pthread_mutex_lock(&pImpl->eh->eMutex);
                pthread_cond_broadcast(&pImpl->eh->eCondVar);
                pthread_mutex_unlock(&pImpl->eh->eMutex);
            }
#endif

			senderThread.join();
			receiverThread.join();
			parserThread.join();
			memoryTransferThread.join();  // TODO: need to abort in-flight transfers
			pImpl->interruptThread.join();

			FT_Close(pImpl->ftdiDevice);

			// Clear Up
#if defined(__linux__)
            if (pImpl->eh) {
                pthread_cond_destroy(&pImpl->eh->eCondVar);
                pthread_mutex_destroy(&pImpl->eh->eMutex);
                delete pImpl->eh;
                pImpl->eh = nullptr;
            }
#endif
		}
	}

	void CM_FTDI::SetTimeouts(int send_timeout_ms, int rx_timeout_ms, int free_timeout_ms, int discover_timeout_ms)
	{
		sendTimeout = std::chrono::milliseconds(send_timeout_ms);
		rxTimeout = std::chrono::milliseconds(rx_timeout_ms);
		autoFreeTimeout = std::chrono::milliseconds(free_timeout_ms);
	}

	// Message sending thread waits until a new message has been added to the queue
	// then sends the report data to the FTDI device
	void CM_FTDI::MessageSender()
	{
		while (DeviceIsOpen == true)
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
            DWORD numBytesWritten = 0, totalBytesWritten = 0;
            const std::vector<uint8_t>& b = m->SerialStream();
            FT_STATUS ftStatus;
            //			ftStatus = FT_Purge(ftdiDevice, FT_PURGE_RX | FT_PURGE_TX); // Purge both Rx and Tx buffers

            std::chrono::time_point<std::chrono::high_resolution_clock> tm_start = std::chrono::high_resolution_clock::now();
            do
            {
                if ((std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - tm_start)) > sendTimeout)
                {
                    // Do something with timeout
                    m->setStatus(Message::Status::TIMEOUT_ON_SEND);
                    mMsgEvent.Trigger<int>(this, MessageEvents::TIMED_OUT_ON_SEND, m->getMessageHandle());  // Notify listeners

                    // If there is anything connected still, we need to flush through the buffer
                    int i = 73; // max msg size
                    const char c = 0;
                    do {
                        ftStatus = FT_Write(pImpl->ftdiDevice, (LPVOID)&c, 1, (LPDWORD)&numBytesWritten);
                        i -= numBytesWritten;
                    } while (!i);
                    break;
                }

                ftStatus = FT_Write(pImpl->ftdiDevice, (LPVOID)&b[totalBytesWritten], (static_cast<DWORD>(b.size()) - totalBytesWritten), (LPDWORD)&numBytesWritten);
                totalBytesWritten += numBytesWritten;
                if (ftStatus != FT_OK)
                {
                    // Do something with FTDI failure
                    m->setStatus(Message::Status::SEND_ERROR);
                    mMsgEvent.Trigger<int>(this, MessageEvents::SEND_ERROR, m->getMessageHandle());
                    break;
                }
            } while (totalBytesWritten < b.size());

            if (m->getStatus() == Message::Status::UNSENT) {
                m->setStatus(Message::Status::SENT);
            }
		}
	}

	void CM_FTDI::ResponseReceiver()
	{
        unsigned char RxBuffer[4096]; // same size as on-chip buffer
#if defined(_WIN32)
        HANDLE waitHandles[2] = { pImpl->hEvent, pImpl->hShutdown };

        while (DeviceIsOpen)
		{
			// Wait for something to happen
            DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (!DeviceIsOpen) break;  // check shutdown

            if (waitResult == WAIT_OBJECT_0) {
                DWORD BytesAvailable = 0, eventStatus, txBytes;

			    FT_STATUS status = FT_GetStatus(pImpl->ftdiDevice, &BytesAvailable, &txBytes, &eventStatus);
                if (status != FT_OK || BytesAvailable == 0) continue;

                DWORD BytesRead = 0;
                if (BytesAvailable > sizeof(RxBuffer)) BytesAvailable = sizeof(RxBuffer);

                if (FT_Read(pImpl->ftdiDevice, RxBuffer, BytesAvailable, &BytesRead) == FT_OK && BytesRead > 0) {
                    // Logging
                    std::stringstream ss;
                    for (DWORD i = 0; i < BytesRead; i++)
                        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(RxBuffer[i]) << " ";
//                    BOOST_LOG_SEV(lg::get(), sev::trace) << "RECD: " << ss.str();

                    {
                        std::scoped_lock lock(m_rxmutex);
                        m_rxCharQueue.insert(m_rxCharQueue.end(), RxBuffer, RxBuffer + BytesRead);
                    }
                    m_rxcond.notify_one();
                }
            }            
		}
#elif defined(__linux__)

        // Linux: wait on the FT_Event condvar which FTDI will signal
        FT_Event *eh = pImpl->eh;

        while (DeviceIsOpen) {
            // lock and wait until we are signalled or shutdown
            pthread_mutex_lock(&eh->eMutex);

            // Wait (unblocked either by FTDI signalling or by Disconnect() calling pthread_cond_broadcast)
            pthread_cond_wait(&eh->eCondVar, &eh->eMutex);

            // If DeviceIsOpen was cleared during shutdown and Disconnect() called broadcast, exit loop.
            if (!DeviceIsOpen) {
                pthread_mutex_unlock(&eh->eMutex);
                break;
            }

            pthread_mutex_unlock(&eh->eMutex);

            // Now check FTDI status and read any available bytes
            DWORD BytesAvailable = 0, eventStatus = 0, txBytes = 0;
            FT_STATUS status = FT_GetStatus(pImpl->ftdiDevice, &BytesAvailable, &txBytes, &eventStatus);
            if (status != FT_OK) {
                // handle error (optionally break)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (BytesAvailable == 0) {
                // nothing to read — go back to waiting
                continue;
            }

            DWORD BytesRead = 0;
            if (BytesAvailable > sizeof(RxBuffer)) BytesAvailable = sizeof(RxBuffer);

            if (FT_Read(pImpl->ftdiDevice, RxBuffer, BytesAvailable, &BytesRead) == FT_OK && BytesRead > 0) {
                {
                    std::scoped_lock lock(m_rxmutex);
                    m_rxCharQueue.insert(m_rxCharQueue.end(), RxBuffer, RxBuffer + BytesRead);
                }
                m_rxcond.notify_one();
            }
        } // while(DeviceIsOpen)

#endif
	}

	void CM_FTDI::InterruptReceiver()
	{

	}
}

#endif
