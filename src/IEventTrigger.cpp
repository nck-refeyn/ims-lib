/*-----------------------------------------------------------------------------
/ Title      : Event Trigger Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/EventManager/src/IEventTrigger.cpp $
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

#include "IEventTrigger.h"
#include "IEventHandler.h"
#include <iostream>
#include <mutex>

namespace iMS {

	IEventTrigger::IEventTrigger()
	{
		// winpthreads (MinGW) initialises std::shared_mutex lazily and fails if two threads make
		// the first use concurrently; use it once here, before other threads can see the object
		std::unique_lock maplck{ m_mapMutex };
	}

	IEventTrigger::~IEventTrigger()
	{
		std::unique_lock maplck{ m_mapMutex };
		for (int i = 0; i < messageCount; i++)
		{
			mMap[i]->clear();
			delete mMap[i];
		}
	}

	void IEventTrigger::Subscribe(const int message, IEventHandler* handler)
	{
		std::unique_lock maplck{ m_mapMutex };
		mMap[message]->push_back(handler);
		maplck.unlock();
	}

	void IEventTrigger::Unsubscribe(const int message, const IEventHandler* handler)
	{
		for (int i = 0; i < messageCount; i++) {
			if (message == i)
			{
				std::unique_lock maplck{ m_mapMutex };
				EventHandlerList* list = mMap[i];
				for (std::vector<IEventHandler*>::iterator iter = list->begin(); iter != list->end();)
				{
					if (**iter == *handler)
					{
						iter = list->erase(iter);
					}
					else {
						++iter;
					}
				}
			}
		}
	}

	int IEventTrigger::Subscribers(const int message)
	{
		std::size_t count;
		{
			std::shared_lock maplck{ m_mapMutex };
			count = mMap.count(message);
		}
		return (count);
	}

	template<typename T>
	void IEventTrigger::Trigger(void* sender, const int message, const T param)
	{
		for (int i = 0; i < messageCount; i++) {
			if (message == i)
			{
				std::shared_lock maplck{ m_mapMutex };
				EventHandlerList* list = mMap[i];
				for (EventHandlerList::iterator iter = list->begin(); iter != list->end(); ++iter)
				{
					//					std::cout << std::hex << "0x" << iter._Ptr << ": " << std::dec << message << " - " << param << "   " << std::endl;
					(*iter)->EventAction(sender, message, param);
				}
			}
		}
	}

	template<typename T, typename T2>
	void IEventTrigger::Trigger(void* sender, const int message, const T param, const T2 param2)
	{
		for (int i = 0; i < messageCount; i++) {
			if (message == i)
			{
				std::shared_lock maplck{ m_mapMutex };
				EventHandlerList * list = mMap[i];
				for (EventHandlerList::iterator iter = list->begin(); iter != list->end(); ++iter)
				{
					(*iter)->EventAction(sender, message, param, param2);
				}
				maplck.unlock();
			}
		}
	}

	void IEventTrigger::updateCount(int count)
	{
		messageCount = count;
		for (int i = 0; i < messageCount; i++)
		{
			mMap[i] = new EventHandlerList();
		}
	}

	template void IEventTrigger::Trigger<int>(void* sender, const int message, const int param);
	template void IEventTrigger::Trigger<int, int>(void* sender, const int message, const int param, const int param2);
	template void IEventTrigger::Trigger<double>(void* sender, const int message, const double param);
	template void IEventTrigger::Trigger<int, std::vector<std::uint8_t>>(void* sender, const int message, const int param, const std::vector<std::uint8_t> data);

}
