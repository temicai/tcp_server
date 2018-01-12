#ifndef TCP_SERVER_H_309AA631578A45BEAEFAC6AEB58C302A
#define TCP_SERVER_H_309AA631578A45BEAEFAC6AEB58C302A

#include "tcp_common.h"

#define MAX_PARALLEL_INSTANCE_NUM 64


extern "C"
{
	unsigned long long __stdcall TS_StartServer(unsigned int uiPort, fMessageCallback fMsgCb, void * pUserData, int nIdleTime);
	int __stdcall TS_StopServer(unsigned long long ullServerInst);
	int __stdcall TS_SetLogType(unsigned long long ullServerInst, int nLogType);
	int __stdcall TS_SendData(unsigned long long ullServerInst, const char * pEndpoint, const char * pData, unsigned int uiDataLen);
	int __stdcall TS_GetPort(unsigned long long ullServerInst, unsigned int & uiPort);
	int __stdcall TS_SetMessageCallback(unsigned long long ullServerInst, fMessageCallback fMsgCb, void * pUserData);
	int __stdcall TS_CloseEndpoint(unsigned long long ullServerInst, const char * pEndpoint);
}

#endif