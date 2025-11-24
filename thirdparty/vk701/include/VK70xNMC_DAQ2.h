
#ifndef __DS_WIN32DLL_VKINGING_H
#define __DS_WIN32DLL_VKINGING_H



//==============================================================================
// ������� stdcall������ _OUT+CDCEL+DLL���壬ͬʱ���� ��Ŀ������ -��������-����-ģ�鶨���ļ� {VK70xNHMC_DAQ.def}
// ������� cdcel���� _OUT+CDCEL+DLL����
//==============================================================================
#define DLL_OUT_CDCEL_STDCALL  1 // 0=CDCEL; 1=STDCALL
#define _DEBUG_VK70xxMC_LIB_   0
//==============================================================================
#if(DLL_OUT_CDCEL_STDCALL == 0)
//ȡ��VK70xNHMC_DAQ.def
#define VK70xnMCDLL_fmt  __declspec(dllexport)
//#define VK70xnMCDLL_fmt  __declspec(dllimport)
#define Def_Function_OUTfmt

#elif (DLL_OUT_CDCEL_STDCALL==1)

//��Ŀ����>������>����>ģ�鶨���ļ���VK70xNHMC_DAQ.def��
#define VK70xnMCDLL_fmt
#define Def_Function_OUTfmt

#else
#error ��error define cdcel and stdcall!!!��
#endif

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// Include files
//#include "cvidef.h"
//==============================================================================
// Constants

//==============================================================================
// Types
//------------------------------------
//int  SampleRate;           // ������
//int  SampleRateResolution; // �����ֱ���
//int  Sampling_Channel;     // ����ͨ��
//====================================
//==============================================================================
// External variables

//==============================================================================
// Global functions
//int Your_Functions_Here (int x);
////VK70xNMC_Intialize(double refmode, int uCH15VR, int uCH26VR,int uCH37VR,int uCH48VR);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Initialize(int mci, double refvol, int bitmode, int sr, int volrg15, int volrg26, int volrg37, int volrg48);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Initialize_2(int mci, double refvol, int bitmode, int sr, int npoints, int timeinval, int *volrg);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_StartSampling(int mci);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_StartSampling_NPoints(int mci, int Npointsnums);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_StopSampling(int mci);
//------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Set_AdditionalFeature(int mci, int funcNo, int para1, double para2);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Set_SystemMode(int mci, int modelval,int  samplemethod,int  sdfilefmt);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Set_eNetParameter(int mci, char *para, int len);		//VK702HU_StopSampling
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Set_DeviceDefaultParameter(int mci, char *wrmodel, char *wrsn, int *para);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_Set_DefaultParameter(int mci, int *para);
//-------------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_ADCConfigParameter(int mci, int *sr, int *npoints, int *timeintervals, int *bitmode, int *para, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_SystemMode(int mci, int *sysmode, int *samplecmd, int *sdfilefmt, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_DeviceDefaultParameter(int mci, char *rdmodol, char *rdswver, char *rdhdver, char *rdsn, int *para, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_eNetParameter(int mci, char *tvalue, int timeout);
//===================================
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetOneChannel(int mci, int CHNum, double *adcbuffer, int rsamplenum);// for VK701N,VK701S,VK702N
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetFourChannel(int mci, double *adcbuffer, int rsamplenum);// for VK701N,VK701S
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetAllChannel(int mci, double *adcbuffer, int rsamplenum); // for VK702N
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetOneChannel_WithIOStatus(int mci, int CHNum, double *adcbuffer, int rsamplenum, int ioenable);// for VK702N
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetFourChannel_WithIOStatus(int mci, double *adcbuffer, int rsamplenum, int ioenable);// for VK702N
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetAllChannel_WithIOStatus(int mci, double *adcbuffer, int rsamplenum, int ioenable); // for VK702N
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetFixedFourChannel_CH1CH2_IO2IO3(int mci, double *adcbuffer, int rsamplenum);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetFixedFourChannel_CH3CH4_IO2IO3(int mci, double *adcbuffer, int rsamplenum);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  VK70xNMC_GetIOStatus(int mci, int *iostatus);
VK70xnMCDLL_fmt char* Def_Function_OUTfmt VK70xNMC_GetVersionLot(void);
//-----------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_AllIOS_Blocking(int mci, int *iobuffer, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_IO1_Blocking(int mci, int *iovalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_IO2_Blocking(int mci, int *iovalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_IO3_Blocking(int mci, int *iovalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_IO4_Blocking(int mci, int *iovalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_PWM_Blocking(int mci, double *dutyval, int *freqval, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_DAC_Blocking(int mci, double *dacvalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_Counter_Blocking(int mci, int *countervalue, int timeout);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_Temperature_Blocking(int mci, double *tempvalue, int timeout);
//VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_Counter_Blocking(int mci, int *countervalue, int timeout);
//VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_Temperature_Blocking(int mci, double *tempvalue, int timeout);
//------------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Set_SaveFileDeafultPath(int tflag, const char *defaultdir);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_SDFileRuningInformation(int mci, int *para);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Enter_SDFileSystem(int mci);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Exit_SDFileSystem(int mci);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_SDFileDIR(int mci);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_OneSDFile(int mci, int fileindex);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_MultiSDFiles(int mci);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Delete_OneSDFile(int mci, int fileindex);
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Delete_MultiSDFiles(int mci);
//===========================================
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Get_ConvertBin2Text_Progress(int *precent, int *tstatus);//progress
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Start_ConvertBin2Text(const char *filename);
//------------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt VK70xNMC_Set_DeviceEnterUpgradeMode(int mci);
//------------------------------------------
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_TCPClose(int portnumber);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_TCPOpen(int portnumber);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_Get_ServerPort(int *iport);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_Get_ConnectedClientNumbers(int *cnum);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_Get_ConnectedClientHandle(int mci, int *ihadble, char *ipadr); // ��ȡ�����Ӳɼ���handle��IP��ַ
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_Get_RxTotoalBytes(int *totalbytesnum, int clrflag);
VK70xnMCDLL_fmt int Def_Function_OUTfmt  Server_Bind_ConnectedClientIP(int tflag);
////////////////////////////////////////////////////
#ifdef __cplusplus
}
#endif

#endif
