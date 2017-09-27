#ifndef TCP_COMMON_7DA365BEF7C44E2CA1AC38F4E810DA88_H
#define TCP_COMMON_7DA365BEF7C44E2CA1AC38F4E810DA88_H

#include <sys/types.h>

#define MSG_LINK_CONNECT  0
#define MSG_DATA 1
#define MSG_LINK_DISCONNECT 2 

enum eErrorCode {
	E_NO_ERROR = 0,
	E_SERVICE_ALREADY_RUNNING = 1,
	E_SERVICE_PORT_USED = 2,
	E_SERVICE_SOCKET_ERROR = 3,
	E_INVALID_CLIENT_ENDPOINT = 4,
	E_CLIENT_SOCKET_ERROR = 5,
	E_SERVICE_CONCURRENCU_REACH_MAX = 6,
	E_SERVICE_ALLOCATE_INSTANCE_ERROR = 7,
	E_INVALID_SERVICE_HANDLE = 8,
	E_SERVICE_NOT_RUNNING = 9,



};



typedef struct tagMessageContent
{
	char szEndPoint[32];
	unsigned char * pMsgData;
	unsigned long ulMsgDataLen;
	unsigned long ulMsgTime;
	tagMessageContent()
	{
		szEndPoint[0] = '\0';
		pMsgData = 0;
		ulMsgDataLen = 0;
		ulMsgTime = 0;
	}
} MessageContent;

typedef void(__stdcall *fMessageCallback)(int, void *, void *);

#endif