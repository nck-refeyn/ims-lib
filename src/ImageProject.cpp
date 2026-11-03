/*-----------------------------------------------------------------------------
/ Title      : Isomet Image Project Implementation
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/ImageOps/src/ImageProject.cpp $
/ Author     : $Author: dave $
/ Company    : Isomet (UK) Ltd
/ Created    : 2015-04-09
/ Last update: $Date: 2020-06-05 07:45:07 +0100 (Fri, 05 Jun 2020) $
/ Platform   :
/ Standard   : C++11
/ Revision   : $Rev: 443 $
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

#include "ImageProject.h"
#include "SignalPath.h"
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include <libxml/xmlreader.h>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cctype>
#include <memory>

#define MY_ENCODING "UTF-8"

namespace iMS {

	static inline bool GetStartElement(xmlTextReaderPtr reader, xmlChar * expectedName);
	static inline bool GetEndElement(xmlTextReaderPtr reader, xmlChar * expectedName);
	static inline bool GetNodeWithStringValue(xmlTextReaderPtr reader, xmlChar * expectedName, std::string& StrValue);
	static inline bool GetNodeWithIntValue(xmlTextReaderPtr reader, xmlChar * expectedName, int& IntValue, int base = 10);
	static inline bool GetNodeWithDoubleValue(xmlTextReaderPtr reader, xmlChar * expectedName, double& FloatValue);
	static inline bool GetNodeWithBoolValue(xmlTextReaderPtr reader, xmlChar * expectedName, bool& BoolValue);
	static inline bool GetAttributeWithStringValue(xmlTextReaderPtr reader, xmlChar * expectedName, std::string& StrValue);
	static inline bool GetAttributeWithIntValue(xmlTextReaderPtr reader, xmlChar * expectedName, int& IntValue, int base = 10);
	static inline bool GetAttributeWithDoubleValue(xmlTextReaderPtr reader, xmlChar * expectedName, double& FloatValue);

	static inline std::string UUIDToStringConverter(std::array<std::uint8_t, 16> uuid);

	typedef struct IIPVersion_t
	{
		int major;
		int minor;
	} IIPVersion;

	class ImageProject::Impl {
	public:
		Impl() {
			m_freeImgList.Name() = std::string("Free Images");
		}

		ImageGroupList m_imgGroupList;
		CompensationFunctionList m_cFuncList;
		ToneBufferList m_toneBufList;
		ImageGroup m_freeImgList;

		bool processFile(xmlTextReaderPtr reader, const IIPVersion& ver);

		static const IIPVersion current;
	};

	// Define Current Image Project file format version
	const IIPVersion ImageProject::Impl::current = { 2, 2 };

	ImageProject::ImageProject() : p_Impl(new Impl()) {}

	/// \brief Implicit Load From File Constructor
	ImageProject::ImageProject(const std::string& fileName) : ImageProject()
	{
        this->Load(fileName);
	}

	ImageGroupList& ImageProject::ImageGroupContainer() { return p_Impl->m_imgGroupList; }
	const ImageGroupList& ImageProject::ImageGroupContainer() const { return p_Impl->m_imgGroupList; }

	CompensationFunctionList& ImageProject::CompensationFunctionContainer() { return p_Impl->m_cFuncList; }
	const CompensationFunctionList& ImageProject::CompensationFunctionContainer() const { return p_Impl->m_cFuncList; }

	ToneBufferList& ImageProject::ToneBufferContainer() { return p_Impl->m_toneBufList; }
	const ToneBufferList& ImageProject::ToneBufferContainer() const { return p_Impl->m_toneBufList; }

	ImageGroup& ImageProject::FreeImageContainer() { return p_Impl->m_freeImgList; }
	const ImageGroup& ImageProject::FreeImageContainer() const { return p_Impl->m_freeImgList; }

	void ImageProject::Clear()
	{
		p_Impl->m_cFuncList.clear();
		p_Impl->m_imgGroupList.clear();
		p_Impl->m_toneBufList.clear();
		p_Impl->m_freeImgList.clear();
	}

	bool ImageProject::Save(const std::string& fileName)
	{
		// If given a .xml extension filename, output raw textual XML data, otherwise maximally compress
		int file_compress = 9;
		std::string file_extension = boost::filesystem::path(fileName).extension().string();
		std::transform(file_extension.begin(),
				file_extension.end(),
				file_extension.begin(),
				[](unsigned char c){ return std::tolower(c); });
		if (file_extension == ".xml") {
			file_compress = 0;
		}

		xmlTextWriterPtr writer;

		/* Create a new XmlWriter for uri, with compression. */
		writer = xmlNewTextWriterFilename(fileName.c_str(), file_compress);
		if (writer == NULL) {
			return false;
		}

		/* Set a readable amount of white space */
		//xmlTextWriterSetIndent(writer, 4);

		/* Start the document with the xml default for the version,
		* encoding UTF-8 and the default for the standalone
		* declaration. */
		if (0 > xmlTextWriterStartDocument(writer, NULL, MY_ENCODING, NULL)) return false;

		/* Copyright statement */
		if (0 > xmlTextWriterWriteComment(writer, BAD_CAST "(c) 2017-25 Isomet (UK) Ltd, All rights reserved")) return false;


		/* Start an element to be the master root element */
		if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImageProject")) return false;
		if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "version", "v%d.%d", p_Impl->current.major, p_Impl->current.minor)) return false;

		/* Add inline elements detailing the number of Image Folders, Compensation Tables and Tone Buffers */
		if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImageGroupCount")) return false;
		if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Count", "%d", p_Impl->m_imgGroupList.size())) return false;
		if (0 > xmlTextWriterEndElement(writer)) return false;
		if (0 > xmlTextWriterStartElement(writer, BAD_CAST "CompensationFunctionCount")) return false;
		if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Count", "%d", p_Impl->m_cFuncList.size())) return false;
		if (0 > xmlTextWriterEndElement(writer)) return false;
		if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ToneBufferCount")) return false;
		if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Count", "%d", p_Impl->m_toneBufList.size())) return false;
		if (0 > xmlTextWriterEndElement(writer)) return false;
		if (0 > xmlTextWriterStartElement(writer, BAD_CAST "FreeImageCount")) return false;
		if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Count", "%d", p_Impl->m_freeImgList.size())) return false;
		if (0 > xmlTextWriterEndElement(writer)) return false;

		/* Iterate through each Image Group writing to disk */
		for (ImageGroupList::const_iterator it = p_Impl->m_imgGroupList.cbegin(); it != p_Impl->m_imgGroupList.cend(); ++it) {
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImageGroup")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Name", "%s", it->Name().c_str())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Created", "%d", it->CreatedTime())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Modified", "%d", it->ModifiedTime())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Author", "%s", it->Author().c_str())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Company", "%s", it->Company().c_str())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Revision", "%s", it->Revision().c_str())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Description", "%s", it->Description().c_str())) return false;

			/* Write the Sequence Table */
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "SequenceTable")) return false;
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ArrayOfSeqEntry")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Size", "%d", it->Sequence().size())) return false;
			for (ImageSequence::const_iterator seq_it = it->Sequence().cbegin(); seq_it != it->Sequence().cend(); ++seq_it)
			{
				const std::shared_ptr<SequenceEntry> entry = *seq_it;
				const std::shared_ptr<ImageSequenceEntry> img_entry = std::dynamic_pointer_cast<ImageSequenceEntry>(entry);

				if (img_entry == nullptr)
					continue;

				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "SeqEntry")) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Image", "%s", UUIDToStringConverter(entry->UUID()).c_str())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "RptType", "%d", img_entry->RptType())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Rpts", "%d", entry->NumRpts())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncDel", "%0.7f", entry->SyncOutDelay().count())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "IntOsc", "%0.7f", static_cast<double>(static_cast<iMS::Frequency>(img_entry->IntOsc())))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ImgDelay", "%0.7f", img_entry->PostImgDelay().count())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ClkDiv", "%d", img_entry->ExtDiv())) return false;

				/* Close the element named SeqEntry. */
				if (0 > xmlTextWriterEndElement(writer)) return false;
			}
			/* Close the element named ArrayOfSeqEntry. */
			if (0 > xmlTextWriterEndElement(writer)) return false;

			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SequenceTermination", "%d", it->Sequence().TermAction())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SequenceTerminationIndex", "%d", it->Sequence().TermValue())) return false;
			/* Close the element named SequenceTable. */
			if (0 > xmlTextWriterEndElement(writer)) return false;

			/* Write the Image Table */
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImageTable")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Count", "%d", it->Size())) return false;

			for (ImageGroup::const_iterator img_it = it->cbegin(); img_it != it->cend(); ++img_it)
			{
				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "Image")) return false;
				if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Name", "%s", img_it->Name().c_str())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ImageUUID", "%s", UUIDToStringConverter(img_it->GetUUID()).c_str())) return false;
				if (0 > xmlTextWriterWriteElement(writer, BAD_CAST "NumChannels", BAD_CAST "4")) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Description", "%s", img_it->Description().c_str())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "IntOsc", "%0.7f",static_cast<double>(static_cast<iMS::Frequency>(img_it->ClockRate())))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ClkDiv", "%d", img_it->ExtClockDivide())) return false;

				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ArrayOfImgPoints")) return false;
				if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Size", "%d", img_it->Size())) return false;
				FAP *fap = new FAP();
				for (Image::const_iterator pt_it = img_it->cbegin(); pt_it != img_it->cend(); ++pt_it)
				{
					if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImgPoint")) return false;

					RFChannel ch = 1;
					do { 
						*fap = pt_it->GetFAP(ch);
						if (0 > xmlTextWriterStartElement(writer, BAD_CAST "FAP")) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ch", "%d", static_cast<int>(ch))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Freq", "%0.3f", static_cast<double>(static_cast<iMS::Frequency>(fap->freq)))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ampl", "%0.3f", static_cast<double>(fap->ampl))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Phase", "%0.3f", static_cast<double>(fap->phase))) return false;
						if (0 > xmlTextWriterEndElement(writer)) return false;
					} while (ch++ != 4);

					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncDig", "%d", pt_it->GetSyncD())) return false;
					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncAnlg1", "%0.4f", pt_it->GetSyncA(0))) return false;
					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncAnlg2", "%0.4f", pt_it->GetSyncA(1))) return false;

					/* Close the element named ImgPoint. */
					if (0 > xmlTextWriterEndElement(writer)) return false;
				}
				delete fap;

				/* Close the element named ArrayOfImgPoints. */
				if (0 > xmlTextWriterEndElement(writer)) return false;

				/* Close the element named Image. */
				if (0 > xmlTextWriterEndElement(writer)) return false;
			}

			/* Close the element named ImageTable. */
			if (0 > xmlTextWriterEndElement(writer)) return false;

			/* Close the element named ImageGroup. */
			if (0 > xmlTextWriterEndElement(writer)) return false;
		}

		/* Iterate through each Compensation Table writing to disk */
		for (CompensationFunctionList::iterator it = p_Impl->m_cFuncList.begin(); it != p_Impl->m_cFuncList.end(); ++it) {
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "CompensationFunction")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Name", "%s", it->Name().c_str())) return false;
			if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Modified", "%d", it->ModifiedTime())) return false;

			/* Write the Table of Compensation Point Specifications */
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ArrayOfCompensationPointSpecification")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Size", "%d", it->size())) return false;
			for (CompensationFunction::iterator cps_it = it->begin(); cps_it != it->end(); ++cps_it)
			{
				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "CompensationPointSpecification")) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Freq", "%0.3f", static_cast<double>(static_cast<Frequency>(cps_it->Freq())))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Ampl", "%0.3f", static_cast<double>(cps_it->Spec().Amplitude()))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Phase", "%0.3f", static_cast<double>(cps_it->Spec().Phase()))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncDig", "%d", cps_it->Spec().SyncDig())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncAnlg", "%0.4f", cps_it->Spec().SyncAnlg())) return false;
				if (0 > xmlTextWriterEndElement(writer)) return false;
			}
			/* Close the element named ArrayOfCompensationPointSpecification. */
			if (0 > xmlTextWriterEndElement(writer)) return false;

			/* Close the element named CompensationFunction. */
			if (0 > xmlTextWriterEndElement(writer)) return false;
		}

		/* Iterate through each Tone Buffer writing to disk */
		for (ToneBufferList::const_iterator it = p_Impl->m_toneBufList.cbegin(); it != p_Impl->m_toneBufList.cend(); ++it) {
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ToneBuffer")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Name", "%s", it->Name().c_str())) return false;
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ArrayOfTBEntry")) return false;
			if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Size", "%d", it->Size())) return false;
			FAP *fap = new FAP();
			for (ToneBuffer::const_iterator tb_it = it->cbegin(); tb_it != it->cend(); ++tb_it)
			{
				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "TBEntry")) return false;

				RFChannel ch = 1;
				do {
					*fap = tb_it->GetFAP(ch);
					if (0 > xmlTextWriterStartElement(writer, BAD_CAST "FAP")) return false;
					if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ch", "%d", static_cast<int>(ch))) return false;
					if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Freq", "%0.3f", static_cast<double>(static_cast<iMS::Frequency>(fap->freq)))) return false;
					if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ampl", "%0.3f", static_cast<double>(fap->ampl))) return false;
					if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Phase", "%0.3f", static_cast<double>(fap->phase))) return false;
					if (0 > xmlTextWriterEndElement(writer)) return false;
				} while (ch++ != 4);

				/* Close the element named TBEntry. */
				if (0 > xmlTextWriterEndElement(writer)) return false;
			}
			delete fap;

			/* Close the element named ArrayOfTBEntry. */
			if (0 > xmlTextWriterEndElement(writer)) return false;

			/* Close the element named ToneBuffer. */
			if (0 > xmlTextWriterEndElement(writer)) return false;
		}

		if (p_Impl->m_freeImgList.size() > 0) {
			if (0 > xmlTextWriterStartElement(writer, BAD_CAST "FreeImages")) return false;
			
			for (ImageGroup::const_iterator img_it = p_Impl->m_freeImgList.cbegin(); img_it != p_Impl->m_freeImgList.cend(); ++img_it)
			{
				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "Image")) return false;
				if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Name", "%s", img_it->Name().c_str())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ImageUUID", "%s", UUIDToStringConverter(img_it->GetUUID()).c_str())) return false;
				if (0 > xmlTextWriterWriteElement(writer, BAD_CAST "NumChannels", BAD_CAST "4")) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "Description", "%s", img_it->Description().c_str())) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "IntOsc", "%0.7f", static_cast<double>(static_cast<iMS::Frequency>(img_it->ClockRate())))) return false;
				if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "ClkDiv", "%d", static_cast<int>(img_it->ExtClockDivide()))) return false;

				if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ArrayOfImgPoints")) return false;
				if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Size", "%d", img_it->Size())) return false;
				FAP *fap = new FAP();
				for (Image::const_iterator pt_it = img_it->cbegin(); pt_it != img_it->cend(); ++pt_it)
				{
					if (0 > xmlTextWriterStartElement(writer, BAD_CAST "ImgPoint")) return false;

					RFChannel ch = 1;
					do {
						*fap = pt_it->GetFAP(ch);
						if (0 > xmlTextWriterStartElement(writer, BAD_CAST "FAP")) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ch", "%d", static_cast<int>(ch))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Freq", "%0.3f", static_cast<double>(static_cast<iMS::Frequency>(fap->freq)))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Ampl", "%0.3f", static_cast<double>(fap->ampl))) return false;
						if (0 > xmlTextWriterWriteFormatAttribute(writer, BAD_CAST "Phase", "%0.3f", static_cast<double>(fap->phase))) return false;
						if (0 > xmlTextWriterEndElement(writer)) return false;
					} while (ch++ != 4);

					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncDig", "%d", pt_it->GetSyncD())) return false;
					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncAnlg1", "%0.4f", pt_it->GetSyncA(0))) return false;
					if (0 > xmlTextWriterWriteFormatElement(writer, BAD_CAST "SyncAnlg2", "%0.4f", pt_it->GetSyncA(1))) return false;

					/* Close the element named ImgPoint. */
					if (0 > xmlTextWriterEndElement(writer)) return false;
				}
				delete fap;

				/* Close the element named ArrayOfImgPoints. */
				if (0 > xmlTextWriterEndElement(writer)) return false;

				/* Close the element named Image. */
				if (0 > xmlTextWriterEndElement(writer)) return false;
			}

			/* Close the element named FreeImages */
			if (0 > xmlTextWriterEndElement(writer)) return false;
		}

		if (0 > xmlTextWriterEndDocument(writer)) return false;

		xmlFreeTextWriter(writer);

		return true;

	}

	bool ImageProject::Load(const std::string& fileName)
	{
		xmlTextReaderPtr reader;
		xmlChar *name, *value;
		int ret;
		bool result = false;
		IIPVersion file_ver;

		reader = xmlReaderForFile(fileName.c_str(), MY_ENCODING, 0);
		if (reader != NULL) {

			/* Start afresh */
			this->Clear();

			/* Navigate to first element */
			do {
				ret = xmlTextReaderRead(reader);
				if (ret < 1) return false;
				name = xmlTextReaderName(reader);
				if (name == NULL) continue;
				else if (xmlStrEqual(name, BAD_CAST "ImageProject")) {
					xmlFree(name);
					break;
				}
				xmlFree(name);
			} while (1);

			/* Check version number */
			int version_attr = xmlTextReaderHasAttributes(reader);
			if (version_attr == 1) {
				value = xmlTextReaderGetAttribute(reader, BAD_CAST "version");
				if (value == NULL) {
					xmlFree(value);
					return false;
				}
				long l;
				char *dp;
				l = strtol((const char *)&value[1], &dp, 10);
				file_ver.major = l;
				l = strtol((const char *)&dp[1], NULL, 10);
				file_ver.minor = l;
			}
			else if (version_attr == 0) {
				/* No version attribute information, predates v2.0, likely from the iHHS ImageFileGenerator */
				file_ver.major = 1;
				file_ver.minor = 0;
			}
			else return false;

			result = p_Impl->processFile(reader, file_ver);

			xmlFreeTextReader(reader);
		}

		return result;
	}

	/* Struct for temporarily holding iHHS Sequence data until we have read in the Image Table and can assign UUIDs*/
	struct iHHSSeqEntry
	{
		std::string Name;
		int Rpts;
		int SyncDelInt;
		int IntOscInt;
		int ImgDelayInt;
		int ClkDiv;
	};

	bool ImageProject::Impl::processFile(xmlTextReaderPtr reader, const IIPVersion& ver)
	{
		int imgFileCount, cFuncCount, tbufCount, freeImgCount;
		
		switch (ver.major) {
		case 0: return false;
		case 1: {
			// iHHS Style
			std::string iHHS_ver;

			if (!GetNodeWithIntValue(reader, BAD_CAST "ImageFileCount", imgFileCount)) return false;
			if (!GetNodeWithStringValue(reader, BAD_CAST "ImageProjectFormat", iHHS_ver)) return false;
			// No other iHHS versions were ever defined
			if (iHHS_ver != "v1.0") return false;

			for (int i = 0; i < imgFileCount; i++)
			{
				if (!GetStartElement(reader, BAD_CAST "ImageFile")) return false;
				std::string imgFileName;
				if (!GetNodeWithStringValue(reader, BAD_CAST "ImageFileName", imgFileName)) return false;
				std::string cTimeStr, mTimeStr;
				int createdTime, modifiedTime;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Created", cTimeStr)) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Modified", mTimeStr)) return false;
				std::tm c_tm = {}, m_tm = {};
				std::stringstream ss(cTimeStr);
				ss >> std::get_time(&c_tm, "%d/%m/%Y %H:%M:%S");
				createdTime = (int)std::mktime(&c_tm);
				ss.clear();
				ss.str(mTimeStr);
				ss >> std::get_time(&m_tm, "%d/%m/%Y %H:%M:%S");
				modifiedTime = (int)std::mktime(&m_tm);

				ImageGroup imgFile(imgFileName, createdTime, modifiedTime);

				if (!GetNodeWithStringValue(reader, BAD_CAST "Author", imgFile.Author())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Company", imgFile.Company())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Revision", imgFile.Revision())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Description", imgFile.Description())) return false;

				// We don't record these two parameters within the ImageGroup anymore
				std::string nullstr;
				if (!GetNodeWithStringValue(reader, BAD_CAST "ClockType", nullstr)) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "TriggerType", nullstr)) return false;

				ImageSequence *imgSeq = new ImageSequence();
				
				if (!GetStartElement(reader, BAD_CAST "SequenceTable")) return false;
				if (!GetStartElement(reader, BAD_CAST "ArrayOfSeqEntry")) return false;

				std::list<iHHSSeqEntry> seq_list;
				do
				{
					// No size info, so keep reading until we inadvertently end the ArrayOfSeqEntry element
					if (!GetStartElement(reader, BAD_CAST "SeqEntry")) break;
					iHHSSeqEntry seq_entry;
					if (!GetNodeWithStringValue(reader, BAD_CAST "Name", seq_entry.Name)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "Rpts", seq_entry.Rpts)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "SyncDelInt", seq_entry.SyncDelInt)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "IntOscInt", seq_entry.IntOscInt)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "ImgDelayInt", seq_entry.ImgDelayInt)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "ClkDiv", seq_entry.ClkDiv)) return false;
					seq_list.push_back(seq_entry);
					if (!GetEndElement(reader, BAD_CAST "SeqEntry")) return false;
				} while (1);

				std::string seqTerm;
				SequenceTermAction sta;
				int seqTermIndex;
				if (!GetNodeWithStringValue(reader, BAD_CAST "SequenceTermination", seqTerm)) return false;
				if (!GetNodeWithIntValue(reader, BAD_CAST "SequenceTerminationIndex", seqTermIndex)) return false;
				if (seqTerm == "Stop") {
					sta = SequenceTermAction::STOP_DISCARD;
				}
				else if (seqTerm == "RepeatAll") {
					sta = SequenceTermAction::REPEAT;
				}
				else {
					sta = SequenceTermAction::REPEAT_FROM;
				}
				imgSeq->OnTermination(sta, seqTermIndex);
				imgFile.Sequence() = *imgSeq;
				delete imgSeq;
				if (!GetEndElement(reader, BAD_CAST "SequenceTable")) return false;

				int imgCount;
				if (!GetStartElement(reader, BAD_CAST "ImageTable")) return false;
				if (!GetNodeWithIntValue(reader, BAD_CAST "ImageCount", imgCount)) return false;

				for (int i = 0; i < imgCount; i++) {
					if (!GetStartElement(reader, BAD_CAST "Image")) return false;
					std::string imgName;
					if (!GetNodeWithStringValue(reader, BAD_CAST "ImageName", imgName)) return false;
					int ImageIndex;	
					if (!GetNodeWithIntValue(reader, BAD_CAST "ImageIndex", ImageIndex)) return false;

					int NumChan, NumPt;
					std::string desc, syncStyle;
					std::array<bool, 12> syncDelayed;

					if (!GetNodeWithIntValue(reader, BAD_CAST "NumChannels", NumChan)) return false;
					if (!GetNodeWithStringValue(reader, BAD_CAST "Description", desc)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "NumPoints", NumPt)) return false;
					if (!GetNodeWithStringValue(reader, BAD_CAST "SyncStyle", syncStyle)) return false;
					if (!GetStartElement(reader, BAD_CAST "SyncBitsDelayed")) return false;
					for (int j = 0; j < 12; j++) {
						char nodeName[6];
						sprintf(nodeName, "bit%d", j);
						if (!GetNodeWithBoolValue(reader, BAD_CAST nodeName, syncDelayed[j])) return false;
					}
					if (!GetEndElement(reader, BAD_CAST "SyncBitsDelayed")) return false;

					Image *new_img = new Image(std::size_t(NumPt), ImagePoint(), imgName);
					new_img->Description() = desc;

					for (int j = 0; j < NumChan; j++) {
						char nodeName[9];
						sprintf(nodeName, "Channel%d", j);
						if (!GetStartElement(reader, BAD_CAST nodeName)) return false;
						std::string syncType;
						bool hasSync = true;
						SignalPath::SYNC_SINK syncDest = SignalPath::SYNC_SINK::DIG;
						if (!GetNodeWithStringValue(reader, BAD_CAST "SyncType", syncType)) return false;
						if (syncType == "NoSync") hasSync = false;
						else {
							if (syncType == "Digital") syncDest = SignalPath::SYNC_SINK::DIG;
							else if (syncType == "DAC0") syncDest = SignalPath::SYNC_SINK::ANLG_A;
							else if (syncType == "DAC1") syncDest = SignalPath::SYNC_SINK::ANLG_B;
						}
						if (!GetStartElement(reader, BAD_CAST "ArrayOfImgPoint")) return false;

						for (int k = 0; k < NumPt; k++)
						{
							if (!GetStartElement(reader, BAD_CAST "imgPoint")) return false;
							FAP fap;
							int sync, freq, ampl, phs;
							if (!GetNodeWithIntValue(reader, BAD_CAST "SyncData", sync)) return false;
							if (!GetNodeWithIntValue(reader, BAD_CAST "Frequency", freq)) return false;
							if (!GetNodeWithIntValue(reader, BAD_CAST "Amplitude", ampl)) return false;
							if (!GetNodeWithIntValue(reader, BAD_CAST "Phase", phs)) return false;

							if (hasSync) {
								switch (syncDest) {
									case SignalPath::SYNC_SINK::ANLG_A: (*new_img)[k].SetSyncA(0, ((float)sync) / 4096.0f); break;
									case SignalPath::SYNC_SINK::ANLG_B: (*new_img)[k].SetSyncA(0, ((float)sync) / 4096.0f); break;
									case SignalPath::SYNC_SINK::DIG: (*new_img)[k].SetSyncD(sync); break;
								}
							}

							fap.freq = MHz(((double)freq * 500.0) / std::pow(2, 32));
							fap.ampl = Percent(((double)ampl + 0.01) / 10.23);
							fap.phase = Degrees(((double)phs + 0.01) / (16383.0 / 360.0));

							(*new_img)[k].SetFAP(RFChannel(j + 1), fap);

							if (!GetEndElement(reader, BAD_CAST "imgPoint")) return false;
						}
						if (!GetEndElement(reader, BAD_CAST "ArrayOfImgPoint")) return false;
						if (!GetEndElement(reader, BAD_CAST nodeName)) return false;
					}
					if (!GetEndElement(reader, BAD_CAST "Image")) return false;
					imgFile.AddImage(*new_img);

					delete new_img;
				}
				if (!GetEndElement(reader, BAD_CAST "ImageTable")) return false;

				/* Create sequence table */
				for (std::list<iHHSSeqEntry>::iterator it = seq_list.begin(); it != seq_list.end(); ++it) {
					/* Search for seq entry name in image table */
					for (ImageGroup::iterator img_it = imgFile.begin(); img_it != imgFile.end(); ++img_it) {
						if (it->Name == img_it->Name()) {
							ImageRepeats img_rpt = ImageRepeats::NONE;
							if (it->Rpts == 255) img_rpt = ImageRepeats::FOREVER;
							else if (it->Rpts > 0) img_rpt = ImageRepeats::PROGRAM;
							Image img = *img_it;
							img.ClockRate(kHz(1000.0 / (double)it->IntOscInt));
							img.ExtClockDivide(it->ClkDiv);
							ImageSequenceEntry ise(img, img_rpt, it->Rpts);
							ise.PostImgDelay() = std::chrono::duration<double>((double)it->ImgDelayInt / 10000.0);
							ise.SyncOutDelay() = std::chrono::duration<double>((double)it->SyncDelInt / 10000000.0);
							imgFile.Sequence().push_back(std::make_shared<ImageSequenceEntry>(ise));
							break;
						}
					}
				}
				this->m_imgGroupList.push_back(imgFile);

				if (!GetEndElement(reader, BAD_CAST "ImageFile")) return false;
			}
			break;
		}
		case 2: {

			if (ver.minor > 0) {
				if (!GetStartElement(reader, BAD_CAST "ImageGroupCount")) return false;
			}
			else {
				if (!GetStartElement(reader, BAD_CAST "ImageFileCount")) return false;
			}
			if (!GetAttributeWithIntValue(reader, BAD_CAST "Count", imgFileCount)) return false;
			if (ver.minor > 1) {
				if (!GetStartElement(reader, BAD_CAST "CompensationFunctionCount")) return false;
			}
			else {
				if (!GetStartElement(reader, BAD_CAST "CompensationTableCount")) return false;
			}
			if (!GetAttributeWithIntValue(reader, BAD_CAST "Count", cFuncCount)) return false;
			if (!GetStartElement(reader, BAD_CAST "ToneBufferCount")) return false;
			if (!GetAttributeWithIntValue(reader, BAD_CAST "Count", tbufCount)) return false;
			if (ver.minor > 0) {
				if (!GetStartElement(reader, BAD_CAST "FreeImageCount")) return false;
				if (!GetAttributeWithIntValue(reader, BAD_CAST "Count", freeImgCount)) return false;
			}
			else freeImgCount = 0;

			for (int i = 0; i < imgFileCount; i++)
			{
				if (ver.minor > 0) {
					if (!GetStartElement(reader, BAD_CAST "ImageGroup")) return false;
				}
				else {
					if (!GetStartElement(reader, BAD_CAST "ImageFile")) return false;
				}
				std::string imgFileName;
				if (!GetAttributeWithStringValue(reader, BAD_CAST "Name", imgFileName)) return false;

				int createdTime, modifiedTime;
				if (!GetNodeWithIntValue(reader, BAD_CAST "Created", createdTime)) return false;
				if (!GetNodeWithIntValue(reader, BAD_CAST "Modified", modifiedTime)) return false;

				ImageGroup imgFile(imgFileName, createdTime, modifiedTime);

				if (!GetNodeWithStringValue(reader, BAD_CAST "Author", imgFile.Author())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Company", imgFile.Company())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Revision", imgFile.Revision())) return false;
				if (!GetNodeWithStringValue(reader, BAD_CAST "Description", imgFile.Description())) return false;

				ImageSequence *imgSeq = new ImageSequence();
				int imgSeqEntries;

				if (!GetStartElement(reader, BAD_CAST "SequenceTable")) return false;
				if (!GetStartElement(reader, BAD_CAST "ArrayOfSeqEntry")) return false;
				if (!GetAttributeWithIntValue(reader, BAD_CAST "Size", imgSeqEntries)) return false;

				for (int i = 0; i < imgSeqEntries; i++) {
					if (!GetStartElement(reader, BAD_CAST "SeqEntry")) return false;

					std::string uuid_str;
					if (!GetNodeWithStringValue(reader, BAD_CAST "Image", uuid_str)) return false;
					boost::uuids::uuid img_uuid = boost::lexical_cast<boost::uuids::uuid>(uuid_str);
					std::array<std::uint8_t, 16> v;
					std::copy_n(img_uuid.begin(), 16, v.begin());

					/* Create a table entry as means of getting a UUID into the sequence entry */
					ImageTableEntry ite(0, 0, 0, 0, 0, v, std::string());

					int RptType, Rpts, ClkDiv;
					double SyncDel, IntOsc, ImgDelay;
					if (!GetNodeWithIntValue(reader, BAD_CAST "RptType", RptType)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "Rpts", Rpts)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncDel", SyncDel)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "IntOsc", IntOsc)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "ImgDelay", ImgDelay)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "ClkDiv", ClkDiv)) return false;

					std::shared_ptr<ImageSequenceEntry> ise;
					if (ClkDiv > 1) {
						ise = std::make_shared<ImageSequenceEntry>(ite, ClkDiv, static_cast<ImageRepeats>(RptType), Rpts);
					}
					else {
						ise = std::make_shared<ImageSequenceEntry>(ite, kHz(IntOsc / 1000.0), static_cast<ImageRepeats>(RptType), Rpts);
					}
					ise->SyncOutDelay() = std::chrono::duration<double>(SyncDel);
					ise->PostImgDelay() = std::chrono::duration<double>(ImgDelay);

					imgSeq->push_back(ise);

					if (!GetEndElement(reader, BAD_CAST "SeqEntry")) return false;
				}
				if (imgSeqEntries > 0) if (!GetEndElement(reader, BAD_CAST "ArrayOfSeqEntry")) return false;

				int seqTerm, seqTermIndex;
				if (!GetNodeWithIntValue(reader, BAD_CAST "SequenceTermination", seqTerm)) return false;
				if (!GetNodeWithIntValue(reader, BAD_CAST "SequenceTerminationIndex", seqTermIndex)) return false;
				imgSeq->OnTermination(static_cast<SequenceTermAction>(seqTerm), seqTermIndex);

				imgFile.Sequence() = *imgSeq;
				delete imgSeq;


				if (!GetEndElement(reader, BAD_CAST "SequenceTable")) return false;

				int imgCount;
				if (!GetStartElement(reader, BAD_CAST "ImageTable")) return false;
				if (!GetAttributeWithIntValue(reader, BAD_CAST "Count", imgCount)) return false;

				for (int i = 0; i < imgCount; i++) {
					if (!GetStartElement(reader, BAD_CAST "Image")) return false;
					std::string imgName;
					if (!GetAttributeWithStringValue(reader, BAD_CAST "Name", imgName)) return false;

					std::string uuid_str;
					if (!GetNodeWithStringValue(reader, BAD_CAST "ImageUUID", uuid_str)) return false;
					boost::uuids::uuid img_uuid = boost::lexical_cast<boost::uuids::uuid>(uuid_str);
					std::array<std::uint8_t, 16> img_uuid_arr;
					std::copy_n(img_uuid.begin(), 16, img_uuid_arr.begin());

					int NumChan, NumPt, ClkDiv;
					std::string desc;
					double IntOsc;

					if (!GetNodeWithIntValue(reader, BAD_CAST "NumChannels", NumChan)) return false;
					if (!GetNodeWithStringValue(reader, BAD_CAST "Description", desc)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "IntOsc", IntOsc)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "ClkDiv", ClkDiv)) return false;

					Image *new_img = new Image(imgName);
					new_img->ClockRate(Frequency(IntOsc));
					new_img->ExtClockDivide(ClkDiv);
					new_img->Description() = desc;


					if (!GetStartElement(reader, BAD_CAST "ArrayOfImgPoints")) return false;
					if (!GetAttributeWithIntValue(reader, BAD_CAST "Size", NumPt)) return false;
					for (int i = 0; i < NumPt; i++) {
						if (!GetStartElement(reader, BAD_CAST "ImgPoint")) return false;

						FAP fap[4];
						double dval;
						int ch;
						do {
							if (!GetStartElement(reader, BAD_CAST "FAP")) return false;
							if (!GetAttributeWithIntValue(reader, BAD_CAST "Ch", ch)) return false;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Freq", dval)) return false;
							fap[ch-1].freq = dval / 1000000.0;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Ampl", dval)) return false;
							fap[ch-1].ampl = dval;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Phase", dval)) return false;
							fap[ch-1].phase = dval;
						} while (ch != 4);

						int SyncDig;
						double SyncAnlg1, SyncAnlg2;
						if (!GetNodeWithIntValue(reader, BAD_CAST "SyncDig", SyncDig, 0)) return false;
						if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncAnlg1", SyncAnlg1)) return false;
						if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncAnlg2", SyncAnlg2)) return false;

						if (!GetEndElement(reader, BAD_CAST "ImgPoint")) return false;

						ImagePoint *pt = new ImagePoint(fap[0], fap[1], fap[2], fap[3], (float)SyncAnlg1, (float)SyncAnlg2, SyncDig);
						new_img->AddPoint(*pt);
						delete pt;
					}
					if (NumPt > 0) if (!GetEndElement(reader, BAD_CAST "ArrayOfImgPoints")) return false;
					imgFile.AddImage(*new_img);

					/* Update any references in sequence table */
					for (ImageSequence::iterator it = imgFile.Sequence().begin(); it != imgFile.Sequence().end(); ++it) {
						const std::shared_ptr<SequenceEntry> entry = *it;
						const std::shared_ptr<ImageSequenceEntry> img_entry = std::dynamic_pointer_cast<ImageSequenceEntry>(entry);

						if (img_entry == nullptr)
							continue;

						if (entry->UUID() == img_uuid_arr) {
							new_img->ClockRate(img_entry->IntOsc());
							new_img->ExtClockDivide(img_entry->ExtDiv());
							ImageSequenceEntry ise(*new_img, img_entry->RptType(), entry->NumRpts());
							ise.PostImgDelay() = img_entry->PostImgDelay();
							ise.SyncOutDelay() = img_entry->SyncOutDelay();
							*entry = ise;
						}
					}
					delete new_img;
					if (!GetEndElement(reader, BAD_CAST "Image")) return false;
				}

				this->m_imgGroupList.push_back(imgFile);
				if (imgCount > 0) if (!GetEndElement(reader, BAD_CAST "ImageTable")) return false;
				if (ver.minor > 0) {
					if (!GetEndElement(reader, BAD_CAST "ImageGroup")) return false;
				}
				else {
					if (!GetEndElement(reader, BAD_CAST "ImageFile")) return false;
				}

			}

			for (int i = 0; i < cFuncCount; i++)
			{
				if (ver.minor > 1) {
					if (!GetStartElement(reader, BAD_CAST "CompensationFunction")) return false;
				}
				else {
					if (!GetStartElement(reader, BAD_CAST "Compensation")) return false;
				}

				CompensationFunction * comp = new CompensationFunction();
				if (!GetAttributeWithStringValue(reader, BAD_CAST "Name", comp->Name())) return false;
				int modifiedTime;
				if (!GetNodeWithIntValue(reader, BAD_CAST "Modified", modifiedTime)) return false;

				if (!GetStartElement(reader, BAD_CAST "ArrayOfCompensationPointSpecification")) return false;
				int compSize;
				if (!GetAttributeWithIntValue(reader, BAD_CAST "Size", compSize)) return false;

				for (int i = 0; i < compSize; i++) {
					if (!GetStartElement(reader, BAD_CAST "CompensationPointSpecification")) return false;

					double freq, ampl, phase;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "Freq", freq)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "Ampl", ampl)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "Phase", phase)) return false;

					int SyncDig;
					double SyncAnlg;
					if (!GetNodeWithIntValue(reader, BAD_CAST "SyncDig", SyncDig, 0)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncAnlg", SyncAnlg)) return false;

					if (!GetEndElement(reader, BAD_CAST "CompensationPointSpecification")) return false;

					CompensationPointSpecification *pt = new CompensationPointSpecification(
						CompensationPoint(
						Percent(ampl),
						Degrees(phase),
						SyncDig,
						SyncAnlg),
						MHz(freq / 1000000.0));
					comp->push_back(*pt);
					delete pt;
				}

				if (compSize > 0) if (!GetEndElement(reader, BAD_CAST "ArrayOfCompensationPointSpecification")) return false;

				this->m_cFuncList.push_back(*comp);
				delete comp;
				if (ver.minor > 1) {
					if (!GetEndElement(reader, BAD_CAST "CompensationFunction")) return false;
				}
				else {
					if (!GetEndElement(reader, BAD_CAST "Compensation")) return false;
				}
			}

			for (int i = 0; i < tbufCount; i++)
			{
				if (!GetStartElement(reader, BAD_CAST "ToneBuffer")) return false;

				std::string tbufName;
				if (!GetAttributeWithStringValue(reader, BAD_CAST "Name", tbufName)) return false;
				ToneBuffer * tbuf = new ToneBuffer(tbufName);

				if (!GetStartElement(reader, BAD_CAST "ArrayOfTBEntry")) return false;
				int tbufSize;
				if (!GetAttributeWithIntValue(reader, BAD_CAST "Size", tbufSize)) return false;

				for (int i = 0; i < tbufSize; i++) {
					if (!GetStartElement(reader, BAD_CAST "TBEntry")) return false;

					FAP fap[4];
					double dval;
					int ch;
					do {
						if (!GetStartElement(reader, BAD_CAST "FAP")) return false;
						if (!GetAttributeWithIntValue(reader, BAD_CAST "Ch", ch)) return false;
						if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Freq", dval)) return false;
						fap[ch - 1].freq = dval / 1000000.0;
						if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Ampl", dval)) return false;
						fap[ch - 1].ampl = dval;
						if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Phase", dval)) return false;
						fap[ch - 1].phase = dval;
					} while (ch != 4);

					if (!GetEndElement(reader, BAD_CAST "TBEntry")) return false;

					TBEntry *tbe = new TBEntry(fap[0], fap[1], fap[2], fap[3], 0.0, 0.0, 0);
					(*tbuf)[i] = (*tbe);
					delete tbe;
				}

				if (tbufSize > 0) if (!GetEndElement(reader, BAD_CAST "ArrayOfTBEntry")) return false;

				this->m_toneBufList.push_back(*tbuf);
				delete tbuf;
				if (!GetEndElement(reader, BAD_CAST "ToneBuffer")) return false;
			}

			if (freeImgCount > 0)
			{
				if (!GetStartElement(reader, BAD_CAST "FreeImages")) return false;

				for (int i = 0; i < freeImgCount; i++) {
					if (!GetStartElement(reader, BAD_CAST "Image")) return false;
					std::string imgName;
					if (!GetAttributeWithStringValue(reader, BAD_CAST "Name", imgName)) return false;

					std::string uuid_str;
					if (!GetNodeWithStringValue(reader, BAD_CAST "ImageUUID", uuid_str)) return false;
					boost::uuids::uuid img_uuid = boost::lexical_cast<boost::uuids::uuid>(uuid_str);
					std::array<std::uint8_t, 16> img_uuid_arr;
					std::copy_n(img_uuid.begin(), 16, img_uuid_arr.begin());

					int NumChan, NumPt, ClkDiv;
					std::string desc;
					double IntOsc;

					if (!GetNodeWithIntValue(reader, BAD_CAST "NumChannels", NumChan)) return false;
					if (!GetNodeWithStringValue(reader, BAD_CAST "Description", desc)) return false;
					if (!GetNodeWithDoubleValue(reader, BAD_CAST "IntOsc", IntOsc)) return false;
					if (!GetNodeWithIntValue(reader, BAD_CAST "ClkDiv", ClkDiv)) return false;

					Image *new_img = new Image(imgName);
					new_img->ClockRate(Frequency(IntOsc));
					new_img->ExtClockDivide(ClkDiv);
					new_img->Description() = desc;


					if (!GetStartElement(reader, BAD_CAST "ArrayOfImgPoints")) return false;
					if (!GetAttributeWithIntValue(reader, BAD_CAST "Size", NumPt)) return false;
					for (int i = 0; i < NumPt; i++) {
						if (!GetStartElement(reader, BAD_CAST "ImgPoint")) return false;

						FAP fap[4];
						double dval;
						int ch;
						do {
							if (!GetStartElement(reader, BAD_CAST "FAP")) return false;
							if (!GetAttributeWithIntValue(reader, BAD_CAST "Ch", ch)) return false;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Freq", dval)) return false;
							fap[ch - 1].freq = dval / 1000000.0;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Ampl", dval)) return false;
							fap[ch - 1].ampl = dval;
							if (!GetAttributeWithDoubleValue(reader, BAD_CAST "Phase", dval)) return false;
							fap[ch - 1].phase = dval;
						} while (ch != 4);

						int SyncDig;
						double SyncAnlg1, SyncAnlg2;
						if (!GetNodeWithIntValue(reader, BAD_CAST "SyncDig", SyncDig, 0)) return false;
						if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncAnlg1", SyncAnlg1)) return false;
						if (!GetNodeWithDoubleValue(reader, BAD_CAST "SyncAnlg2", SyncAnlg2)) return false;

						if (!GetEndElement(reader, BAD_CAST "ImgPoint")) return false;

						ImagePoint *pt = new ImagePoint(fap[0], fap[1], fap[2], fap[3], (float)SyncAnlg1, (float)SyncAnlg2, SyncDig);
						new_img->AddPoint(*pt);
						delete pt;
					}
					if (NumPt > 0) if (!GetEndElement(reader, BAD_CAST "ArrayOfImgPoints")) return false;
					m_freeImgList.AddImage(*new_img);

					delete new_img;
					if (!GetEndElement(reader, BAD_CAST "Image")) return false;
				}

				if (!GetEndElement(reader, BAD_CAST "FreeImages")) return false;
			}
		}
			break;
		default:
			return false;
		}
		return true;
	}

	static inline bool GetStartElement(xmlTextReaderPtr reader, xmlChar * expectedName)
	{
		int ret;
		xmlChar *name;

		/* Ignore whitespace */
		do {
			ret = xmlTextReaderRead(reader);
			if (ret < 1) return false;
			name = xmlTextReaderName(reader);
			if (name == NULL) return false;
			if (xmlStrEqual(name, BAD_CAST "#text")) {
				xmlFree(name);
				continue;
			}
			else if ((!xmlStrEqual(name, expectedName)) || (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT)) {
				xmlFree(name);
				return false;
			}
			else break;
		} while (1);
		xmlFree(name);
		return true;
	}

	static inline bool GetEndElement(xmlTextReaderPtr reader, xmlChar * expectedName)
	{
		int ret;
		xmlChar *name;

		/* Ignore whitespace */
		do {
			ret = xmlTextReaderRead(reader);
			if (ret < 1) return false;
			name = xmlTextReaderName(reader);
			if (name == NULL) return false;
			if (xmlStrEqual(name, BAD_CAST "#text")) {
				xmlFree(name);
				continue;
			}
			else if ((!xmlStrEqual(name, expectedName)) || (xmlTextReaderNodeType(reader) != XML_READER_TYPE_END_ELEMENT)) {
				xmlFree(name);
				return false;
			}
			else break;
		} while (1);
		xmlFree(name);
		return true;
	}

	static inline bool GetNodeWithStringValue(xmlTextReaderPtr reader, xmlChar * expectedName, std::string& StrValue)
	{
		int ret;
		xmlChar *value;

		if (!GetStartElement(reader, expectedName)) return false;

		ret = xmlTextReaderRead(reader);
		if (ret < 1) return false;
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_TEXT) return false;

		value = xmlTextReaderValue(reader);
		if (value == NULL) return false;
		else StrValue = std::string((const char *)value);
		xmlFree(value);

		/* Read closing element */
		if (!GetEndElement(reader, expectedName)) return false;

		return true;
	}

	static inline bool GetNodeWithIntValue(xmlTextReaderPtr reader, xmlChar * expectedName, int& IntValue, int base)
	{
		std::string StrValue;
		if (!GetNodeWithStringValue(reader, expectedName, StrValue)) return false;
		try {
			IntValue = std::stoi(StrValue, NULL, base);
		}
		catch (std::invalid_argument) { return false; }
		catch (std::out_of_range) { return false; }
		return true;
	}

	static inline bool GetNodeWithDoubleValue(xmlTextReaderPtr reader, xmlChar * expectedName, double& FloatValue)
	{
		std::string StrValue;
		if (!GetNodeWithStringValue(reader, expectedName, StrValue)) return false;
		try {
			FloatValue = std::stod(StrValue);
		}
		catch (std::invalid_argument) { return false; }
		catch (std::out_of_range) { return false; }
		return true;
	}

	static inline bool GetNodeWithBoolValue(xmlTextReaderPtr reader, xmlChar * expectedName, bool& BoolValue)
	{
		std::string StrValue;
		if (!GetNodeWithStringValue(reader, expectedName, StrValue)) return false;
		BoolValue = false;
		if (StrValue == "true") BoolValue = true;
		else if (StrValue == "false") BoolValue = false;
		else return false;
		return true;
	}

	static inline bool GetAttributeWithStringValue(xmlTextReaderPtr reader, xmlChar * expectedName, std::string& StrValue)
	{
		xmlChar *value;

		if (1 > xmlTextReaderHasAttributes(reader)) return false;
		value = xmlTextReaderGetAttribute(reader, expectedName);
		if (value == NULL) {
			xmlFree(value);
			return false;
		}
		StrValue = std::string((const char *)value);
		xmlFree(value);

		return true;
	}

	static inline bool GetAttributeWithIntValue(xmlTextReaderPtr reader, xmlChar * expectedName, int& IntValue, int base)
	{
		std::string StrValue;
		if (!GetAttributeWithStringValue(reader, expectedName, StrValue)) return false;
		try {
			IntValue = std::stoi(StrValue, NULL, base);
		}
		catch (std::invalid_argument) { return false; }
		catch (std::out_of_range) { return false; }
		return true;
	}

	static inline bool GetAttributeWithDoubleValue(xmlTextReaderPtr reader, xmlChar * expectedName, double& FloatValue)
	{
		std::string StrValue;
		if (!GetAttributeWithStringValue(reader, expectedName, StrValue)) return false;
		try {
			FloatValue = std::stod(StrValue);
		}
		catch (std::invalid_argument) { return false; }
		catch (std::out_of_range) { return false; }
		return true;
	}

	static inline std::string UUIDToStringConverter(std::array<std::uint8_t, 16> uuid)
	{
		std::ostringstream u_s;
		int j;
		for (j = 0; j < 4; j++) u_s << std::hex << std::setfill('0') << std::setw(2) << (int)uuid[j];
		u_s << "-";
		for (j = 4; j < 6; j++) u_s << std::setfill('0') << std::setw(2) << (int)uuid[j];
		u_s << "-";
		for (j = 6; j < 8; j++) u_s << std::setfill('0') << std::setw(2) << (int)uuid[j];
		u_s << "-";
		for (j = 8; j < 10; j++) u_s << std::setfill('0') << std::setw(2) << (int)uuid[j];
		u_s << "-";
		for (j = 10; j < 16; j++) u_s << std::setfill('0') << std::setw(2) << (int)uuid[j];
		return u_s.str();
	}

}
