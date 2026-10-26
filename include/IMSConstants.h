/*-----------------------------------------------------------------------------
/ Title      : iMS Useful Type Defines Header
/ Project    : Isomet Modular Synthesiser System
/------------------------------------------------------------------------------
/ File       : $URL: http://nutmeg/svn/sw/trunk/09-Isomet/iMS_SDK/API/Other/h/IMSConstants.h $
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

#ifndef IMS_IMSCONSTANTS_H__
#define IMS_IMSCONSTANTS_H__

namespace iMS {

	const std::uint16_t ACR_AmplitudeControl_bitmask = 0x000C;
	const std::uint16_t ACR_AmplitudeControlUpper_bitmask = 0x00C0;
	const std::uint16_t ACR_AmplitudeControlCh1_bitmask = 0x0004;
	const std::uint16_t ACR_AmplitudeControlCh2_bitmask = 0x0008;
	const std::uint16_t ACR_AmplitudeControlCh3_bitmask = 0x0040;
	const std::uint16_t ACR_AmplitudeControlCh4_bitmask = 0x0080;
	const std::uint16_t ACR_ClearSTM_bitmask = 0x2000;

	const std::uint16_t ACR_AmplitudeControl_OFF = 0x0000;
	const std::uint16_t ACR_AmplitudeControl_EXTERNAL = 0x0004;
	const std::uint16_t ACR_AmplitudeControl_WIPER_1 = 0x0008;
	const std::uint16_t ACR_AmplitudeControl_WIPER_2 = 0x000C;

	const std::uint16_t ACR_ClearSTM = 0x2000;
	const std::uint16_t ACR_LocalInUse = 0x2000;

	const std::uint16_t SYNTH_REG_IOSig = 1;
	const std::uint16_t SYNTH_REG_IOSig_ANLG_A_Mask = 0x700;
	const std::uint16_t SYNTH_REG_IOSig_ANLG_B_Mask = 0xF800;
	const std::uint16_t SYNTH_REG_IOSig_DIG_Mask = 0xE0;
	const int SYNTH_REG_IOSig_ANLG_A_Shift = 8;
	const int SYNTH_REG_IOSig_ANLG_B_Shift = 11;
	const int SYNTH_REG_IOSig_DIG_Shift = 5;

	const std::uint16_t SYNTH_REG_IO_Signal_Control = 1;
	const std::uint16_t SYNTH_REG_SingleTone_Phase = 2;
	const std::uint16_t SYNTH_REG_SingleTone_Ampl = 3;
	const std::uint16_t SYNTH_REG_SingleTone_Freq = 4;
	const std::uint16_t SYNTH_REG_Phase_Offset_Ch1 = 9;
	const std::uint16_t SYNTH_REG_Phase_Offset_Ch2 = 10;
	const std::uint16_t SYNTH_REG_Phase_Offset_Ch3 = 11;
	const std::uint16_t SYNTH_REG_Phase_Offset_Ch4 = 12;
	const std::uint16_t SYNTH_REG_Pix_Control = 13;
	const std::uint16_t SYNTH_REG_Channel_Swap = 13;
	const std::uint16_t SYNTH_REG_NHF_Timeout = 18;
	const std::uint16_t SYNTH_REG_Clear_NHF = 19;
	const std::uint16_t SYNTH_REG_NHF_Action = 20;
	const std::uint16_t SYNTH_REG_ProgLocal = 25;
	const std::uint16_t SYNTH_REG_UseLocal = 26;
	const std::uint16_t SYNTH_REG_IO_Config_Mask = 31;
	const std::uint16_t SYNTH_REG_ProgSyncDig = 32;
	const std::uint16_t SYNTH_REG_ProgFreq0L = 58;
	const std::uint16_t SYNTH_REG_ProgFreq0H = 33;
	const std::uint16_t SYNTH_REG_ProgAmpl0 = 34;
	const std::uint16_t SYNTH_REG_ProgPhase0 = 35;
	const std::uint16_t SYNTH_REG_ProgFreq1L = 36;
	const std::uint16_t SYNTH_REG_ProgFreq1H = 37;
	const std::uint16_t SYNTH_REG_ProgAmpl1 = 38;
	const std::uint16_t SYNTH_REG_ProgPhase1 = 39;
	const std::uint16_t SYNTH_REG_ProgFreq2L = 40;
	const std::uint16_t SYNTH_REG_ProgFreq2H = 41;
	const std::uint16_t SYNTH_REG_ProgAmpl2 = 42;
	const std::uint16_t SYNTH_REG_ProgPhase2 = 43;
	const std::uint16_t SYNTH_REG_ProgFreq3L = 44;
	const std::uint16_t SYNTH_REG_ProgFreq3H = 45;
	const std::uint16_t SYNTH_REG_ProgAmpl3 = 46;
	const std::uint16_t SYNTH_REG_ProgPhase3 = 47;
	const std::uint16_t SYNTH_REG_PDI_Checksum = 48;
	const std::uint16_t SYNTH_REG_ENC_Control = 49;
	const std::uint16_t SYNTH_REG_ENC_CoeffKP = 50;
	const std::uint16_t SYNTH_REG_ENC_CoeffKI = 51;
	const std::uint16_t SYNTH_REG_ENC_VelEstX = 52;
	const std::uint16_t SYNTH_REG_ENC_VelEstY = 53;
	const std::uint16_t SYNTH_REG_ENC_VelGainX = 54;
	const std::uint16_t SYNTH_REG_ENC_VelGainY = 55;
	const std::uint16_t SYNTH_REG_SDOR_Pulse = 56;
	const std::uint16_t SYNTH_REG_SDOR_Delay = 57;
	const std::uint16_t SYNTH_REG_STM_FuncHold = 61;
	const std::uint16_t SYNTH_REG_SweepFSRR = 62;
	const std::uint16_t SYNTH_REG_SweepRSRR = 63;
	const std::uint16_t SYNTH_REG_SweepFDWLo = 64;
	const std::uint16_t SYNTH_REG_SweepFDWHi = 65;
	const std::uint16_t SYNTH_REG_SweepRDWLo = 66;
	const std::uint16_t SYNTH_REG_SweepRDWHi = 67;
	const std::uint16_t SYNTH_REG_Chan_Scope = 69;
	const std::uint16_t SYNTH_REG_Sync_Update = 70;
	const std::uint16_t SYNTH_REG_Phase_Resync = 71;
	const std::uint16_t SYNTH_REG_Image_Format = 72;
	const std::uint16_t SYNTH_REG_Freq_Offset_Ch1 = 74;
	const std::uint16_t SYNTH_REG_Freq_Offset_Ch2 = 75;
	const std::uint16_t SYNTH_REG_Freq_Offset_Ch3 = 76;
	const std::uint16_t SYNTH_REG_Freq_Offset_Ch4 = 77;
	const std::uint16_t SYNTH_REG_UseLocalIndex = 78;
	const std::uint16_t SYNTH_REG_SDORInvert = 79;
	const std::uint16_t SYNTH_REG_SDORPulseSelect = 80;
	const std::uint16_t SYNTH_REG_ChannelDelay34 = 81;
	const std::uint16_t SYNTH_REG_ChannelDelay12 = 82;
	const std::uint16_t SYNTH_REG_ETMStartFreqLo = 85;
	const std::uint16_t SYNTH_REG_ETMStartFreqHi = 86;
	const std::uint16_t SYNTH_REG_ETMStartAmpl = 87;
	const std::uint16_t SYNTH_REG_ETMStartPhase = 88;
	const std::uint16_t SYNTH_REG_ETMEndFreqLo = 89;
	const std::uint16_t SYNTH_REG_ETMEndFreqHi = 90;
	const std::uint16_t SYNTH_REG_ETMEndAmpl = 91;
	const std::uint16_t SYNTH_REG_ETMEndPhase = 92;
	const std::uint16_t SYNTH_REG_ETMControl = 93;
	const std::uint16_t SYNTH_REG_FreqLower = 94;
	const std::uint16_t SYNTH_REG_Freq_OffsetLower_Ch1 = 95;
	const std::uint16_t SYNTH_REG_Freq_OffsetLower_Ch2 = 96;
	const std::uint16_t SYNTH_REG_Freq_OffsetLower_Ch3 = 97;
	const std::uint16_t SYNTH_REG_Freq_OffsetLower_Ch4 = 98;
	const std::uint16_t SYNTH_REG_CICLength = 99;
	const std::uint16_t SYNTH_REG_CICBypass = 100;
	const std::uint16_t SYNTH_REG_IIRAlpha = 101;
	const std::uint16_t SYNTH_REG_IIRCount = 102;
	const std::uint16_t SYNTH_REG_IIRAlphaPre = 103;
	const std::uint16_t SYNTH_REG_DigGain = 104;
	const std::uint16_t SYNTH_REG_VCOMux = 105;
	const std::uint16_t SYNTH_REG_Ch1FShift = 106;
	const std::uint16_t SYNTH_REG_Ch1FScale = 107;
	const std::uint16_t SYNTH_REG_Ch1AShift = 108;
	const std::uint16_t SYNTH_REG_Ch1AScale = 109;
	const std::uint16_t SYNTH_REG_Ch2FShift = 110;
	const std::uint16_t SYNTH_REG_Ch2FScale = 111;
	const std::uint16_t SYNTH_REG_Ch2AShift = 112;
	const std::uint16_t SYNTH_REG_Ch2AScale = 113;
	const std::uint16_t SYNTH_REG_Ch1FConstHi = 114;
	const std::uint16_t SYNTH_REG_Ch1FConstLo = 115;
	const std::uint16_t SYNTH_REG_Ch1AConst = 116;
	const std::uint16_t SYNTH_REG_Ch2FConstHi = 117;
	const std::uint16_t SYNTH_REG_Ch2FConstLo = 118;
	const std::uint16_t SYNTH_REG_Ch2AConst = 119;
	const std::uint16_t SYNTH_REG_VCOTrack = 120;
	const std::uint16_t SYNTH_REG_VCOMonCh1 = 121;
	const std::uint16_t SYNTH_REG_VCOMonCh2 = 122;
	const std::uint16_t SYNTH_REG_VCOMonCh1Filt = 123;
	const std::uint16_t SYNTH_REG_VCOMonCh2Filt = 124;
	const std::uint16_t SYNTH_REG_VCOMonCh1Freq = 125;
	const std::uint16_t SYNTH_REG_VCOMonCh2Freq = 126;
	const std::uint16_t SYNTH_REG_VCOMonCh1Ampl = 127;
	const std::uint16_t SYNTH_REG_VCOMonCh2Ampl = 128;
    const std::uint16_t SYNTH_REG_VCOSaveStartup = 129;
    const std::uint16_t SYNTH_REG_DutyCycleEn = 130;
    const std::uint16_t SYNTH_REG_DutyCycleWidth = 131;
    const std::uint16_t SYNTH_REG_DutyCycleDelay = 132;

	const std::uint16_t ACR_RFGate_bitmask = 0x0001;
	const std::uint16_t ACR_EXTEn_bitmask = 0x0002;
	const std::uint16_t ACR_RFBias_bitmask = 0x0030;

	const std::uint16_t ACR_RFGate_ON = 0x0001;
	const std::uint16_t ACR_RFGate_OFF = 0x0000;
	const std::uint16_t ACR_EXTEn_ON = 0x0000;
	const std::uint16_t ACR_EXTEn_OFF = 0x0002;
	const std::uint16_t ACR_RFBias12_OFF = 0x0010;
	const std::uint16_t ACR_RFBias12_ON = 0x0000;
	const std::uint16_t ACR_RFBias34_OFF = 0x0020;
	const std::uint16_t ACR_RFBias34_ON = 0x0000;

	const std::uint16_t SYNTH_REG_ETMControl_bits_Channel_mask = 0x3;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Channel_shift = 0;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Scaling_mask = 0xC;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Scaling_shift = 2;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Function_mask = 0xF00;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Function_shift = 8;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Program_mask = 0x20;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Trigger_mask = 0x40;
	const std::uint16_t SYNTH_REG_ETMControl_bits_Clear_mask = 0x80;

	const std::uint16_t ENC_Control_ENC_Mode_Quadrature = 0;
	const std::uint16_t ENC_Control_ENC_Mode_Clk_Dir = 1;
	const std::uint16_t ENC_Control_Vel_Mode_Fast = 0;
	const std::uint16_t ENC_Control_Vel_Mode_Slow = 2;

	const std::uint16_t CTRLR_REG_NumPts = 48;
	const std::uint16_t CTRLR_REG_OscFreq = 49;
	const std::uint16_t CTRLR_REG_ExtDiv = 69;
	const std::uint16_t CTRLR_REG_ImgDelay = 50;
	const std::uint16_t CTRLR_REG_ImgModes = 51;
	const std::uint16_t CTRLR_REG_ExtPolarity = 52;
	const std::uint16_t CTRLR_REG_Img_Play = 53;
	const std::uint16_t CTRLR_REG_Img_Ctrl = 54;
	const std::uint16_t CTRLR_REG_Img_Progress = 55;
	const std::uint16_t CTRLR_REG_UUID = 56;
	const std::uint16_t CTRLR_REG_NumPtsLo = 64;
	const std::uint16_t CTRLR_REG_NumPtsHi = 65;
	const std::uint16_t CTRLR_REG_Img_ProgressLo = 66;
	const std::uint16_t CTRLR_REG_Img_ProgressHi = 67;
	const std::uint16_t CTRLR_REG_ImgModesExt = 70;
	const std::uint16_t CTRLR_REG_ClockOutput = 71;
	const std::uint16_t CTRLR_REG_DutyCycle = 72;
	const std::uint16_t CTRLR_REG_OscPhase = 73;
	const std::uint16_t CTRLR_REG_FPIFormat = 74;
	const std::uint16_t CTRLR_REG_FPIFormat_MSB_Supported = 0x0001;  // read-only
	const std::uint16_t CTRLR_REG_FPIFormat_MSB_Enable = 0x0002;

	const std::uint16_t SYNTH_REG_Img_FormatLo = 72;
	const std::uint16_t SYNTH_REG_Img_FormatHi = 73;

	const std::uint16_t CTRLR_REG_Img_Play_FSTOP = 1;
	const std::uint16_t CTRLR_REG_Img_Play_STOP = 2;
	const std::uint16_t CTRLR_REG_Img_Play_RUN = 4;
	const std::uint16_t CTRLR_REG_Img_Play_ERUN = 8;

	const std::uint16_t CTRLR_REG_Img_Ctrl_IOS_Busy = 0x0001;
	const std::uint16_t CTRLR_REG_Img_Ctrl_DL_Active = 0x0002;
	const std::uint16_t CTRLR_REG_Img_Ctrl_Common_Channels = 0x0004;
	const std::uint16_t CTRLR_REG_Img_Ctrl_Prescaler_Disable = 0x0008;

	const std::uint16_t CTRLR_SYNDMA_Start_DMA = 0;
	const std::uint16_t CTRLR_SYNDMA_DMA_Abort = 2;

	const std::uint16_t CTRLR_SEQPLAY_Seq_Start = 0;
	const std::uint16_t CTRLR_SEQPLAY_USR_Trig = 1;
	const std::uint16_t CTRLR_SEQPLAY_Seq_Stop = 2;
	const std::uint16_t CTRLR_SEQPLAY_Seq_Pause = 3;
	const std::uint16_t CTRLR_SEQPLAY_Seq_Restart = 4;

	const std::uint16_t CTRLR_INTERRUPT_SINGLE_IMAGE_FINISHED = 0;
	const std::uint16_t CTRLR_INTERRUPT_SEQUENCE_START = 1;
	const std::uint16_t CTRLR_INTERRUPT_SEQUENCE_FINISHED = 2;
	const std::uint16_t CTRLR_INTERRUPT_SEQUENCE_ERROR = 3;
	const std::uint16_t CTRLR_INTERRUPT_TONE_START = 5;
	const std::uint16_t CTRLR_INTERRUPT_SEQDL_ERROR = 6;
	const std::uint16_t CTRLR_INTERRUPT_SEQDL_COMPLETE = 7;
	const std::uint16_t CTRLR_INTERRUPT_SEQDL_BUFFER_PROCESSED = 8;

}

#endif
