/*-----------------------------------------------------------------------------
/ Title      : Image Operations Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ImageOps/src/ImageOps.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2025-01-08 21:34:12 +0000 (Wed, 08 Jan 2025) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 655 $
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

#include "ImageOps.h"
#include "Image_p.h"
#include "BulkVerifier.h"
#include "IConnectionManager.h"
#include "IEventTrigger.h"
#include "IMSTypeDefs_p.h"
#include "IMSConstants.h"
#include "ToneBuffer.h"
#include "PrivateUtil.h"

#include <algorithm>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <iomanip>
//#include <iostream>

namespace iMS
{
//	class VerifyListener : public IEventHandler
//	{
//	public:

//	};

	// Free function for formatting Image objects into bytestreams
	int FormatImage(const Image& img, std::shared_ptr<IMSSystem> ims, boost::container::deque < std::uint8_t >& img_data, ImageFormat formatSpec, int MSBFirst)
	{
		// These are the old resolutions that must be used if the iMS is in LSB first mode.  Newer firmware supports MSB first mode with variable resolution
		const int lsb_freqbits = 16;
		const int lsb_amplbits = 8;
		const int lsb_phasebits = 12;
		const int lsb_syncdbits = 12;
		const int lsb_syncabits = 12;

		int img_index = 0;
		int length = img.Size();
		int bytesPerPoint = 0;
		int sync_anlg_channels = formatSpec.SyncAnlgChannels();
		if (sync_anlg_channels > 2) sync_anlg_channels = 2;
		// Restrict to maximum size of Controller memory;
		//length = std::min(length, ims->Ctlr().GetCap().MaxImageSize);

		Image::const_iterator it = img.cbegin();
		while ((img_index < length) && (it < img.cend()))
		{
			ImagePoint pt = (*it);

			int chan = RFChannel::min;
			while (chan <= formatSpec.Channels()) {
				FAP fap = pt.GetFAP(chan);

				std::uint32_t freq = FrequencyRenderer::RenderAsImagePoint(ims, fap.freq);
				std::uint32_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, fap.ampl);
				std::uint32_t phase = PhaseRenderer::RenderAsImagePoint(ims, fap.phase);

				if (!MSBFirst) {
					for (int i = (ims->Synth().GetCap().freqBits - lsb_freqbits); i <= (ims->Synth().GetCap().freqBits - 1); i+=8) {
						img_data.push_back(static_cast<std::uint8_t>((freq >> i) & 0xFF));
						if (!img_index) bytesPerPoint++;
					}
					for (int i = (ims->Synth().GetCap().amplBits - lsb_amplbits); i <= (ims->Synth().GetCap().amplBits - 1); i+=8) {
						img_data.push_back(static_cast<std::uint8_t>((ampl >> i) & 0xFF));
						if (!img_index) bytesPerPoint++;
					}
					for (int i = (ims->Synth().GetCap().phaseBits - lsb_phasebits); i <= (ims->Synth().GetCap().phaseBits - 1); i+=8) {
						img_data.push_back(static_cast<std::uint8_t>((phase >> i) & 0xFF));
						if (!img_index) bytesPerPoint++;
					}

					chan++;
				}
				else {
					// Add FAP
					int freqEndBit = ims->Synth().GetCap().freqBits - formatSpec.FreqBytes() * 8;
					if (freqEndBit < -8) freqEndBit = -8;

					int amplEndBit = ims->Synth().GetCap().amplBits - formatSpec.AmplBytes() * 8;
					if (amplEndBit < -8) amplEndBit = -8;

					int phsEndBit = ims->Synth().GetCap().phaseBits - formatSpec.PhaseBytes() * 8;
					if (phsEndBit < -8) phsEndBit = -8;

					for (int i = ims->Synth().GetCap().freqBits - 8; i >= freqEndBit; i-=8) {
						if (i < 0) {
							img_data.push_back(static_cast<std::uint8_t>((freq << -i) & 0xFF));  // Accounts for zero filling bottom byte for non multiple-of-8 bit sizes
						}
						else {
							img_data.push_back(static_cast<std::uint8_t>((freq >> i) & 0xFF));
						}
						if (!img_index) bytesPerPoint++;
					}
					if (formatSpec.EnableAmpl()) {
						for (int i = ims->Synth().GetCap().amplBits - 8; i >= amplEndBit; i-=8) {
							if (i < 0) {
								img_data.push_back(static_cast<std::uint8_t>((ampl << -i) & 0xFF));
							}
							else {
								img_data.push_back(static_cast<std::uint8_t>((ampl >> i) & 0xFF));
							}
							if (!img_index) bytesPerPoint++;
						}
					}
					if (formatSpec.EnablePhase()) {
						for (int i = ims->Synth().GetCap().phaseBits - 8; i >= phsEndBit; i-=8) {
							if (i < 0) {
								img_data.push_back(static_cast<std::uint8_t>((phase << -i) & 0xFF));
							}
							else {
								img_data.push_back(static_cast<std::uint8_t>((phase >> i) & 0xFF));
							}
							if (!img_index) bytesPerPoint++;
						}
					}

					// Set Next Channel
					if (formatSpec.CombineAllChannels()) {
						chan += formatSpec.Channels();
					}
					else if (formatSpec.CombineChannelPairs()) { 
						chan += 2;
					}
					else {
						chan++;
					}
				}
			}

			float synca[2] = { pt.GetSyncA(0), pt.GetSyncA(1) };
			unsigned int syncd = pt.GetSyncD();
			std::uint32_t syncd_mod = syncd & ((1 << ims->Synth().GetCap().LUTSyncDBits) - 1);

			if (!MSBFirst) {
				for (int i = (ims->Synth().GetCap().LUTSyncDBits - lsb_syncdbits); i <= (ims->Synth().GetCap().LUTSyncDBits - 1); i += 8) {
					img_data.push_back(static_cast<std::uint8_t>((syncd_mod >> i) & 0xFF));
					if (!img_index) bytesPerPoint++;
				}

				for (int j = 0; j < 2; j++) {
					std::uint32_t synca_int = (std::uint32_t)((1 << ims->Synth().GetCap().LUTSyncABits) * synca[j]);
					for (int i = (ims->Synth().GetCap().LUTSyncABits - lsb_syncabits); i <= (ims->Synth().GetCap().LUTSyncABits - 1); i += 8) {
						img_data.push_back(static_cast<std::uint8_t>((synca_int >> i) & 0xFF));
						if (!img_index) bytesPerPoint++;
					}
				}
			}
			else {
				// Add Sync
				int syncDEndBit = ims->Synth().GetCap().LUTSyncDBits - formatSpec.SyncBytes() * 8;
				if (syncDEndBit < -8) syncDEndBit = -8;

				int syncAEndBit = ims->Synth().GetCap().LUTSyncABits - formatSpec.SyncBytes() * 8;
				if (syncAEndBit < -8) syncAEndBit = -8;

				if (formatSpec.EnableSyncDig()) {
					for (int i = ims->Synth().GetCap().LUTSyncDBits - 8; i >= syncDEndBit; i -= 8) {
						if (i < 0) {
							img_data.push_back(static_cast<std::uint8_t>((syncd_mod << -i) & 0xFF));
						}
						else {
							img_data.push_back(static_cast<std::uint8_t>((syncd_mod >> i) & 0xFF));
						}
						if (!img_index) bytesPerPoint++;
					}
				}

				for (int j = 0; j < sync_anlg_channels; j++) {
					std::uint32_t synca_int = (std::uint32_t)((1 << ims->Synth().GetCap().LUTSyncABits) * synca[j]);
					for (int i = ims->Synth().GetCap().LUTSyncABits - 8; i >= syncAEndBit; i -= 8) {
						if (i < 0) {
							img_data.push_back(static_cast<std::uint8_t>((synca_int << -i) & 0xFF));
						}
						else {
							img_data.push_back(static_cast<std::uint8_t>((synca_int >> i) & 0xFF));
						}
						if (!img_index) bytesPerPoint++;
					}
				}
			}

			if ((++it == img.cend()) || (++img_index == length))
			{
				break;
			}
		}
		return bytesPerPoint;
	}

	std::uint8_t FormatSequenceEntry(const std::shared_ptr<SequenceEntry>& seq_entry, std::shared_ptr<IMSSystem> ims, std::vector < std::uint8_t >& seq_data)
	{
		/* Add Entries */
		/* v1 Payload
		* [15:0] = Image UUID Reference
		* [16] = number of repeats
		* [18:17] = synchronous digital output delay
		* [22:19] = internal clock rate
		* [24:23] = external clock divider
		* [28:25] = post image delay
		*
		* v2 Payload: Image (context = 6)
		* [15:0] = Image UUID Reference
		* [16] = number of synth programming registers
		* [18:17] = synchronous digital output delay
		* [22:19] = internal clock rate
		* [24:23] = external clock divider
		* [28:25] = post image delay
		* [31:29] = number of repeats
		* [35:32] = synth prog reg 1
		* [39:36] = synth prog reg 2
		* ...
		* [63:60] = synth prog reg 7
		*
		* v2 Payload: Tone (context = 7)
		* [15:0] = Tone Buffer UUID Reference
		* [16] = number of synth programming registers
		* [18:17] = synchronous digital output delay
		* [19] = initial index to output from LTB
		* [31:20] = N/A
		* [35:32] = synth prog reg 1
		* [39:36] = synth prog reg 2
		* ...
		* [63:60] = synth prog reg 7
		*/
		std::uint8_t type;
		bool adv_seq = (ims->Ctlr().GetVersion().major >= 2);

		const std::shared_ptr<ToneSequenceEntry> tone_entry = std::dynamic_pointer_cast<ToneSequenceEntry>(seq_entry);
		const std::shared_ptr<ImageSequenceEntry> img_entry = std::dynamic_pointer_cast<ImageSequenceEntry>(seq_entry);

		if (tone_entry != nullptr) {
			if (!adv_seq) {
				//BOOST_LOG_SEV(lg::get(), sev::error) << "FormatSequenceEntry: Controller firmware needs v2.x for tone sequence entry support ";
				return 0;
			}
			type = 7;
		}
		else if (img_entry != nullptr) {
			if (adv_seq) {
				type = 6;
			}
			else {
				type = 1;
			}
		}
		else {
			//BOOST_LOG_SEV(lg::get(), sev::error) << "FormatSequenceEntry: Unrecognised type name " << typeid(*seq_entry).name();
			return 0;
		}

		std::array<std::uint8_t, 16> img_uuid = seq_entry->UUID();
		seq_data.clear();
		seq_data.assign(img_uuid.begin(), img_uuid.begin() + 16);

		if (adv_seq) {
			// Send Frequency offsets + Tone Select/Deselect
			seq_data.push_back(static_cast<std::uint8_t>(RFChannel::max - RFChannel::min + 1 + 1));
		}
		else {
			seq_data.push_back(static_cast<std::uint8_t>(seq_entry->NumRpts()));
		}

		using sdor_dly_type = std::chrono::duration<std::uint16_t, std::ratio < 1, 10000000 >>;
		std::uint16_t sdor_dly = std::chrono::duration_cast<sdor_dly_type>(seq_entry->SyncOutDelay()).count();

		seq_data.push_back(static_cast<std::uint8_t>(sdor_dly & 0xff));
		seq_data.push_back(static_cast<std::uint8_t>(sdor_dly >> 8));

		if (img_entry != nullptr) {
			unsigned int int_clk;
			if (ims->Ctlr().GetVersion().revision < 38) {
				int_clk = FrequencyRenderer::RenderAsPointRate(ims, img_entry->IntOsc(), false);
			}
			else {
				int_clk = FrequencyRenderer::RenderAsPointRate(ims, img_entry->IntOsc(), true);
			}

			seq_data.push_back(static_cast<std::uint8_t>(int_clk & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((int_clk >> 8) & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((int_clk >> 16) & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((int_clk >> 24) & 0xff));

			seq_data.push_back(static_cast<std::uint8_t>(img_entry->ExtDiv() & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((img_entry->ExtDiv() >> 8) & 0xff));

			using img_dly_type = std::chrono::duration<std::uint32_t, std::ratio < 1, 10000 >>;
			std::uint32_t img_dly = std::chrono::duration_cast<img_dly_type>(img_entry->PostImgDelay()).count();

			seq_data.push_back(static_cast<std::uint8_t>(img_dly & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((img_dly >> 8) & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((img_dly >> 16) & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((img_dly >> 24) & 0xff));

		}
		else if (tone_entry != nullptr) {
			seq_data.push_back(static_cast<std::uint8_t>(tone_entry->InitialIndex()));
			seq_data.insert(seq_data.end(), { 0, 0, 0, 0, 0, 0, 0, 0, 0 });
		}

		if (adv_seq) {
			seq_data.push_back(static_cast<std::uint8_t>(seq_entry->NumRpts() & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((seq_entry->NumRpts() >> 8) & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((seq_entry->NumRpts() >> 16) & 0xff));
		}

		if (adv_seq) {
			// Frequency offset registers
			std::uint16_t addr = SYNTH_REG_Freq_Offset_Ch1 << 2, data;
			for (RFChannel chan = RFChannel::min; ; chan++, addr += 4) {
				const MHz& offset = seq_entry->GetFrequencyOffset(chan);
				if (offset < 0.0) {
					uint32_t offset_int = FrequencyRenderer::RenderAsStaticOffset(ims, -offset, 1);
					data = static_cast<std::uint16_t>(offset_int);
				}
				else {
					uint32_t offset_int = FrequencyRenderer::RenderAsStaticOffset(ims, offset, 0);
					data = static_cast<std::uint16_t>(offset_int);
				}
				seq_data.push_back(static_cast<std::uint8_t>(addr & 0xff));
				seq_data.push_back(static_cast<std::uint8_t>((addr >> 8) & 0xff));
				seq_data.push_back(static_cast<std::uint8_t>(data & 0xff));
				seq_data.push_back(static_cast<std::uint8_t>((data >> 8) & 0xff));

				if (chan == RFChannel::max) break;
			}

			uint16_t tonedata = 0;
			if (tone_entry != nullptr) {
				switch (tone_entry->ControlSource()) {
				case SignalPath::ToneBufferControl::HOST: tonedata |= 0x100; break;
				case SignalPath::ToneBufferControl::EXTERNAL: tonedata |= 0x300; break;
				case SignalPath::ToneBufferControl::EXTERNAL_EXTENDED: tonedata |= 0x500; break;
				default: tonedata |= 0x100; break;
				}

				tonedata += (tone_entry->InitialIndex() & 0xFF);
			}
			uint16_t toneaddr = SYNTH_REG_UseLocalIndex;
			if (ims->Synth().GetVersion().revision < 90) {
				// overwrites Compensation Use bits
				toneaddr = SYNTH_REG_UseLocal;
				// make them active
				tonedata |= 0x1800;
			}
			seq_data.push_back((toneaddr << 2) & 0xff);
			seq_data.push_back((toneaddr >> 6) & 0xff);
			seq_data.push_back(static_cast<std::uint8_t>(tonedata & 0xff));
			seq_data.push_back(static_cast<std::uint8_t>((tonedata >> 8) & 0xff));
		}

		return type;
	}

	int FormatSequenceBuffer(const ImageSequence& seq, std::shared_ptr<IMSSystem> ims, boost::container::deque < std::uint8_t >& seq_data)
	{
		static const char signature[] = "iMS_SEQ";
		static const char signature2[] = "iMS_SQ2";
		seq_data.clear();

		// Sequence Buffer header
		if (seq.size() <= (int)UINT16_MAX)
		{
			seq_data.assign(signature, signature + sizeof(signature));
			AppendVarToContainer< boost::container::deque<std::uint8_t>, std::uint16_t >(seq_data, static_cast<std::uint16_t>(seq.size()));
		}
		else
		{
			seq_data.assign(signature2, signature2 + sizeof(signature2));
			AppendVarToContainer< boost::container::deque<std::uint8_t>, std::uint32_t >(seq_data, static_cast<std::uint32_t>(seq.size()));
		}
		//seq_data.push_back(static_cast<std::uint16_t>(seq.size()) & 0xFF);
		//seq_data.push_back(static_cast<std::uint16_t>((seq.size()) >> 8) & 0xFF);

		std::vector<std::uint8_t> v;
		for (ImageSequence::const_iterator it = seq.cbegin(); it != seq.cend(); ++it) {
			std::uint8_t type = FormatSequenceEntry(*it, ims, v);
			if (type == 0) {
				// error formatting sequence entry
				return 0;
			}
			seq_data.push_back(type);
			seq_data.push_back(static_cast<std::uint8_t>(v.size()));
			std::move(std::begin(v), std::end(v), std::back_inserter(seq_data));
		}

		return (int)seq_data.size();
	}

	class DMASupervisor : public IEventHandler
	{
	private:
		std::atomic<bool> m_busy{ true };
		std::atomic<int> m_tfr_size{ 0 };
	public:
		void EventAction(void* /*sender*/, const int message, const int param)
		{
			switch (message)
			{
			case (MessageEvents::MEMORY_TRANSFER_ERROR): m_busy.store(false);
				BOOST_LOG_SEV(lg::get(), sev::error) << "Memory Transfer Error";
				break;
			case (MessageEvents::MEMORY_TRANSFER_COMPLETE): m_busy.store(false); m_tfr_size.store(param);  
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Memory Transfer Complete " << param << " bytes transferred";
				break;
			}
		}
		bool Busy() const { return m_busy.load(); };
		void Reset() {
			m_busy.store(true);
			m_tfr_size.store(0);
		}
		int GetTransferredSize() {
			return m_tfr_size.load();
		}
	};

	class ImageDownloadEventTrigger :
		public IEventTrigger
	{
	public:
		ImageDownloadEventTrigger() { updateCount(ImageDownloadEvents::Count); }
		~ImageDownloadEventTrigger() {};
	};

	class ImageDownload::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const Image&);
		~Impl();

        LazyWorker downloadWorker;
        LazyWorker verifyWorker;
        LazyWorker rxWorker;
        
		std::weak_ptr<IMSSystem> m_ims;
		const Image& m_Image;
		ImageFormat m_fmt;

		//ImageBank m_bank;
		int m_startaddr { -1 };
		int m_msbFirst{ 0 };

		const std::unique_ptr<boost::container::deque<std::uint8_t>> m_imgdata;
		const std::unique_ptr<boost::container::deque<std::uint8_t>> m_vfydata;
		std::unique_ptr<ImageDownloadEventTrigger> m_Event;

		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(ImageDownload::Impl* dl) : m_parent(dl) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			ImageDownload::Impl* m_parent;
		};
		ResponseReceiver* Receiver;

		class VerifyResult : public IEventHandler
		{
		public:
			VerifyResult(ImageDownload::Impl* dl) : m_parent(dl) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			ImageDownload::Impl* m_parent;
		};
		VerifyResult* vfyResult;

		DMASupervisor *dmah;

		std::list <MessageHandle> dl_list;
		MessageHandle dl_final;
		mutable std::mutex dl_list_mutex;

        bool downloadRequested{ false };
        void DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);
    
        bool verifyRequested{ false };
        void VerifyWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);

	    void RxWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);

		std::deque<int> rxok_list;
		std::deque<int> rxerr_list;

		bool VerifyStarted{ false };

		BulkVerifier verifier;
	};

	ImageDownload::Impl::Impl(std::shared_ptr<IMSSystem> ims, const Image& img) : 
		m_ims(ims),
		m_Image(img),
		m_imgdata(new boost::container::deque<std::uint8_t>()),
		m_vfydata(new boost::container::deque<std::uint8_t>()),
		m_Event(new ImageDownloadEventTrigger()),
		Receiver(new ResponseReceiver(this)),
		vfyResult(new VerifyResult(this)),
		verifier(ims),
        downloadWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            DownloadWorkerLoop(running, cond, mtx);
        }),
        verifyWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            VerifyWorkerLoop(running, cond, mtx);
        }),
        rxWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            RxWorkerLoop(running, cond, mtx);
        })
	{
		m_fmt = ImageFormat(ims);

		// Subscribe listeners
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);

		verifier.BulkVerifierEventSubscribe(BulkVerifierEvents::VERIFY_SUCCESS, vfyResult);
		verifier.BulkVerifierEventSubscribe(BulkVerifierEvents::VERIFY_FAIL, vfyResult);

        dl_final = NullMessage;
	}

	ImageDownload::Impl::~Impl()
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

		verifier.BulkVerifierEventUnsubscribe(BulkVerifierEvents::VERIFY_SUCCESS, vfyResult);
		verifier.BulkVerifierEventUnsubscribe(BulkVerifierEvents::VERIFY_FAIL, vfyResult);

		delete vfyResult;
		delete Receiver;
	}

	void ImageDownload::Impl::ResponseReceiver::EventAction(void* /*sender*/, const int message, const int param)
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

	ImageDownload::ImageDownload(std::shared_ptr<IMSSystem> ims, const Image& img) : p_Impl(new Impl(ims, img))	{}

	ImageDownload::~ImageDownload() { delete p_Impl; p_Impl = nullptr; }

	// Pass the verify results back to the user
	void ImageDownload::Impl::VerifyResult::EventAction(void* /*sender*/, const int message, const int /*param*/)
	{
		switch (message)
		{
		case (BulkVerifierEvents::VERIFY_SUCCESS) : m_parent->m_Event->Trigger<int>((void *)m_parent, ImageDownloadEvents::VERIFY_SUCCESS, 0); break;
		case (BulkVerifierEvents::VERIFY_FAIL) : m_parent->m_Event->Trigger<int>((void *)m_parent, ImageDownloadEvents::VERIFY_FAIL, m_parent->verifier.Errors()); break;
		}
	}

	void ImageDownload::SetFormat(const ImageFormat& fmt)
	{
		p_Impl->m_fmt = fmt;
	}

	bool ImageDownload::StartDownload()
	{
        auto ims = p_Impl->m_ims.lock();
        if (!ims) return false;
        auto conn = ims->Connection();

		p_Impl->m_startaddr = -1;

		// Make sure Controller and Synthesiser are present
		if (!ims->Ctlr().IsValid()) return false;
		if (!ims->Synth().IsValid()) return false;

		// Check to see if Controller supports simultaneous download or Fast Transfer
		IMSController::Capabilities cap = ims->Ctlr().GetCap();
		if (cap.FastImageTransfer) {
			ImageTableViewer itv(ims);
			for (const auto& ite : itv) {
				if (ite.Matches(p_Impl->m_Image)) {
					// Image already present on Controller
					p_Impl->m_Event->Trigger<int>((void*)this, ImageDownloadEvents::DOWNLOAD_FINISHED, 0);
					return true;
				}
			}
		}
		else {

			if (!cap.SimultaneousPlayback)
			{
				// It doesn't.
				// Check to see if Controller is currently playing out
				HostReport iorpt(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_Img_Ctrl);

				DeviceReport ioresp = conn->SendMsgBlocking(iorpt);

				if (ioresp.Payload<std::uint16_t>() & CTRLR_REG_Img_Ctrl_IOS_Busy)
				{
					// It is, so abort
					return false;
				}
			}
		}

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
			p_Impl->dl_list.clear();
            p_Impl->downloadRequested = true;
			//VerifyReset();
			lck.unlock();

            break;
		}
		
        p_Impl->downloadWorker.notify();
		return true;
	}

	//bool ImageDownload::StartDownload(ImageBank bank, int start_addr) : m_bank(bank), m_startaddr(start_addr)
	//{
	//}

	bool ImageDownload::StartVerify()
	{
        auto ims = p_Impl->m_ims.lock();
        if (!ims) return false;
        auto conn = ims->Connection();

		// Make sure Controller is present
		if (!ims->Ctlr().IsValid()) return false;

        p_Impl->verifyWorker.start();
        p_Impl->rxWorker.start();
        
        int retries=10;
        while (retries)
		{
            std::unique_lock<std::mutex> lck{ p_Impl->verifyWorker.mutex(), std::try_to_lock };

            if (!lck.owns_lock()) {
                if (!--retries) return false;
                // Mutex lock failed, Verifier must be busy, try again later
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

            p_Impl->verifyRequested = true;
            break;
        }

		p_Impl->VerifyStarted = true;
		p_Impl->verifier.VerifyReset();
		p_Impl->verifyWorker.notify();
		return true;
	}

	int ImageDownload::GetVerifyError()
	{
		return (p_Impl->verifier.GetVerifyError());
	}

/*	bool ImageDownload::VerifyInProgress() const
	{
		std::unique_lock<std::mutex> vfylck{ p_Impl->m_vfymutex };
		bool verify = p_Impl->verifier.VerifyInProgress() || p_Impl->VerifyStarted;
		vfylck.unlock();
		return (verify);
	}*/

	void ImageDownload::ImageDownloadEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event->Subscribe(message, handler);
	}

	void ImageDownload::ImageDownloadEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event->Unsubscribe(message, handler);
	}

	// Image Downloading Thread
    void ImageDownload::Impl::DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
	{
        auto ims = m_ims.lock();
        if (!ims) return;
		IMSController::Capabilities cap = ims->Ctlr().GetCap();
		HostReport * iorpt;
    
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
			if (cap.FastImageTransfer) {
				// Fast transfer to large capacity memory

				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_FPIFormat);

				DeviceReport ioresp = conn->SendMsgBlocking(*iorpt);

				if (ioresp.Payload<std::uint16_t>() & CTRLR_REG_FPIFormat_MSB_Supported)
				{
					// Device supports MSB Mode
					m_msbFirst = 1;

					delete iorpt;
					iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_FPIFormat);
					// Bit 1 enables MSB-first mode. Bit 0 (supported) is read-only and always reads 1, so
					// writing back the value read shifted left accumulated set bits with every download.
					iorpt->Payload<std::uint16_t>(CTRLR_REG_FPIFormat_MSB_Enable);

					// Set Controller to download mode
					if (NullMessage == conn->SendMsg(*iorpt))
					{
						BOOST_LOG_SEV(lg::get(), sev::error) << "Failed to set MSB Mode";

						delete iorpt;
						lck.unlock();
						continue;
					}
				}
				delete iorpt;

				// Create Byte Vector
				m_imgdata->clear();
				int BytesInImagePoint = FormatImage(m_Image, ims, *m_imgdata, m_fmt, m_msbFirst);
				std::uint32_t ImageBytes = BytesInImagePoint * m_Image.Size();

				/*std::uint32_t checksum = 0;
				boost::container::deque<std::uint8_t>::iterator it =  m_imgdata->begin();
				while (it != m_imgdata->end()) {
					std::uint32_t val = static_cast<std::uint32_t>(*it++);
					val |= (static_cast<std::uint32_t>(*it++) << 8);
					val |= (static_cast<std::uint32_t>(*it++) << 16);
					val |= (static_cast<std::uint32_t>(*it++) << 24);
					checksum += val;
				}
				std::cout << "Calculated Checksum: 0x" << std::hex << checksum << std::endl;*/
				
				// Attempt to add new Image to Image Table
				iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::WRITE, 0);
				ReportFields f = iorpt->Fields();
				f.context = static_cast<std::uint8_t>(HostReport::ImageIndexOperations::ADD_ENTRY);
				iorpt->Fields(f);
				
				// Data format:
				// [15:0] = UUID
				// [19:16] = Image Length(bytes)
				// [23:20] = Image Length(Points)
				// [27:24] = Format Specifier
				// [43:28] = name

				std::array<std::uint8_t, 16> uuid = m_Image.GetUUID();
				std::vector<std::uint8_t> data(uuid.begin(), uuid.begin() + 16);
				for (int i = 0; i < 4; i++) {
					data.push_back((ImageBytes >> (i*8))& 0xFF);
				}
				std::uint32_t ImageSize = m_Image.Size();
				for (int i = 0; i < 4; i++) {
					data.push_back((ImageSize >> (i * 8)) & 0xFF);
				}
				std::uint32_t fmt_spec = m_fmt.GetFormatSpec();
				for (int i = 0; i < 4; i++) {
					data.push_back((fmt_spec >> (i * 8)) & 0xFF);
				}
				std::string name = m_Image.Name();
				name.resize(16, ' ');
				data.insert(data.end(), name.begin(), name.end());

				iorpt->Payload<std::vector<std::uint8_t>>(data);

				ioresp = conn->SendMsgBlocking(*iorpt);
				if (ioresp.Done() && !ioresp.GeneralError()) {
					ImageIndex ImageMemoryIndex = ioresp.Fields().addr;
					std::uint32_t ImageMemoryAddress = ioresp.Payload<std::uint32_t>();

					m_Event->Trigger<int>((void *)this, ImageDownloadEvents::IMAGE_DOWNLOAD_NEW_HANDLE, ImageMemoryIndex);

					dmah = new DMASupervisor();
					conn->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);

					// Start memory download
					conn->MemoryDownload(*m_imgdata, ImageMemoryAddress, ImageMemoryIndex, uuid);

					while (dmah->Busy()) {
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
					}
					//std::cout << "Memory Download complete" << std::endl;

					conn->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);

					int tfr_size = dmah->GetTransferredSize();
					delete dmah;

					if (tfr_size > 0) {
						// Transfer complete.  Add data to index table
						const IMSController& c = ims->Ctlr();
						ImageTable imgtbl = c.ImgTable();

						ImageTableEntry ite(ImageMemoryIndex, ImageMemoryAddress, ImageSize, tfr_size, 0, m_Image.GetUUID(), m_Image.Name());
						ImageTable::iterator iter = imgtbl.begin();
						do {
							if ((iter == imgtbl.end()) || (iter->Handle() > ImageMemoryIndex)) {
								imgtbl.insert(iter, ite);
								break;
							}
							++iter;
						} while (1);
						ims->Ctlr(IMSController(c.Model(), c.Description(), c.GetCap(), c.GetVersion(), imgtbl));

						m_Event->Trigger<int>((void *)this, ImageDownloadEvents::DOWNLOAD_FINISHED, tfr_size);
					}
					else {
						// Problem with DMA Transfer
						m_Event->Trigger<int>((void *)this, ImageDownloadEvents::DOWNLOAD_FAIL_TRANSFER_ABORT, 0);
                        BOOST_LOG_SEV(lg::get(), sev::error) << "Image Download DMA Transfer Failed.";
					}
				}
				else {
					// Problem setting up transfer, abort
					m_Event->Trigger<int>((void *)this, ImageDownloadEvents::DOWNLOAD_FAIL_MEMORY_FULL, 0);
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Failed to setup Image Download. Memory or Index Table Full?";
				}
				//if (m_imgdata->size()) m_imgdata->clear();
				delete iorpt;

			}
			else {
				// Download to internal 4Kpt memory
				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Ctrl);
				iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Ctrl_DL_Active);

				// Set Controller to download mode
				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					lck.unlock();
					continue;
				}
				delete iorpt;

				// Determine if all Image Points have identical FAPs.  If so, use CommonChannel mode for up to 4k points
				bool CommonChannels = true;
				for (Image::const_iterator it = m_Image.cbegin(); it != m_Image.cend(); ++it)
				{
					ImagePoint pt = (*it);
					if (!((pt.GetFAP(1) == pt.GetFAP(2)) && (pt.GetFAP(2) == pt.GetFAP(3)) && (pt.GetFAP(3) == pt.GetFAP(4))))
					{
						CommonChannels = false;
					}
				}

				int img_index = 0;
				int length = m_Image.Size();
				dl_final = NullMessage;

				std::vector<std::uint8_t> img_data;
				Image::const_iterator it = m_Image.cbegin();

				// Restrict to maximum size of Controller memory;
				length = std::min(length, ims->Ctlr().GetCap().MaxImageSize);
				if (!CommonChannels) {
					length = std::min(length, (ims->Ctlr().GetCap().MaxImageSize / 4));
				}

				while ((img_index < length) && (it < m_Image.cend()))
				{
					std::uint16_t img_addr;
					if (CommonChannels) {
						img_addr = img_index;
						// Add up to 60 bytes of data to vector
						for (int i = 0; i < 60; i += 5)
						{
							ImagePoint pt = (*it);
							FAP fap = pt.GetFAP(1);
							unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, fap.freq);
							img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 16)) & 0xFF));  // Top 16 bits only
							img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 8)) & 0xFF));
							std::uint16_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, fap.ampl);
							img_data.push_back(static_cast<std::uint8_t>(ampl & 0xFF));
							img_data.push_back(static_cast<std::uint8_t>(0));  // No phase data in Common Channels mode
							img_data.push_back(static_cast<std::uint8_t>(0));

							if ((++it == m_Image.cend()) || (++img_index == length))
							{
								break;
							}
						}
					}
					else {
						img_addr = 4 * img_index;
						// Add up to 60 bytes of data to vector
						for (int i = 0; i < 60; i += 20)
						{
							ImagePoint pt = (*it);
							for (int chan = 1; chan <= 4; chan++) {
								FAP fap = pt.GetFAP(chan);
								unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, fap.freq);
								img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 16)) & 0xFF));  // Top 16 bits only
								img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 8)) & 0xFF));
								std::uint16_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, fap.ampl);
								img_data.push_back(static_cast<std::uint8_t>(ampl & 0xFF));
								std::uint16_t phase = PhaseRenderer::RenderAsImagePoint(ims, fap.phase);
								img_data.push_back(static_cast<std::uint8_t>(phase & 0xFF));
								img_data.push_back(static_cast<std::uint8_t>((phase >> 8) & 0xFF));
							}

							if ((++it == m_Image.cend()) || (++img_index == length))
							{
								break;
							}
						}
					}

					iorpt = new HostReport(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::WRITE, img_addr);
					iorpt->Payload<std::vector<std::uint8_t>>(img_data);
					MessageHandle h = conn->SendMsg(*iorpt);
					delete iorpt;

					// Add message handle to download list so we can check the responses
					std::unique_lock<std::mutex> dllck{ dl_list_mutex };
					dl_list.push_back(h);
					dllck.unlock();

					img_data.clear();
				}

				std::unique_lock<std::mutex> dllck{ dl_list_mutex };
				if (!dl_list.empty()) dl_final = dl_list.back();
				dllck.unlock();

				// Program Image Length into NumPts register
				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_NumPts);
				iorpt->Payload<std::uint16_t>(length - 1); // program in one less than the number of points in the image

				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					lck.unlock();
					continue;
				}
				delete iorpt;

				// Send Image UUID to Controller
				// v1.0.1
				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_UUID);
				std::array<std::uint8_t, 16> uuid = m_Image.GetUUID();
				std::vector<std::uint8_t> v(uuid.begin(), uuid.begin() + 16);
				iorpt->Payload<std::vector<std::uint8_t>>(v);
				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					lck.unlock();
					continue;
				}
				delete iorpt;

				// Transfer complete.  Add data to index table
				const IMSController& c = ims->Ctlr();
				ImageTable imgtbl = c.ImgTable();

				ImageTableEntry ite(0, 0, length, length*20, 0, m_Image.GetUUID(), m_Image.Name());
				imgtbl.clear();
				imgtbl.push_back(ite);
				ims->Ctlr(IMSController(c.Model(), c.Description(), c.GetCap(), c.GetVersion(), imgtbl));

				// Clear download mode and set Common Channels bit if relevant
				iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Ctrl);
				if (CommonChannels) {
					iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Ctrl_Common_Channels);
				}
				else {
					iorpt->Payload<std::uint16_t>(0);
				}
				if (NullMessage == conn->SendMsg(*iorpt))
				{
					delete iorpt;
					lck.unlock();
					continue;
				}
				delete iorpt;
			}

			// Release lock, wait for next download trigger
			lck.unlock();
		}
	}

	// Image Verifying Thread
    void ImageDownload::Impl::VerifyWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
	{
        auto ims = m_ims.lock();
        if (!ims) return;
        IMSController::Capabilities cap = ims->Ctlr().GetCap();
		HostReport* iorpt;

		while (true) {
    		std::unique_lock<std::mutex> lck{ mtx };
			cond.wait(lck, [this, &running]() {
                return verifyRequested || !running;
            });

			// Allow thread to terminate 
			if (!running) break;
            verifyRequested = false;

            auto ims = m_ims.lock();
            if (!ims) break;
            auto conn = ims->Connection();

			// Verify loop
			if (cap.FastImageTransfer) {
				// Fast transfer to large capacity memory

				// Create Byte Vector
				m_imgdata->clear();
				/*int BytesInImagePoint = */FormatImage(m_Image, ims, *m_imgdata, m_fmt, m_msbFirst);
				//std::uint32_t ImageBytes = BytesInImagePoint * m_Image.Size();

				// Find image in image table by checking UUID
				int h = -1;
				for (ImageTable::const_iterator it = ims->Ctlr().ImgTable().cbegin(); it != ims->Ctlr().ImgTable().cend(); ++it)
				{
					if (it->UUID() == m_Image.GetUUID()) h = std::distance(ims->Ctlr().ImgTable().cbegin(), it);
				}
				if (h < 0) {
					// Failed. Abort.  TODO: Added error event
					continue;
				}
				ImageTableEntry ite = *(std::next(ims->Ctlr().ImgTable().cbegin(), h));

				dmah = new DMASupervisor();
				conn->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);

				// Start memory upload
				conn->MemoryUpload(*m_vfydata, ite.Address(), ite.Size(), h, ite.UUID());

				while (dmah->Busy()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}

				conn->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);

				//int tfr_size = dmah->GetTransferredSize();
				delete dmah;
				
				if (m_vfydata->size() < m_imgdata->size()) {
					m_imgdata->resize(m_vfydata->size());
				}
				else {
					m_vfydata->resize(m_imgdata->size());
				}
				int tfr_len = (int)m_imgdata->size();

				if (*m_vfydata == *m_imgdata) {
                    BOOST_LOG_SEV(lg::get(), sev::info) << "Verify passed";
                    m_Event->Trigger<int>((void *)this, ImageDownloadEvents::VERIFY_SUCCESS, 0);
                }
				else {
					int first_error = 0;
					for (int i = 0; i < tfr_len; i++) {
						if (m_vfydata->at(i) != m_imgdata->at(i)) {
							first_error = i;
							break;
						}
					}
                    BOOST_LOG_SEV(lg::get(), sev::info) << "Verify FAILED at " << first_error;
					m_Event->Trigger<int>((void *)this, ImageDownloadEvents::VERIFY_FAIL, first_error); break;
				}

				if (m_imgdata->size()) m_imgdata->clear();
				if (m_vfydata->size()) m_vfydata->clear();
			}
			else {

				int img_index = 0;
				int length = m_Image.Size();

				// Determine if all Image Points have identical FAPs.  If so, use CommonChannel mode for up to 4k points
				bool CommonChannels = true;
				for (Image::const_iterator it = m_Image.cbegin(); it != m_Image.cend(); ++it)
				{
					ImagePoint pt = (*it);
					if (!((pt.GetFAP(1) == pt.GetFAP(2)) && (pt.GetFAP(2) == pt.GetFAP(3)) && (pt.GetFAP(3) == pt.GetFAP(4))))
					{
						CommonChannels = false;
					}
				}

				// Restrict to maximum size of Controller memory;
				length = std::min(length, ims->Ctlr().GetCap().MaxImageSize);
				if (!CommonChannels)
				{
					length = std::min(length, (ims->Ctlr().GetCap().MaxImageSize / 4));
				}

				std::vector<std::uint8_t> img_data;
				Image::const_iterator it = m_Image.cbegin();
				while ((img_index < length) && (it < m_Image.cend()))
				{
					std::uint16_t img_addr;
					if (CommonChannels) {
						img_addr = img_index;
						// Add up to 60 bytes of data to vector
						for (int i = 0; i < 60; i += 5)
						{
							ImagePoint pt = (*it);
							FAP fap = pt.GetFAP(1);
							unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, fap.freq);
							img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 16)) & 0xFF));  // Top 16 bits only
							img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 8)) & 0xFF));
							std::uint16_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, fap.ampl);
							img_data.push_back(static_cast<std::uint8_t>(ampl & 0xFF));
							img_data.push_back(static_cast<std::uint8_t>(0)); // No phase data in Common Channels mode
							img_data.push_back(static_cast<std::uint8_t>(0));

							if ((++it == m_Image.cend()) || (++img_index == length))
							{
								break;
							}
						}
					}
					else {
						img_addr = 4 * img_index;
						// Add up to 60 bytes of data to vector
						for (int i = 0; i < 60; i += 20)
						{
							ImagePoint pt = (*it);
							for (int chan = 1; chan <= 4; chan++) {
								FAP fap = pt.GetFAP(chan);
								unsigned int freq = FrequencyRenderer::RenderAsImagePoint(ims, fap.freq);
								img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 16)) & 0xFF));  // Top 16 bits only
								img_data.push_back(static_cast<std::uint8_t>((freq >> (ims->Synth().GetCap().freqBits - 8)) & 0xFF));
								std::uint16_t ampl = AmplitudeRenderer::RenderAsImagePoint(ims, fap.ampl);
								img_data.push_back(static_cast<std::uint8_t>(ampl & 0xFF));
								std::uint16_t phase = PhaseRenderer::RenderAsImagePoint(ims, fap.phase);
								img_data.push_back(static_cast<std::uint8_t>(phase & 0xFF));
								img_data.push_back(static_cast<std::uint8_t>((phase >> 8) & 0xFF));
							}

							if ((++it == m_Image.cend()) || (++img_index == length))
							{
								break;
							}
						}
					}


					iorpt = new HostReport(HostReport::Actions::CTRLR_IMAGE, HostReport::Dir::READ, img_addr);
					ReportFields f = iorpt->Fields();
					f.len = static_cast<std::uint16_t>(img_data.size());
					iorpt->Fields(f);
					MessageHandle h = conn->SendMsg(*iorpt);
					delete iorpt;

					// Add image data to verify memory
					std::shared_ptr<VerifyChunk> chunk(new VerifyChunk(h, img_data, img_addr));
					verifier.AddChunk(chunk);

					img_data.clear();
				}
			}
			verifier.Finalize();
			// Wait for next download trigger
			VerifyStarted = false;
		}
	}

	// Image Readback Verify Data Processing Thread
    void ImageDownload::Impl::RxWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
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
					//if (dl_list.empty())
					if (handle == dl_final)
					{
						m_Event->Trigger<int>((void *)this, ImageDownloadEvents::DOWNLOAD_FINISHED, 0);
					}
				}
				dllck.unlock();

			}

			while (!rxerr_list.empty())
			{
				int param = rxerr_list.front();
				rxerr_list.pop_front();

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
					m_Event->Trigger<int>((void *)this, ImageDownloadEvents::DOWNLOAD_ERROR, handle);
				}
				dllck.unlock();

			}

		}
	}

	class ImagePlayerEventTrigger :
		public IEventTrigger
	{
	public:
		ImagePlayerEventTrigger() { updateCount(ImagePlayerEvents::Count); }
		~ImagePlayerEventTrigger() {};
	};

	class ImagePlayer::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const Image&, const PlayConfiguration*);
		Impl(std::shared_ptr<IMSSystem>, const ImageTableEntry&, const PlayConfiguration*, const kHz InternalClock);
		Impl(std::shared_ptr<IMSSystem>, const ImageTableEntry&, const PlayConfiguration*, const int ExtClockDivide);
		~Impl();

		std::weak_ptr<IMSSystem> m_ims;
		//const Image& img;
		const std::array<std::uint8_t, 16> uuid;
		const int img_size;
		const Frequency int_clk;
		const int ext_div;
		const PlayConfiguration* cfg;
		const std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250);
		ImageFormat m_fmt;

		std::unique_ptr<ImagePlayerEventTrigger> m_Event;
		bool ConfigurePlayback(std::shared_ptr<IMSSystem> ims);

		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(ImagePlayer::Impl* pl) : m_parent(pl) {};
			void EventAction(void* sender, const int message, const int param);
		private:
			ImagePlayer::Impl* m_parent;
		};
		ResponseReceiver* Receiver;

		bool BackgroundThreadRunning;
		mutable std::mutex m_bkmutex;
		std::condition_variable m_bkcond;
		std::thread BackgroundThread;
		void BackgroundWorker();

		std::queue<MessageHandle> finishHandle;
		std::queue<MessageHandle> progressHandle;
		int latestProgress{ -1 };
		bool ImagePlaying{ false };
	};

	ImagePlayer::Impl::Impl(std::shared_ptr<IMSSystem> ims, const Image& img, const PlayConfiguration* cfg) :
		m_ims(ims),
		uuid(img.GetUUID()),
		img_size(img.Size()),
		int_clk(img.ClockRate()),
		ext_div(img.ExtClockDivide()),
		cfg(cfg),
		m_Event(new ImagePlayerEventTrigger()),
		Receiver(new ResponseReceiver(this))
	{
		BackgroundThreadRunning = true;
		BackgroundThread = std::thread(&ImagePlayer::Impl::BackgroundWorker, this);

		// Subscribe listener
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);
		if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46)
			conn->MessageEventSubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
	}

	ImagePlayer::Impl::Impl(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const PlayConfiguration* cfg, const kHz InternalClock) :
		m_ims(ims),
		uuid(ite.UUID()),
		img_size(ite.NPts()),
		int_clk(InternalClock),
		ext_div(1),
		cfg(cfg),
		m_Event(new ImagePlayerEventTrigger()),
		Receiver(new ResponseReceiver(this))
	{
		BackgroundThreadRunning = true;
		BackgroundThread = std::thread(&ImagePlayer::Impl::BackgroundWorker, this);

		// Subscribe listener
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);
		if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46)
			conn->MessageEventSubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
	}

	ImagePlayer::Impl::Impl(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const PlayConfiguration* cfg, const int ExtClockDivide) :
		m_ims(ims),
		uuid(ite.UUID()),
		img_size(ite.NPts()),
		int_clk(kHz(1.0)),
		ext_div(ExtClockDivide),
		cfg(cfg),
		m_Event(new ImagePlayerEventTrigger()),
		Receiver(new ResponseReceiver(this))
	{
		BackgroundThreadRunning = true;
		BackgroundThread = std::thread(&ImagePlayer::Impl::BackgroundWorker, this);

		// Subscribe listener
		auto conn = ims->Connection();
		conn->MessageEventSubscribe(MessageEvents::SEND_ERROR, Receiver);
		conn->MessageEventSubscribe(MessageEvents::TIMED_OUT_ON_SEND, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_RECEIVED, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_VALID, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_TIMED_OUT, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_CRC, Receiver);
		conn->MessageEventSubscribe(MessageEvents::RESPONSE_ERROR_INVALID, Receiver);
		if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46)
			conn->MessageEventSubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
	}

	ImagePlayer::Impl::~Impl()
	{
		// Unblock worker thread
		BackgroundThreadRunning = false;
		m_bkcond.notify_one();

		BackgroundThread.join();

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
            if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46)
                conn->MessageEventUnsubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
        });
	}

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const Image& img) :
			p_Impl(new Impl(ims, img, &(this->cfg))) {};

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const Image& img, const PlayConfiguration& cfg) :
			cfg(cfg), p_Impl(new Impl(ims, img, &(this->cfg))) {};

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const kHz InternalClock) :
			p_Impl(new Impl(ims, ite, &(this->cfg), InternalClock)) {};

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const int ExtClockDivide) :
			p_Impl(new Impl(ims, ite, &(this->cfg), ExtClockDivide)) {};

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const PlayConfiguration& cfg, const kHz InternalClock) :
			cfg(cfg), p_Impl(new Impl(ims, ite, &(this->cfg), InternalClock)) {};

	ImagePlayer::ImagePlayer(std::shared_ptr<IMSSystem> ims, const ImageTableEntry& ite, const PlayConfiguration& cfg, const int ExtClockDivide) :
			cfg(cfg), p_Impl(new Impl(ims, ite, &(this->cfg), ExtClockDivide)) {};

	ImagePlayer::~ImagePlayer()	{ delete p_Impl; p_Impl = nullptr; };

	bool ImagePlayer::Impl::ConfigurePlayback(std::shared_ptr<IMSSystem> ims)
	{
        auto conn = ims->Connection();
        HostReport* iorpt;

        int ckgen_mode = 0;

        iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_ClockOutput);
        DeviceReport resp = conn->SendMsgBlocking(*iorpt);
        if (resp.Done()) {
            ckgen_mode = resp.Payload<std::uint16_t>() & 0x1;
        }
        delete iorpt;

        // Set ImgModes CTRLR_REG_ImgModesExt
        iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ImgModes);
        int TFR{ 0 };
        if (cfg->int_ext == PointClock::EXTERNAL)
        {
            TFR |= 4;
        }
        switch (cfg->trig)
        {
        case ImageTrigger::CONTINUOUS: TFR |= 3; break;
        case ImageTrigger::EXTERNAL: TFR |= 1; break;
        case ImageTrigger::HOST: TFR |= 2; break;
        case ImageTrigger::POST_DELAY: TFR |= 0; break;
        }
        if (cfg->rpts == Repeats::FOREVER)
        {
            TFR |= 8;
        }
        else if (cfg->rpts != Repeats::NONE) {
            int i = cfg->n_rpts & 0xFF;
            TFR |= (i << 8);
        }
        iorpt->Payload<std::uint16_t>(TFR);

        if (NullMessage == conn->SendMsg(*iorpt))
        {
            delete iorpt;
            return false;
        }
        delete iorpt;
        if (cfg->rpts == Repeats::PROGRAM) {
            // extend repeats to 24 bit
            iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ImgModesExt);
            iorpt->Payload<std::uint16_t>((cfg->n_rpts >> 8) & 0xFF);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
        }


        // Set Post-Image Delay, if required
        if (cfg->trig == ImageTrigger::POST_DELAY)
        {
            iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ImgDelay);
            // Resolution of 1/10 millisecond, max 6.5s
            iorpt->Payload<std::uint16_t>(cfg->del.count());
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
        }

        // Set external signal polarity, if required
        if (!ckgen_mode && ((cfg->trig == ImageTrigger::EXTERNAL) || (cfg->int_ext == PointClock::EXTERNAL) ) )
        {
            iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ExtPolarity);

            // bit 0 = clock; bit 1 = trigger
            // <0> = rising edge; <1> = falling edge
            std::uint16_t d = 0;
            d |= (cfg->clk_pol == Polarity::INVERSE) ? 1 : 0;
            d |= (cfg->trig_pol == Polarity::INVERSE) ? 2 : 0;
            iorpt->Payload<std::uint16_t>(d);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
        }

        // Set Internal Oscillator clock rate or exteral clock divider, as appropriate
        if (!ckgen_mode) {
            bool PrescalerDisable = true;
            if ((int_clk < 5000.0) || !ims->Ctlr().GetCap().FastImageTransfer) {
                PrescalerDisable = false;
            }
            if (ims->Ctlr().GetCap().FastImageTransfer) {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Ctrl);
                if (PrescalerDisable) {
                    iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Ctrl_Prescaler_Disable);
                }
                else {
                    iorpt->Payload<std::uint16_t>(0);
                }
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }
            if (cfg->int_ext == PointClock::INTERNAL)
            {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_OscFreq);
                iorpt->Payload<std::uint16_t>(FrequencyRenderer::RenderAsPointRate(ims, int_clk, PrescalerDisable));

                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }
        }

        if (cfg->int_ext == PointClock::EXTERNAL) {
            if (ckgen_mode) {
                BOOST_LOG_SEV(lg::get(), sev::warning) << "External Image Clock requested with Signal Generator On.  Disabling signal generator";
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ClockOutput);
                iorpt->Payload<uint16_t>(0);
                MessageHandle h = conn->SendMsg(*iorpt);
                if (NullMessage == h)
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }
            int lcl_ext_div = (ext_div < 1) ? 1 : (ext_div > 65535) ? 65535 : ext_div;
            if (ims->Ctlr().GetCap().FastImageTransfer) {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_ExtDiv);
            }
            else {
                // iMS Lite reuses OscFreq field
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_OscFreq);
            }
            iorpt->Payload<std::uint16_t>(lcl_ext_div);

            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
        }

        // Set ImageFormat (v1.8 - use auto mode only for now)
        //if (m_fmt.IsAuto()) {
        //	iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Img_FormatLo);
        //	iorpt->Payload<uint32_t>(0x80000fff);

        //	if (NullMessage == conn->SendMsg(*iorpt))
        //	{
        //		delete iorpt;
        //		return false;
        //	}
        //	delete iorpt;
        //}

        return true;
	}

	bool ImagePlayer::Play(ImageTrigger start_trig)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        { 
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            auto conn = ims->Connection();
            IMSController::Capabilities cap = ims->Ctlr().GetCap();

            // Check to see if we are already playing back or downloading
            HostReport *iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_Img_Ctrl);
            DeviceReport resp = conn->SendMsgBlocking(*iorpt);
            if ((resp.Payload<std::uint16_t>() & CTRLR_REG_Img_Ctrl_IOS_Busy) ||
                ((cap.SimultaneousPlayback == false) && (resp.Payload<std::uint16_t>() & CTRLR_REG_Img_Ctrl_DL_Active)) ) {
                if (resp.Payload<std::uint16_t>() & CTRLR_REG_Img_Ctrl_IOS_Busy)
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Already playing back";
                else
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Simultaneous playback/download not supported";
                delete iorpt;
                return false;
            }
            delete iorpt;

            // Readback UUID from hardware to check the Image has been downloaded and is identical to the one we're being asked to play
            // (only for Lite Controller which has a single Image Memory)
            if (!cap.FastImageTransfer) {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_UUID);
                ReportFields f = iorpt->Fields();
                f.len = static_cast<std::uint16_t>(p_Impl->uuid.size());
                iorpt->Fields(f);
                resp = conn->SendMsgBlocking(*iorpt);
                if (!resp.Done()) {
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Couldn't read back UUID";
                    delete iorpt;
                    return false;
                }
                else {
                    std::vector<std::uint8_t> v = resp.Payload<std::vector<std::uint8_t>>();
                    std::array<std::uint8_t, 16> uuid;
                    v.resize(16);
                    std::copy(v.begin(), v.end(), uuid.begin());
                    if (uuid != p_Impl->uuid) {
                        BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: UUID Mismatch";
                        delete iorpt;
                        return false;
                    }
                }
                delete iorpt;
            }
            
            if (!p_Impl->ConfigurePlayback(ims)) return false;

            // For large memory capacity controllers, Start DMA Engine and program NumPts
            if (cap.FastImageTransfer) {
                std::array<std::uint8_t, 16> uuid = p_Impl->uuid;
                std::vector<std::uint8_t> data(uuid.begin(), uuid.begin() + 16);
                data.push_back(static_cast<std::uint8_t>(cfg.n_rpts & 0xFF));
                if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().major > 1) {
                    data.push_back(static_cast<std::uint8_t>((cfg.n_rpts >> 8) & 0xFF));
                    data.push_back(static_cast<std::uint8_t>((cfg.n_rpts >> 16) & 0xFF));
                }
                data.push_back(static_cast<std::uint8_t>(cfg.rpts));

                iorpt = new HostReport(HostReport::Actions::CTRLR_SYNDMA, HostReport::Dir::WRITE, CTRLR_SYNDMA_Start_DMA);
                iorpt->Payload<std::vector<std::uint8_t>>(data);
                resp = conn->SendMsgBlocking(*iorpt);
                if (!resp.Done() || resp.GeneralError())
                {
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Unable to start DMA";
                    delete iorpt;
                    return false;
                }
                delete iorpt;

                int length = p_Impl->img_size;
                // Restrict to maximum size of Controller memory;
                length = std::min(length, ims->Ctlr().GetCap().MaxImageSize);
                // Program Image Length into NumPts register
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_NumPtsLo);
                iorpt->Payload<std::uint32_t>(length);

                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Image Length Program Failed";
                    delete iorpt;
                    return false;
                }
                delete iorpt;
            }

            // Play Image
            iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Play);
            if (start_trig == ImageTrigger::EXTERNAL) {
                iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Play_ERUN);
            }
            else {
                iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Play_RUN);
            }

            if (NullMessage == conn->SendMsg(*iorpt))
            {
                BOOST_LOG_SEV(lg::get(), sev::error) << "Image Playback Start Failed: Play Command Failed";
                delete iorpt;
                return false;
            }
            delete iorpt;

            {
                std::unique_lock<std::mutex> lck{ p_Impl->m_bkmutex };
                p_Impl->ImagePlaying = true;
                p_Impl->m_bkcond.notify_one();
                lck.unlock();
                p_Impl->m_Event->Trigger<int>(this, ImagePlayerEvents::IMAGE_STARTED, 0);
            }
            BOOST_LOG_SEV(lg::get(), sev::info) << "Image Playback Start request sent";
            return true;
        }).value_or(false);
	}

	bool ImagePlayer::GetProgress()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        { 
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;

            if (!p_Impl->ImagePlaying) return false;

            auto conn = ims->Connection();
            IMSController::Capabilities cap = ims->Ctlr().GetCap();

            HostReport *iorpt;
            if (cap.FastImageTransfer) {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_Img_ProgressLo);
                ReportFields f = iorpt->Fields();
                f.len = 4;
                iorpt->Fields(f);
            }
            else {
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_Img_Progress);
            }

            {
                std::unique_lock<std::mutex> lck{ p_Impl->m_bkmutex };
                p_Impl->progressHandle.push(conn->SendMsg(*iorpt));
                delete iorpt;

                if (NullMessage == p_Impl->progressHandle.back())
                {
                    lck.unlock();
                    return false;
                }
                //std::cout << "Get Progress {" << p_Impl->progressHandle.front() << " " << p_Impl->progressHandle.back() << "} ";
                BOOST_LOG_SEV(lg::get(), sev::info) << "Image Progress request sent";
            }
            return true;
        }).value_or(false);            
	}
	
	bool ImagePlayer::Stop(StopStyle s)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        { 
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            auto conn = ims->Connection();
            HostReport *iorpt;
            
            if (ImagePlayer::StopStyle::IMMEDIATELY == s)
            {
                // Force Stop Image
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Play);
                iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Play_FSTOP);
                BOOST_LOG_SEV(lg::get(), sev::info) << "Image Playback Force Stop request sent";
                if (NullMessage == conn->SendMsg(*iorpt))
                {
                    delete iorpt;
                    return false;
                }
                delete iorpt;
                IMSController::Capabilities cap = ims->Ctlr().GetCap();
                if (cap.FastImageTransfer) {
                    iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Stop);
                    if (NullMessage == conn->SendMsg(*iorpt))
                    {
                        delete iorpt;
                        return false;
                    }
                    delete iorpt;
                    iorpt = new HostReport(HostReport::Actions::CTRLR_SYNDMA, HostReport::Dir::WRITE, CTRLR_SYNDMA_DMA_Abort);
                    if (NullMessage == conn->SendMsg(*iorpt))
                    {
                        delete iorpt;
                        return false;
                    }
                    delete iorpt;
                }
            }
            else {
                // Gracefully Stop Image at end of currenr repeat
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::WRITE, CTRLR_REG_Img_Play);
                iorpt->Payload<std::uint16_t>(CTRLR_REG_Img_Play_STOP);
                BOOST_LOG_SEV(lg::get(), sev::info) << "Image Playback Graceful Stop request sent";
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

	void ImagePlayer::SetPostDelay(const std::chrono::duration<double>& d)
	{
		cfg.del = std::chrono::duration_cast<ImagePlayer::PlayConfiguration::post_delay>(d);
		cfg.trig = ImagePlayer::ImageTrigger::POST_DELAY;
	}

	void ImagePlayer::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
        auto ims = m_parent->m_ims.lock();
        if (!ims) return;
        auto conn = ims->Connection();

		while (!m_parent->progressHandle.empty() && 
			((NullMessage == m_parent->progressHandle.front()) || (param > (m_parent->progressHandle.front())))) m_parent->progressHandle.pop();
		if (ims->Ctlr().Model() == "iMSP" && ims->Ctlr().GetVersion().revision <= 46) {
			while (!m_parent->finishHandle.empty() &&
				((NullMessage == m_parent->finishHandle.front()) || (param > (m_parent->finishHandle.front())))) m_parent->finishHandle.pop();
		}
		switch (message)
		{
		case (MessageEvents::INTERRUPT_RECEIVED): {
			unsigned int type = (param >> 16);
	
			if (type == CTRLR_INTERRUPT_SINGLE_IMAGE_FINISHED) {
				std::unique_lock<std::mutex> lck{ m_parent->m_bkmutex };
				m_parent->ImagePlaying = false;
				m_parent->m_bkcond.notify_one();
				lck.unlock();
			}

			break;
		}

		case (MessageEvents::RESPONSE_RECEIVED) :
		case (MessageEvents::RESPONSE_ERROR_VALID) : {

			{
				if ((!m_parent->progressHandle.empty()) && (param == m_parent->progressHandle.front()))
				{
					auto object = static_cast<IConnectionManager*>(sender);
					IOReport resp = object->Response(param);

					std::unique_lock<std::mutex> lck{ m_parent->m_bkmutex };
					if (ims->Ctlr().GetCap().FastImageTransfer) {
						m_parent->latestProgress = resp.Payload<std::uint32_t>();
					}
					else {
						m_parent->latestProgress = resp.Payload<std::uint16_t>();
					}
					m_parent->progressHandle.pop();
					m_parent->m_bkcond.notify_one();
					lck.unlock();  // Wait until thread is not consuming the mutex (might be in timeout loop) before notifying
					//std::cout << "Recvd " << param << std::endl;

				}
				if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46) break;
				else if ((!m_parent->finishHandle.empty()) && (param == m_parent->finishHandle.front()))
				{
					auto object = static_cast<IConnectionManager*>(sender);
					IOReport resp = object->Response(param);

					std::uint16_t ctrl = resp.Payload<std::uint16_t>();
					std::unique_lock<std::mutex> lck{ m_parent->m_bkmutex };
					m_parent->finishHandle.pop();
					if (!(ctrl & CTRLR_REG_Img_Ctrl_IOS_Busy)) {
						m_parent->ImagePlaying = false;
						m_parent->m_bkcond.notify_one();
					}
					lck.unlock();
				}
			}
			break;
		}
		case (MessageEvents::TIMED_OUT_ON_SEND) :
		case (MessageEvents::SEND_ERROR) :
		case (MessageEvents::RESPONSE_TIMED_OUT) :
		case (MessageEvents::RESPONSE_ERROR_CRC) :
		case (MessageEvents::RESPONSE_ERROR_INVALID) : {

			{
				if ((!m_parent->progressHandle.empty()) && (param == m_parent->progressHandle.front()))
				{
					std::unique_lock<std::mutex> lck{ m_parent->m_bkmutex };
					m_parent->progressHandle.pop();
					lck.unlock();
				}
				if ((!m_parent->finishHandle.empty()) && (param == m_parent->finishHandle.front()))
				{
					std::unique_lock<std::mutex> lck{ m_parent->m_bkmutex };
					m_parent->finishHandle.pop();
					lck.unlock();
				}
				break;
			}
		}
		}
	}

	// Notify application when something happens
	void ImagePlayer::ImagePlayerEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event->Subscribe(message, handler);

        auto ims = p_Impl->m_ims.lock();
        if (!ims) return;
        auto conn = ims->Connection();

		if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46) {
			int IntrMask;
			if (message == ImagePlayerEvents::IMAGE_FINISHED) {
				IntrMask = (int)(1 << CTRLR_INTERRUPT_SINGLE_IMAGE_FINISHED);
				HostReport *iorpt;
				iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 1);
				iorpt->Payload<int>(IntrMask);
				ReportFields f = iorpt->Fields();
				f.len = sizeof(IntrMask);
				iorpt->Fields(f);
				ims->Connection()->SendMsg(*iorpt);
				delete iorpt;
			}
		}
	}

	void ImagePlayer::ImagePlayerEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event->Unsubscribe(message, handler);

        auto ims = p_Impl->m_ims.lock();
        if (!ims) return;
        auto conn = ims->Connection();

		if (!p_Impl->m_Event->Subscribers(message) && (message == ImagePlayerEvents::IMAGE_FINISHED)) {
			int IntrMask = (int)~(1 << CTRLR_INTERRUPT_SINGLE_IMAGE_FINISHED);
			HostReport *iorpt;
			iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
			iorpt->Payload<int>(IntrMask);
			ReportFields f = iorpt->Fields();
			f.len = sizeof(IntrMask);
			iorpt->Fields(f);
			ims->Connection()->SendMsg(*iorpt);
			delete iorpt;
		}
	}

	void ImagePlayer::Impl::BackgroundWorker()
	{
		bool ImagePlaying_last = false;
		std::unique_lock<std::mutex> lck{ m_bkmutex };
		std::chrono::time_point<std::chrono::high_resolution_clock> last_progress_check = std::chrono::high_resolution_clock::now();

        auto ims = m_ims.lock();
        if (!ims) return;
        auto conn = ims->Connection();

		while (BackgroundThreadRunning) {
			//while (std::cv_status::timeout == m_bkcond.wait_for(lck, poll_interval))
			m_bkcond.wait_for(lck, poll_interval);

			if ((ims->Ctlr().Model() == "iMSP" && ims->Ctlr().GetVersion().revision <= 46) && (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - last_progress_check) > poll_interval))
			{
				// Controllers with FW revision greater than 46 use interrupt driven image notifications
				if (ImagePlaying) {

					HostReport *iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_Img_Ctrl);

					finishHandle.push(conn->SendMsg(*iorpt));
					delete iorpt;
				}

				/*if (!BackgroundThreadRunning || (latestProgress != -1)) break;
				if (!ImagePlaying && ImagePlaying_last) break;
				ImagePlaying_last = ImagePlaying;*/

				last_progress_check = std::chrono::high_resolution_clock::now();
			}
			
			if (!BackgroundThreadRunning) break;

			if (latestProgress != -1) {
				int progress = latestProgress;
				lck.unlock();
				m_Event->Trigger<int>(this, ImagePlayerEvents::POINT_PROGRESS, progress);
				lck.lock();
				latestProgress = -1;
			}

			if (!ImagePlaying && ImagePlaying_last) {
				lck.unlock();
				m_Event->Trigger<int>(this, ImagePlayerEvents::IMAGE_FINISHED, 0);
				lck.lock();
				BOOST_LOG_SEV(lg::get(), sev::info) << "Image Playback Finished";
			}
			ImagePlaying_last = ImagePlaying;
		}
	}

	/* IMAGE TABLE READER */
	ImageTableReader::ImageTableReader(std::shared_ptr<IMSSystem> ims) : m_ims(ims)
	{}

	ImageTableReader::~ImageTableReader()
	{}

	ImageTable ImageTableReader::Readback()
	{
		std::unique_ptr<ImageTable> imgt(new ImageTable());
		std::array<std::uint8_t, 16> uuid;

        return with_locked_value(m_ims, [&](std::shared_ptr<IMSSystem> ims) -> ImageTable
        {  
            // Read back ImageTable from iMS Controller
            if (!ims->Ctlr().IsValid()) return ImageTable();

            auto conn = ims->Connection();

            // Get number of entries in table
            std::size_t tbl_size;
            HostReport *iorpt;
            IMSController::Capabilities cap = ims->Ctlr().GetCap();
            if (!cap.FastImageTransfer) {
                // Only ever 1 entry in a Controller Lite
                tbl_size = 1;

                // Read Image Length
                int img_len = 0;
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_NumPts);
                DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
                delete iorpt;
                if (Resp.Done()) {
                    img_len = Resp.Payload<std::uint16_t>();
                }
                else return ImageTable();

                // Read Image UUID
                iorpt = new HostReport(HostReport::Actions::CTRLR_REG, HostReport::Dir::READ, CTRLR_REG_UUID);
                ReportFields f = iorpt->Fields();
                f.len = static_cast<std::uint16_t>(uuid.size());
                iorpt->Fields(f);
                Resp = conn->SendMsgBlocking(*iorpt);
                if (!Resp.Done()) {
                    delete iorpt;
                    return ImageTable();
                }
                else {
                    std::vector<std::uint8_t> v = Resp.Payload<std::vector<std::uint8_t>>();
                    //std::array<std::uint8_t, 16> uuid;
                    v.resize(16);
                    std::copy(v.begin(), v.end(), uuid.begin());
                    ImageTableEntry ite(0, 0, img_len, img_len * 20, 0, uuid, std::string("solo_img"));
                    imgt->push_back(ite);
                }
                delete iorpt;
            }
            else {
                iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::READ, 0);
                ReportFields f = iorpt->Fields();
                f.context = 4; // Get Image Table Size
                f.len = 2;
                iorpt->Fields(f);
                DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
                delete iorpt;
                if (Resp.Done()) {
                    tbl_size = Resp.Payload<std::uint16_t>();
                }
                else return ImageTable();

                for (ImageIndex idx = 0; idx < (int)tbl_size; idx++) {
                    iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::READ, static_cast<std::uint16_t>(idx));
                    f = iorpt->Fields();
                    f.context = static_cast < std::uint8_t>(HostReport::ImageIndexOperations::GET_ENTRY); 
                    f.len = 49;
                    iorpt->Fields(f);
                    Resp = conn->SendMsgBlocking(*iorpt);
                    delete iorpt;
                    if (Resp.Done() && !Resp.GeneralError()) {
                        ImageTableEntry ite(idx, Resp.Payload<std::vector<std::uint8_t>>());
                        imgt->push_back(ite);
                    }
                }
            }
            // Return by value
            return (*imgt);

        }).value_or(ImageTable());

	}

	/* IMAGE TABLE VIEWER */
	class ImageTableViewer::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem> ims) : m_ims(ims) {}

		std::weak_ptr<IMSSystem> m_ims;
	};

	ImageTableViewer::ImageTableViewer(std::shared_ptr<IMSSystem> ims) : p_Impl(new Impl(ims)) {}

	ImageTableViewer::~ImageTableViewer() { delete p_Impl; p_Impl = nullptr; }

	const int ImageTableViewer::Entries() const
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> int
        { 
    		return (int)ims->Ctlr().ImgTable().size();
        }).value_or(0);
	}

	const ImageTableEntry ImageTableViewer::operator[](const std::size_t idx) const
	{
        std::size_t entries = (std::size_t)this->Entries();

        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> ImageTableEntry
        {         
            ImageTable t = ims->Ctlr().ImgTable();
            if (idx < entries) {
                return *(std::next(t.begin(), idx));
            }
            else {
                return ImageTableEntry();
            }
        }).value_or(ImageTableEntry());
	}

	ImageTableViewer::const_iterator ImageTableViewer::begin() 
    {
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> ImageTableViewer::const_iterator
        {         
            return ims->Ctlr().ImgTable().begin(); 
        }).value();  // This can throw a bad_optional_access if m_ims is invalid
    }

	ImageTableViewer::const_iterator ImageTableViewer::end() { 
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> ImageTableViewer::const_iterator
        {   
            return ims->Ctlr().ImgTable().end(); 
        }).value();
    }

	bool ImageTableViewer::Erase(const std::size_t idx)
	{
        auto ims = p_Impl->m_ims.lock();
        if (!ims) return false;
        auto conn = ims->Connection();

		// Read back ImageTable from iMS Controller
		if (!ims->Ctlr().IsValid()) return false;

		HostReport* iorpt;
		IMSController::Capabilities cap = ims->Ctlr().GetCap();

		// Can't delete images in single image controllers. Just download a replacement image.
		if (!cap.FastImageTransfer) return false;

		if (idx < (std::size_t)this->Entries()) {
			ImageTable t = ims->Ctlr().ImgTable();
			ImageTableEntry ite = *(std::next(t.begin(), idx));

			iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::WRITE, 0);
			ReportFields f = iorpt->Fields();
			f.context = static_cast<std::uint8_t>(HostReport::ImageIndexOperations::CHECK_UUID);
			f.len = 16;
			iorpt->Fields(f);

			std::array<std::uint8_t, 16> uuid = ite.UUID();
			std::vector<std::uint8_t> data(uuid.begin(), uuid.begin() + 16);
			iorpt->Payload<std::vector<std::uint8_t>>(data);

			DeviceReport ioresp = conn->SendMsgBlocking(*iorpt);
			if (!ioresp.Done() || ioresp.GeneralError()) {
				// Most likely image UUID doesn't exist on Controller.  How did we get out of sync?
				BOOST_LOG_SEV(lg::get(), sev::error) << "Erase of image index " << idx << " [" << ite.Name() << "] requested but no matching UUID found on Controller.";

				delete iorpt;
				return false;
			}

			uint16_t img_idx = ioresp.Payload<uint16_t>();
			delete iorpt;

			iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::WRITE, img_idx);
			f = iorpt->Fields();
			f.context = static_cast<std::uint8_t>(HostReport::ImageIndexOperations::DEL_ENTRY);
			f.len = 0;
			iorpt->Fields(f);

			ioresp = conn->SendMsgBlocking(*iorpt);
			if (!ioresp.Done() || ioresp.GeneralError()) {
				// Erase failed. As image must exist (we checked for this above) a DMA operation must be in progress
				BOOST_LOG_SEV(lg::get(), sev::error) << "Erase of image index " << idx << " [" << ite.Name() << "] failed. Image Upload / Download in progress.";

				delete iorpt;
				return false;
			}
			delete iorpt;

			// Remove from Image Table
			t.erase(std::next(t.begin(), idx));
			const IMSController& c = ims->Ctlr();
			ims->Ctlr(IMSController(c.Model(), c.Description(), c.GetCap(), c.GetVersion(), t));

			return true;
		}
		BOOST_LOG_SEV(lg::get(), sev::warning) << "Erase of image index " << idx << " failed. Index not found in Image Table";
		return false;
	}

	bool ImageTableViewer::Erase(ImageTableEntry ite)
	{
		for (int i = 0; i < this->Entries(); i++)
		{
			if ((*this)[i].UUID() == ite.UUID())
				return this->Erase(i);
		}
		return false;
	}

	bool ImageTableViewer::Erase(ImageTableViewer::const_iterator it)
	{
		return this->Erase(std::distance(this->begin(), it));
	}

	bool ImageTableViewer::Clear()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {  
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();

            HostReport* iorpt;
            IMSController::Capabilities cap = ims->Ctlr().GetCap();

            // Can't delete images in single image controllers. Just download a replacement image.
            if (!cap.FastImageTransfer) return false;

            // Try an erase all command
            iorpt = new HostReport(HostReport::Actions::CTRLR_IMGIDX, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = static_cast<std::uint8_t>(HostReport::ImageIndexOperations::ERASE_ALL);
            f.len = 0;
            iorpt->Fields(f);

            DeviceReport ioresp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;
            if (!ioresp.Done() || ioresp.GeneralError()) {
                // Try clearing images individually
                int images_total = this->Entries();
                for (int i = 0; i < images_total; i++) {
                    if (!this->Erase((*this)[0])) {
                        BOOST_LOG_SEV(lg::get(), sev::error) << "Unable to Clear Image Table";
                        return false;
                    }
                }
            }
            // All done OK
            BOOST_LOG_SEV(lg::get(), sev::info) << "Image Table Cleared";

            // Reset local image table
            const IMSController& c = ims->Ctlr();
            ImageTable empty_table;
            ims->Ctlr(IMSController(c.Model(), c.Description(), c.GetCap(), c.GetVersion(), empty_table));

            return true;
        }).value_or(false);
	}

	std::ostream& operator <<(std::ostream& stream, const ImageTableViewer& itv) {

		for (int i = 0; i < itv.Entries(); i++) {
			stream << "Image[" << i << "] id:" << itv[i].Handle() <<
				" Addr: 0x" << std::setw(8) << std::setfill('0') << std::hex << itv[i].Address() <<
				" Points: " << std::dec << itv[i].NPts() <<
				" ByteLength: " << itv[i].Size() <<
				" Format Code: " << std::hex << itv[i].Format() <<
				" UUID: ";
			int j;
			stream << std::hex;
			for (j = 0; j < 4; j++) stream << std::setfill('0') << std::setw(2) << (int)itv[i].UUID()[j];
			stream << "-";
			for (j = 4; j < 6; j++) stream << std::setfill('0') << std::setw(2) << (int)itv[i].UUID()[j];
			stream << "-";
			for (j = 6; j < 8; j++) stream << std::setfill('0') << std::setw(2) << (int)itv[i].UUID()[j];
			stream << "-";
			for (j = 8; j < 10; j++) stream << std::setfill('0') << std::setw(2) << (int)itv[i].UUID()[j];
			stream << "-";
			for (j = 10; j < 16; j++) stream << std::setfill('0') << std::setw(2) << (int)itv[i].UUID()[j];
			stream << " Name: " << itv[i].Name();
			stream << std::dec << std::endl;
		}
		return stream;
	}


	/* SEQUENCE DOWNLOADER */
	class SequenceDownloadEventTrigger :
		public IEventTrigger
	{
	public:
		SequenceDownloadEventTrigger() { updateCount(DownloadEvents::Count); }
		~SequenceDownloadEventTrigger() {};
	};

	class SequenceDownload::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>, const ImageSequence&);
		~Impl();

        LazyWorker downloadWorker;

		std::weak_ptr<IMSSystem> m_ims;
		const ImageSequence& m_Seq;

		const std::unique_ptr<boost::container::deque<std::uint8_t>> m_seqdata;
		std::unique_ptr<SequenceDownloadEventTrigger> m_Event;

		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(SequenceDownload::Impl* pl) : m_parent(pl) { Init(); };
			void EventAction(void* sender, const int message, const int param);
			void Init() { busy.store(true); error.store(false); success_code = 0; error_code = 0; }
			bool IsBusy() const { return busy.load(); }
			bool HasError() const { return error.load(); }
			int SuccessCode() const { return success_code; }
			int ErrorCode() const { return error_code; }
		private:
			SequenceDownload::Impl* m_parent;
			std::atomic_bool busy;
			std::atomic_bool error;
			int error_code;
			int success_code;
		};
		ResponseReceiver* Receiver;

		DMASupervisor* dmah;

        bool downloadRequested{ false };
        void DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx);
 
		// Capability flags
		bool fast_seq_dl_supported{ false };
		bool large_seq_supported{ false };
	};

	SequenceDownload::Impl::Impl(std::shared_ptr<IMSSystem> ims, const ImageSequence& seq) :
		m_ims(ims), m_Seq(seq),
		m_seqdata(new boost::container::deque<std::uint8_t>()),
		Receiver(new ResponseReceiver(this)),
		m_Event(new SequenceDownloadEventTrigger()),
		dmah(new DMASupervisor()),
        downloadWorker([this](std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx) {
            DownloadWorkerLoop(running, cond, mtx);
        })
	{
		fast_seq_dl_supported = false;
		large_seq_supported = false;

		// Subscribe listener
		auto conn = ims->Connection();
		if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46) {
			conn->MessageEventSubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
			int IntrMask = (int)((1 << CTRLR_INTERRUPT_SEQDL_ERROR) | (1 << CTRLR_INTERRUPT_SEQDL_COMPLETE) | (1 << CTRLR_INTERRUPT_SEQDL_BUFFER_PROCESSED));

			HostReport* iorpt;
			iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 1);
			iorpt->Payload<int>(IntrMask);
			ReportFields f = iorpt->Fields();
			f.len = sizeof(IntrMask);
			iorpt->Fields(f);
			conn->SendMsg(*iorpt);
			delete iorpt;
		}

		conn->MessageEventSubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);
	}

	SequenceDownload::Impl::~Impl() 
	{
        with_locked(m_ims, [&](std::shared_ptr<IMSSystem> ims) {         
            // Unsubscribe listener
            auto conn = ims->Connection();
            if (ims->Ctlr().Model() != "iMSP" || ims->Ctlr().GetVersion().revision > 46) {
                conn->MessageEventUnsubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
                int IntrMask = ~(int)((1 << CTRLR_INTERRUPT_SEQDL_ERROR) | (1 << CTRLR_INTERRUPT_SEQDL_COMPLETE) | (1 << CTRLR_INTERRUPT_SEQDL_BUFFER_PROCESSED));

                HostReport* iorpt;
                iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
                iorpt->Payload<int>(IntrMask);
                ReportFields f = iorpt->Fields();
                f.len = sizeof(IntrMask);
                iorpt->Fields(f);
                ims->Connection()->SendMsg(*iorpt);
                delete iorpt;
            }

            conn->MessageEventUnsubscribe(MessageEvents::MEMORY_TRANSFER_COMPLETE, dmah);
        });

		delete dmah;
		delete Receiver;
	}

	SequenceDownload::SequenceDownload(std::shared_ptr<IMSSystem> ims, const ImageSequence& seq) : p_Impl(new Impl(ims, seq)) {}

	SequenceDownload::~SequenceDownload() { delete p_Impl; p_Impl = nullptr; }

	bool SequenceDownload::Download(bool asynchronous)
	{
        auto ims = p_Impl->m_ims.lock();
        if (!ims) return false;
        auto conn = ims->Connection();

		// Make sure Controller is present
		if (!ims->Ctlr().IsValid()) return false;
		int entries = (int)p_Impl->m_Seq.size();

		HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::READ, 0);
		ReportFields f = iorpt->Fields();
		f.context = 0;
		f.len = 3;
		iorpt->Fields(f);
		DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
		delete iorpt;

		if (!Resp.Done() || Resp.GeneralError() || Resp.Fields().len < 2) {}
		else {
			if (Resp.Fields().len > 2) {
				if (Resp.Payload<std::vector<std::uint8_t>>().at(2) != 0) {
					p_Impl->fast_seq_dl_supported = true;
				}
			}
			if (Resp.Fields().len > 3) {
				if (Resp.Payload<std::vector<std::uint8_t>>().at(3) != 0) {
					p_Impl->large_seq_supported = true;
				}
			}
		}

		if (p_Impl->fast_seq_dl_supported && asynchronous) {
            p_Impl->downloadWorker.start();

            int retries=10;
            while (retries)
            {
                std::unique_lock<std::mutex> lck{ p_Impl->downloadWorker.mutex(), std::try_to_lock };

                if (!lck.owns_lock()) {
                    if (!--retries) {
    					BOOST_LOG_SEV(lg::get(), sev::warning) << "Sequence Download busy - try again later";
                        return false;
                    }
                    // Mutex lock failed, Downloader must be busy, try again later
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                p_Impl->downloadRequested = true;
                lck.unlock();

                break;
            }

			p_Impl->downloadWorker.notify();
			return true;
		}

		/* Create a new sequence */
		/* Payload
		* [15:0] = Sequence UUID
		* [17:16] = number of entries in sequence
		*/
		iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
		f = iorpt->Fields();
		f.context = 0;
		iorpt->Fields(f);
		std::array<std::uint8_t, 16> uuid = p_Impl->m_Seq.GetUUID();
		std::vector<std::uint8_t> v(uuid.begin(), uuid.begin() + 16);
		v.push_back(entries & 0xff);
		v.push_back(entries >> 8);
		iorpt->Payload<std::vector<std::uint8_t>>(v);
		Resp = conn->SendMsgBlocking(*iorpt);
		if (!Resp.Done() || Resp.GeneralError()) {
			BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Download: Sequence creation failed.  Insufficient device memory for sequence entries";
			p_Impl->m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, 0);
			delete iorpt;
			return false;
		}
		delete iorpt;

		int entry = 0;
		for (ImageSequence::const_iterator it = p_Impl->m_Seq.cbegin(); it != p_Impl->m_Seq.cend(); ++it) {
			iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, entry);
			f = iorpt->Fields();

			const std::shared_ptr<SequenceEntry> seq_entry = *it;
			f.context = FormatSequenceEntry(*it, ims, v);
			if (f.context == 0) {
				delete iorpt;
				return false;
			}
			iorpt->Fields(f);

			iorpt->Payload<std::vector<std::uint8_t>>(v);
			Resp = conn->SendMsgBlocking(*iorpt);
			if (!Resp.Done() || Resp.GeneralError()) {
				BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Download: Sequence creation failed.  Error on sequence entry " << entry;
				p_Impl->m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, entry);
				delete iorpt;
				return false;
			}
			entry++;
			delete iorpt;
		}

		/* Commit Sequence */
		/* Payload:
		* [0] = termination type
		* [4:1] = termination value
		* [20:5] = termination tag (for Insert mode)
		*/
		iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
		f = iorpt->Fields();
		f.context = 2;
		iorpt->Fields(f);
		v.clear();
		v.push_back(static_cast<std::uint8_t>(p_Impl->m_Seq.TermAction()));
		std::uint32_t term_val = p_Impl->m_Seq.TermValue();
		v.push_back(static_cast<std::uint8_t>(term_val & 0xff));
		v.push_back(static_cast<std::uint8_t>((term_val >> 8) & 0xff));
		v.push_back(static_cast<std::uint8_t>((term_val >> 16) & 0xff));
		v.push_back(static_cast<std::uint8_t>((term_val >> 24) & 0xff));

		if (p_Impl->m_Seq.TermInsertBefore() != nullptr) {
			const std::array<uint8_t, 16> term_tag = p_Impl->m_Seq.TermInsertBefore()->GetUUID();
			v.insert(v.end(), term_tag.begin(), term_tag.begin() + 16);
		}

		iorpt->Payload<std::vector<std::uint8_t>>(v);
		Resp = conn->SendMsgBlocking(*iorpt);
		if (!Resp.Done() || Resp.GeneralError()) {
			BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Download: Sequence commit failed.";
			p_Impl->m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_FAIL_TRANSFER_ABORT, 0);
			delete iorpt;
			return false;
		}
		delete iorpt;

		return true;
	}


	void SequenceDownload::SequenceDownloadEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event->Subscribe(message, handler);
	}

	void SequenceDownload::SequenceDownloadEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event->Unsubscribe(message, handler);
	}

	// Sequence Downloading Thread
	void SequenceDownload::Impl::DownloadWorkerLoop(std::atomic<bool>& running, std::condition_variable& cond, std::mutex& mtx)
	{
		HostReport* iorpt;

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

			// Create Byte Vector
			std::uint32_t BytesInSequenceBuffer = FormatSequenceBuffer(m_Seq, ims, *m_seqdata);
			if (BytesInSequenceBuffer == 0) {
				BOOST_LOG_SEV(lg::get(), sev::error) << "FormatSequenceBuffer: Unable to create sequence buffer";
				m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, 0);
				continue;
			}

			/* Attempt to Create a new sequence */
			/* Payload
			* [15:0] = Sequence UUID
			* [17:16] = number of entries in sequence
			* [18] = Use fast fownload
			* [22:19] = Download size (bytes)
			* 
			* OR: (for large sequence supporting firmware)
			* 
			* [15:0] = Sequence UUID
			* [17:16] = set to zero
			* [18] = Use fast fownload = 1
			* [22:19] = Download size (bytes)
			* [26:23] = number of entries in sequence
			*
			*/
			int entries = (int)m_Seq.size();
			iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
			ReportFields f = iorpt->Fields();
			f.context = 0;
			iorpt->Fields(f);
			std::array<std::uint8_t, 16> uuid = m_Seq.GetUUID();
			std::vector<std::uint8_t> v(uuid.begin(), uuid.begin() + 16);
			if (entries <= (int)UINT16_MAX) {
			v.push_back(entries & 0xff);
			v.push_back(entries >> 8);
			}
			else {
				v.push_back(0);
				v.push_back(0);
			}
			v.push_back(1);
			v.push_back(BytesInSequenceBuffer & 0xff);
			v.push_back((BytesInSequenceBuffer >> 8) & 0xff);
			v.push_back((BytesInSequenceBuffer >> 16) & 0xff);
			v.push_back((BytesInSequenceBuffer >> 24) & 0xff);
			if (entries > (int)UINT16_MAX) {
				v.push_back(entries & 0xff);
				v.push_back((entries >> 8) & 0xff);
				v.push_back((entries >> 16) & 0xff);
				v.push_back((entries >> 24) & 0xff);
			}
			iorpt->Payload<std::vector<std::uint8_t>>(v);
			DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
			if (!Resp.Done() || Resp.GeneralError()) {
				BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Download (DownloadWorker): Sequence creation failed.  Insufficient device memory for sequence entries";
				m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, 0);
				delete iorpt;
				continue;
			}
			delete iorpt;

			if (Resp.Fields().len < 4) {
				BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Download (DownloadWorker): Sequence creation failed.  iMS failed to provide download memory address";
				m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, 0);
				continue;
			}
				
			std::uint32_t SeqMemoryAddress = Resp.Payload < std::uint32_t >();
			std::uint32_t SeqMemoryLength = 0x1000000;

			if (Resp.Fields().len >= 8) {
				SeqMemoryLength = Resp.Payload < std::vector <std::uint32_t> >().at(1);
			}
			BOOST_LOG_SEV(lg::get(), sev::trace) << "Download Buffer " << SeqMemoryLength << " bytes @ 0x" << std::hex << SeqMemoryAddress << std::dec;

			// Start memory download
			int tfr_size = 0;
			boost::container::deque<std::uint8_t>::iterator bufs = m_seqdata->begin();
			boost::container::deque<std::uint8_t>::iterator bufe = m_seqdata->end();

			//boost::container::deque<std::uint8_t> copy_buf;

			do {
				dmah->Reset();
				Receiver->Init();

				if (m_seqdata->size() > SeqMemoryLength) {
					if ((m_seqdata->size() - tfr_size) <= SeqMemoryLength)
					{
						bufe = m_seqdata->end();
					}
					else
					{
						bufe = bufs + SeqMemoryLength;
					}
				}
				boost::container::deque<std::uint8_t> copy_buf(boost::make_move_iterator(bufs), boost::make_move_iterator(bufe));
				conn->MemoryDownload(copy_buf, SeqMemoryAddress, 0, uuid);
				uuid[0]++;

				while (dmah->Busy()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}

				tfr_size += dmah->GetTransferredSize();
				BOOST_LOG_SEV(lg::get(), sev::info) << "Memory Download complete. Transferred " << tfr_size << " bytes.";

				while (Receiver->IsBusy()) {
	//				std::cout << "Busy" << std::endl;
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}

				if (Receiver->HasError()) {
					BOOST_LOG_SEV(lg::get(), sev::error) << "Error in sequence download";
					m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_ERROR, Receiver->ErrorCode());
						break;
				}

				bufs += dmah->GetTransferredSize();
			} while (tfr_size < m_seqdata->size());

			if (tfr_size > 0) {
				BOOST_LOG_SEV(lg::get(), sev::trace) << "Seq Download Commit";
				// Transfer complete.  Commit sequence
				/* Commit Sequence */
				/* Payload:
				* [0] = termination type
				* [4:1] = termination value
				* [20:5] = termination tag (for Insert mode)
				*/
				iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
				f = iorpt->Fields();
				f.context = 2;
				iorpt->Fields(f);
				v.clear();
				v.push_back(static_cast<std::uint8_t>(m_Seq.TermAction()));
				std::uint32_t term_val = m_Seq.TermValue();
				v.push_back(static_cast<std::uint8_t>(term_val & 0xff));
				v.push_back(static_cast<std::uint8_t>((term_val >> 8) & 0xff));
				v.push_back(static_cast<std::uint8_t>((term_val >> 16) & 0xff));
				v.push_back(static_cast<std::uint8_t>((term_val >> 24) & 0xff));

				if (m_Seq.TermInsertBefore() != nullptr) {
					const std::array<uint8_t, 16> term_tag = m_Seq.TermInsertBefore()->GetUUID();
					v.insert(v.end(), term_tag.begin(), term_tag.begin() + 16);
				}

				iorpt->Payload<std::vector<std::uint8_t>>(v);
				conn->SendMsg(*iorpt);
				delete iorpt;

				m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_FINISHED, tfr_size);
			}
			else {
//				std::cout << "Seq Download FAIL" << std::endl;
				// Problem with DMA Transfer
				m_Event->Trigger<int>((void*)this, DownloadEvents::DOWNLOAD_FAIL_TRANSFER_ABORT, tfr_size);
			}

			// Release lock, wait for next download trigger
			lck.unlock();
		}
	}

	void SequenceDownload::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
		switch (message)
		{
		case (MessageEvents::INTERRUPT_RECEIVED):
			unsigned int type = (param >> 16);
			int value = (param & 0xffff);
			switch (type) {
			case (CTRLR_INTERRUPT_SEQDL_ERROR): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Download ERROR Event Trigger";
//				std::cout << "Seq Download Error" << std::endl;
				error.store(true); busy.store(false); error_code = value;
			}
			break;
			case (CTRLR_INTERRUPT_SEQDL_COMPLETE): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Download Complete Event Trigger";
//				std::cout << "Seq Download Complete" << std::endl;
				error.store(false); busy.store(false); success_code = value;
			}
			break;
			case (CTRLR_INTERRUPT_SEQDL_BUFFER_PROCESSED): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Download Buffer Processed Event Trigger";
//				std::cout << "Seq Download Complete" << std::endl;
				error.store(false); busy.store(false);
			}
			break;
			}
			break;
		}
	}


	/* SEQUENCE MANAGER */
	class SequenceEventTrigger :
		public IEventTrigger
	{
	public:
		SequenceEventTrigger() { updateCount(SequenceEvents::Count); }
		~SequenceEventTrigger() {};
	};


	class SequenceManager::Impl
	{
	public:
		Impl(std::shared_ptr<IMSSystem>);
		~Impl();

		std::unique_ptr<SequenceEventTrigger> m_Event;
		class ResponseReceiver : public IEventHandler
		{
		public:
			ResponseReceiver(SequenceManager::Impl* pl) : m_parent(pl) {};
			void EventAction(void* sender, const int message, const int param);
			//void EventAction(void* sender, const int message, const int param, const int param2);
			void EventAction(void* sender, const int message, const int param, const std::vector<std::uint8_t> data);
		private:
			SequenceManager::Impl* m_parent;
		};
		ResponseReceiver* Receiver;

		std::weak_ptr<IMSSystem> m_ims;
	};

	SequenceManager::Impl::Impl(std::shared_ptr<IMSSystem> ims) :
		m_Event(new SequenceEventTrigger()),
		Receiver(new ResponseReceiver(this)),
		m_ims(ims)
	{
		// Subscribe listener
		auto conn = ims->Connection();
		if (ims->Ctlr().GetVersion().revision > 46) {
			conn->MessageEventSubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
		}

	}

	SequenceManager::Impl::~Impl() 
	{
        with_locked(m_ims, [&](std::shared_ptr<IMSSystem> ims) {               
            // Unsubscribe listener
            auto conn = ims->Connection();
            if (ims->Ctlr().GetVersion().revision > 46)
                conn->MessageEventUnsubscribe(MessageEvents::INTERRUPT_RECEIVED, Receiver);
        });

		delete Receiver;
	}

	SequenceManager::SequenceManager(std::shared_ptr<IMSSystem> ims) : p_Impl(new Impl(ims)) {}

	SequenceManager::~SequenceManager() { delete p_Impl; p_Impl = nullptr; }

	bool SequenceManager::StartSequenceQueue(const SequenceManager::SeqConfiguration& cfg, ImageTrigger start_trig)
	{
        auto ims = p_Impl->m_ims.lock();
        if (!ims) return false;
        auto conn = ims->Connection();

		// Make sure Controller & Synthesiser are present
		if (!ims->Ctlr().IsValid()) return false;
		if (!ims->Synth().IsValid()) return false;

		// Sequences require interrupts, which are only available on Controller firmware versions > 46
		if (ims->Ctlr().GetVersion().revision <= 46) {
			BOOST_LOG_SEV(lg::get(), sev::error) << "Sequence Playback failed: Require Controller F/W revision > 46. Yours = " << 
				ims->Ctlr().GetVersion().revision;
			return false;
		}

		IMSController::Capabilities cap = ims->Ctlr().GetCap();

		if (!cap.FastImageTransfer) return false;

		// Set ImageFormat (v1.8 - use auto mode only for now)
		{
			HostReport* iorpt = new HostReport(HostReport::Actions::SYNTH_REG, HostReport::Dir::WRITE, SYNTH_REG_Img_FormatLo);
			iorpt->Payload<uint32_t>(0x80000fff);

			if (NullMessage == conn->SendMsg(*iorpt))
			{
				delete iorpt;
				return false;
			}
			delete iorpt;
		}

		{
			HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Start);
			std::vector<std::uint8_t> v;

			// Set ImgModes
			std::uint8_t TFR{ 0 };
			if (cfg.int_ext == PointClock::EXTERNAL)
			{
				TFR |= 4;
			}
			switch (cfg.trig)
			{
			case ImageTrigger::CONTINUOUS: TFR |= 3; break;
			case ImageTrigger::EXTERNAL: TFR |= 1; break;
			case ImageTrigger::HOST: TFR |= 2; break;
			case ImageTrigger::POST_DELAY: TFR |= 0; break;
			}
			v.push_back(TFR);

			std::uint8_t Pol = 0;
			Pol |= (cfg.clk_pol == Polarity::INVERSE) ? 1 : 0;
			Pol |= (cfg.trig_pol == Polarity::INVERSE) ? 2 : 0;
			v.push_back(Pol);

			std::uint8_t Play = 0;
			switch (start_trig)
			{
			case ImageTrigger::EXTERNAL: Play |= 2; break;
			case ImageTrigger::HOST: Play |= 4; break;
			case ImageTrigger::CONTINUOUS: Play |= 1; break;
			default: Play |= 1; break;
			}
			v.push_back(Play);

			iorpt->Payload < std::vector<std::uint8_t>>(v);
			conn->SendMsg(*iorpt);
			delete iorpt;
		}

		BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Queue started";

		return true;
	}

	void SequenceManager::SendHostTrigger()
	{
        with_locked(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) {  
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return;
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_USR_Trig);
            ims->Connection()->SendMsg(*iorpt);
            delete iorpt;
        });
	}   

	bool SequenceManager::Stop(ImagePlayer::StopStyle style)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            IMSController::Capabilities cap = ims->Ctlr().GetCap();
            if (!cap.FastImageTransfer) {
                return false;
            }

            auto conn = ims->Connection();
            HostReport* iorpt;

            iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Stop);
            if (ImagePlayer::StopStyle::IMMEDIATELY == style)
            {
                // Force Stop Image
                BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Force Stop request sent";
                iorpt->Payload<std::uint8_t>(0);
            }
            else {
                // Gracefully Stop Image at end of currenr repeat
                BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Graceful Stop request sent";
                iorpt->Payload<std::uint8_t>(1);
            }
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false); 
	}

	bool SequenceManager::StopAtEndOfSequence()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {        
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            IMSController::Capabilities cap = ims->Ctlr().GetCap();
            if (!cap.FastImageTransfer) {
                return false;
            }

            auto conn = ims->Connection();
            HostReport* iorpt;

            iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Stop);

            BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Programmed Stop (end of sequence) request sent";
            iorpt->Payload<std::uint8_t>(2);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);        
	}

	bool SequenceManager::Pause(ImagePlayer::StopStyle style)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {           
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            IMSController::Capabilities cap = ims->Ctlr().GetCap();
            if (!cap.FastImageTransfer) {
                return false;
            }

            auto conn = ims->Connection();
            HostReport* iorpt;

            iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Pause);
            if (ImagePlayer::StopStyle::IMMEDIATELY == style)
            {
                // Force Stop Image
                BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Immediate Pause request sent";
                iorpt->Payload<std::uint8_t>(0);
            }
            else {
                // Gracefully Stop Image at end of currenr repeat
                BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Pause after Entry request sent";
                iorpt->Payload<std::uint8_t>(1);
            }
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);          
	}

	bool SequenceManager::Resume()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            // Make sure Controller & Synthesiser are present
            if (!ims->Ctlr().IsValid()) return false;
            if (!ims->Synth().IsValid()) return false;

            IMSController::Capabilities cap = ims->Ctlr().GetCap();
            if (!cap.FastImageTransfer) {
                return false;
            }

            auto conn = ims->Connection();
            HostReport* iorpt;

            iorpt = new HostReport(HostReport::Actions::CTRLR_SEQPLAY, HostReport::Dir::WRITE, CTRLR_SEQPLAY_Seq_Restart);
            BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Resume request sent";
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);   
	}

	std::uint16_t SequenceManager::QueueCount()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> std::uint16_t
        {         
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return -1;
            auto conn = ims->Connection();
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::READ, 0);
            ReportFields f = iorpt->Fields();
            f.context = 0;
            f.len = 2;
            iorpt->Fields(f);
            DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;

            if (!Resp.Done() || Resp.GeneralError() || Resp.Fields().len < 2) return 0;
            else {
                return Resp.Payload<std::uint16_t>();
            }
        }).value_or(0);         
	}

	bool SequenceManager::GetSequenceUUID(int index, std::array<std::uint8_t, 16>& uuid)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {        
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::READ, index);
            ReportFields f = iorpt->Fields();
            f.context = 1;
            f.len = 16;
            iorpt->Fields(f);
            DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;

            if (!Resp.Done() || Resp.GeneralError() || Resp.Fields().len < 16) return false;
            else {
                std::vector<std::uint8_t> v = Resp.Payload<std::vector<std::uint8_t>>();
                v.resize(16);
                std::copy(v.cbegin(), v.cend(), uuid.begin());
            }
            return true;
        }).value_or(false);          
	}

	bool SequenceManager::QueueClear()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {          
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 5;
            f.len = 0;
            iorpt->Fields(f);
            DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;

            if (!Resp.Done() || Resp.GeneralError()) return false;
            return true;
        }).value_or(false); 
	}

	bool SequenceManager::RemoveSequence(const std::array<std::uint8_t, 16>& uuid)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 4;
            f.len = 16;
            iorpt->Fields(f);
            std::vector<std::uint8_t> v;
            v.assign(uuid.cbegin(), uuid.cbegin() + 16);

            iorpt->Payload<std::vector<std::uint8_t>>(v);
            DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;

            if (!Resp.Done() || Resp.GeneralError()) return false;
            return true;
        }).value_or(false); 
	}

	bool SequenceManager::RemoveSequence(const ImageSequence& seq)
	{
		std::array<std::uint8_t, 16> seq_uuid = seq.GetUUID();
		return this->RemoveSequence(seq_uuid);
	}

	bool SequenceManager::UpdateTermination(const std::array<std::uint8_t, 16>& uuid, SequenceTermAction term, int term_val)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {          
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport * iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 3;
            f.len = 21;
            iorpt->Fields(f);
            std::vector<std::uint8_t> v;
            v.assign(uuid.cbegin(), uuid.cbegin() + 16);
            v.push_back(static_cast<std::uint8_t>(term));
            v.push_back(static_cast<std::uint8_t>(term_val & 0xff));
            v.push_back(static_cast<std::uint8_t>((term_val >> 8) & 0xff));
            v.push_back(static_cast<std::uint8_t>((term_val >> 16) & 0xff));
            v.push_back(static_cast<std::uint8_t>((term_val >> 24) & 0xff));

            iorpt->Payload<std::vector<std::uint8_t>>(v);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false); 
	}

	bool SequenceManager::UpdateTermination(const std::array<std::uint8_t, 16>& uuid, SequenceTermAction term, const std::array<std::uint8_t, 16>& term_uuid)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {          
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 3;
            f.len = 37;
            iorpt->Fields(f);
            std::vector<std::uint8_t> v;
            v.assign(uuid.cbegin(), uuid.cbegin() + 16);
            v.push_back(static_cast<std::uint8_t>(term));
            v.insert(v.end(), { 0, 0, 0, 0 });
            v.insert(v.end(), term_uuid.begin(), term_uuid.begin() + 16);

            iorpt->Payload<std::vector<std::uint8_t>>(v);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);         
	}

	bool SequenceManager::UpdateTermination(ImageSequence& seq, SequenceTermAction term, int term_val)
	{
		std::array<std::uint8_t, 16> seq_uuid = seq.GetUUID();
		seq.OnTermination(term, term_val);
		return this->UpdateTermination(seq_uuid, term, term_val);
	}

	bool SequenceManager::UpdateTermination(ImageSequence& seq, SequenceTermAction term, const ImageSequence* term_seq)
	{
		if (term_seq != nullptr) {
			std::array<std::uint8_t, 16> seq_uuid = seq.GetUUID();
			std::array<std::uint8_t, 16> term_seq_uuid = term_seq->GetUUID();
			seq.OnTermination(term, term_seq);
			return this->UpdateTermination(seq_uuid, term, term_seq_uuid);
		}
		else return false;
	}

	bool SequenceManager::MoveSequence(const ImageSequence& dest, const ImageSequence& src)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {         
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 9;
            f.len = 32;
            iorpt->Fields(f);
            std::vector<std::uint8_t> v;
            auto s = src.GetUUID();
            auto d = dest.GetUUID();
            v.assign(d.cbegin(), d.cbegin() + 16);
            v.insert(v.end(), s.cbegin(), s.cbegin() + 16);
            iorpt->Payload<std::vector<std::uint8_t>>(v);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);        
	}

	bool SequenceManager::MoveSequenceToEnd(const ImageSequence& src)
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {          
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::WRITE, 0);
            ReportFields f = iorpt->Fields();
            f.context = 10;
            f.len = 16;
            iorpt->Fields(f);
            std::vector<std::uint8_t> v;
            auto s = src.GetUUID();
            v.assign(s.cbegin(), s.cbegin() + 16);
            iorpt->Payload<std::vector<std::uint8_t>>(v);
            if (NullMessage == conn->SendMsg(*iorpt))
            {
                delete iorpt;
                return false;
            }
            delete iorpt;
            return true;
        }).value_or(false);           
	}

	bool SequenceManager::GetCurrentPosition()
	{
        return with_locked_value(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) -> bool
        {          
            // Make sure Controller is present
            if (!ims->Ctlr().IsValid()) return false;
            auto conn = ims->Connection();
            HostReport* iorpt = new HostReport(HostReport::Actions::CTRLR_SEQQUEUE, HostReport::Dir::READ, 0);
            ReportFields f = iorpt->Fields();
            f.context = 8;
            f.len = 21;
            iorpt->Fields(f);
            DeviceReport Resp = conn->SendMsgBlocking(*iorpt);
            delete iorpt;

            if (!Resp.Done() || Resp.GeneralError() || Resp.Fields().len < 21) return false;
            else {
                int value = 0;
                auto d = Resp.Payload<std::vector<uint8_t>>();
                value |= ((int)d[1]);
                value |= ((int)d[2] << 8);
                value |= ((int)d[3] << 16);
                value |= ((int)d[4] << 24);

                std::vector<uint8_t> uuid(d.begin() + 5, d.begin() + 21);
                p_Impl->m_Event->Trigger<int, std::vector<std::uint8_t>>((void*)p_Impl, SequenceEvents::SEQUENCE_POSITION, value, uuid);

                return true;
            }
        }).value_or(false);           
	}

	// Notify application when something happens
	void SequenceManager::SequenceEventSubscribe(const int message, IEventHandler* handler)
	{
		p_Impl->m_Event->Subscribe(message, handler);
        with_locked(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) {             
            if (ims->Ctlr().GetVersion().revision >= 38) {
                int IntrMask;
                switch (message) {
                case SequenceEvents::SEQUENCE_START: IntrMask = (int)(1 << CTRLR_INTERRUPT_SEQUENCE_START); break;
                case SequenceEvents::SEQUENCE_FINISHED: IntrMask = (int)(1 << CTRLR_INTERRUPT_SEQUENCE_FINISHED); break;
                case SequenceEvents::SEQUENCE_ERROR: IntrMask = (int)(1 << CTRLR_INTERRUPT_SEQUENCE_ERROR); break;
                case SequenceEvents::SEQUENCE_TONE: IntrMask = (int)(1 << CTRLR_INTERRUPT_TONE_START); break;
                default: IntrMask = 0;
                }
                HostReport *iorpt;
                iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 1);
                iorpt->Payload<int>(IntrMask);
                ReportFields f = iorpt->Fields();
                f.len = sizeof(IntrMask);
                iorpt->Fields(f);
                ims->Connection()->SendMsg(*iorpt);
                delete iorpt;
            }
        });
	}

	void SequenceManager::SequenceEventUnsubscribe(const int message, const IEventHandler* handler)
	{
		p_Impl->m_Event->Unsubscribe(message, handler);
        with_locked(p_Impl->m_ims, [&](std::shared_ptr<IMSSystem> ims) {   
            int IntrMask;
            if (!p_Impl->m_Event->Subscribers(message)) {
                switch (message) {
                case SequenceEvents::SEQUENCE_START: IntrMask = (int)~(1 << CTRLR_INTERRUPT_SEQUENCE_START); break;
                case SequenceEvents::SEQUENCE_FINISHED: IntrMask = (int)~(1 << CTRLR_INTERRUPT_SEQUENCE_FINISHED); break;
                case SequenceEvents::SEQUENCE_ERROR: IntrMask = (int)~(1 << CTRLR_INTERRUPT_SEQUENCE_ERROR); break;
                case SequenceEvents::SEQUENCE_TONE: IntrMask = (int)~(1 << CTRLR_INTERRUPT_TONE_START); break;
                default: IntrMask = 0;
                }
                HostReport *iorpt;
                iorpt = new HostReport(HostReport::Actions::CTRLR_INTREN, HostReport::Dir::WRITE, 0);
                iorpt->Payload<int>(IntrMask);
                ReportFields f = iorpt->Fields();
                f.len = sizeof(IntrMask);
                iorpt->Fields(f);
                ims->Connection()->SendMsg(*iorpt);
                delete iorpt;
            }
        });        
	}

	void SequenceManager::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param)
	{
		switch (message)
		{
		case (MessageEvents::INTERRUPT_RECEIVED) :
			unsigned int type = (param >> 16);
			int value = (param & 0xffff);
			switch (type) {
			case (CTRLR_INTERRUPT_SEQUENCE_FINISHED): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Finished Event Trigger";
				m_parent->m_Event->Trigger<int>((void*)m_parent, SequenceEvents::SEQUENCE_FINISHED, value); break;
			}
			case (CTRLR_INTERRUPT_SEQUENCE_ERROR): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Error Event Trigger";
				m_parent->m_Event->Trigger<int>((void*)m_parent, SequenceEvents::SEQUENCE_ERROR, value); break;
			}
			case (CTRLR_INTERRUPT_TONE_START): {
				BOOST_LOG_SEV(lg::get(), sev::debug) << "Sequence Tone Entry Event Trigger";
				m_parent->m_Event->Trigger<int>((void*)m_parent, SequenceEvents::SEQUENCE_TONE, value); break;
				}
			}
		}
	}

/*	void SequenceManager::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param, const int param2)
	{
		switch (message)
		{
		case (MessageEvents::INTERRUPT_RECEIVED) :
			unsigned int type = (param >> 16);
			int value = (param & 0xffff);
			switch (type) {
			case (CTRLR_INTERRUPT_SEQUENCE_IMAGE_REPEAT) : m_parent->m_Event->Trigger<int, int>((void *)m_parent, SequenceEvents::SEQUENCE_IMAGE_REPEAT, value, param2); break;
			}
		}
	}*/

	void SequenceManager::Impl::ResponseReceiver::EventAction(void* sender, const int message, const int param, const std::vector<std::uint8_t> data)
	{
		switch (message)
		{
		case (MessageEvents::INTERRUPT_RECEIVED) :
			unsigned int type = (param >> 16);
			int value = (param & 0xffff);
			switch (type) {
			case (CTRLR_INTERRUPT_SEQUENCE_START): {
					BOOST_LOG_SEV(lg::get(), sev::info) << "Sequence Start Event Trigger";
					m_parent->m_Event->Trigger<int, std::vector<std::uint8_t>>((void*)m_parent, SequenceEvents::SEQUENCE_START, value, data); break;
				}
			}
		}
	}
}
