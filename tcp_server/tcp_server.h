#ifndef TCP_SERVER_H_309AA631578A45BEAEFAC6AEB58C302A
#define TCP_SERVER_H_309AA631578A45BEAEFAC6AEB58C302A

#include "tcp_common.h"

#define MAX_PARALLEL_INSTANCE_NUM 32


extern "C"
{
	unsigned int __stdcall TS_StartServer(unsigned int uiPort, int nLogType, fMessageCallback fMsgCb,
		void * pUserData, int nIdleTime);
	int __stdcall TS_StopServer(unsigned int uiServerInst);
	int __stdcall TS_SetLogType(unsigned int uiServerInst, int nLogType);
	int __stdcall TS_SendData(unsigned int uiServerInst, const char * pEndpoint, const char * pData, unsigned int uiDataLen);
	int __stdcall TS_GetPort(unsigned int uiServerInst, unsigned int & uiPort);
	int __stdcall TS_SetMessageCallback(unsigned int uiServerInst, fMessageCallback fMsgCb, void * pUserData);
	int __stdcall TS_CloseEndpoint(unsigned int uiServerInst, const char * pEndpoint);
}

#endif