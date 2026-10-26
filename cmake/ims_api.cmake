# Copyright 2021 Isomet (UK) Ltd. All rights reserved.

set(ims_api_source_files
    ${api_source_dir}/AcoustoOptics.cpp
    ${api_source_dir}/Auxiliary.cpp
    ${api_source_dir}/Compensation.cpp
    ${api_source_dir}/CM_Common.cpp
    ${api_source_dir}/CM_CYUSB.cpp
    ${api_source_dir}/CM_ENET.cpp
    ${api_source_dir}/tftp_client.cpp
    ${api_source_dir}/tftp_packet.cpp
    ${api_source_dir}/CM_FTDI.cpp
    ${api_source_dir}/CM_RS422.cpp
    ${api_source_dir}/CS_ETH.cpp
    ${api_source_dir}/CS_RS422.cpp
    ${api_source_dir}/ConnectionList.cpp
    ${api_source_dir}/ConnectionManager.cpp
    ${api_source_dir}/IMSSystem.cpp
    ${api_source_dir}/Diagnostics.cpp
    ${api_source_dir}/IEventHandler.cpp
    ${api_source_dir}/IEventTrigger.cpp
    ${api_source_dir}/FileSystem.cpp
    ${api_source_dir}/Image.cpp
    ${api_source_dir}/ImageOps.cpp
    ${api_source_dir}/ImageProject.cpp
    ${api_source_dir}/DeviceReport.cpp
    ${api_source_dir}/HostReport.cpp
    ${api_source_dir}/IOReport.cpp
    ${api_source_dir}/Message.cpp
    ${api_source_dir}/ReportManipulation.cpp
    ${api_source_dir}/BulkVerifier.cpp
    ${api_source_dir}/Containers.cpp
    ${api_source_dir}/EEPROM.cpp
    ${api_source_dir}/IMSTypeDefs.cpp
    ${api_source_dir}/LibVersion.cpp
    ${api_source_dir}/PrivateUtil.cpp
    ${api_source_dir}/FirmwareUpgrade.cpp
    ${api_source_dir}/SignalPath.cpp
    ${api_source_dir}/WaveShaping.cpp
    ${api_source_dir}/SystemFunc.cpp
    ${api_source_dir}/ToneBuffer.cpp
    ${api_source_dir}/VCO.cpp
    ${api_lib_dir}/sqlite3/readonlymemvfs.cpp
    ${api_lib_dir}/sqlite3/sqlite3.c
)

set_source_files_properties(${api_lib_dir}/sqlite3/sqlite3.c PROPERTIES LANGUAGE C)

if(WIN32)
set(ims_api_source_files
    ${ims_api_source_files}
    ${api_lib_dir}/CyAPI/CyAPI.cpp
)
# enumser depends on ATL, which MinGW does not provide (CM_RS422.cpp has a MinGW fallback)
if(NOT MINGW)
    list(APPEND ims_api_source_files ${api_lib_dir}/enumser/enumser.cpp)
endif()
endif()

if(MINGW)
    # Vendor code assigns negative USBD_STATUS constants to unsigned values; MSVC only warns about this
    set_source_files_properties(${api_lib_dir}/CyAPI/CyAPI.cpp PROPERTIES COMPILE_OPTIONS "-Wno-narrowing")
endif()
