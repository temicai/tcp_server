#ifndef TCP_CONCRETE_SERVER__H_CEAB29558442414197F0F1328F59D844
#define TCP_CONCRETE_SERVER__H_CEAB29558442414197F0F1328F59D844

#undef FD_SETSIZE
#define FD_SETSIZE 1024

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <time.h>
#include <chrono>
#include <thread>
#include <string>
#include <map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "tcp_common.h"
#include "pf_log.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "pf_log.lib")

namespace ts
{
	enum eEventType
	{
		EVENT_ADD = 1,
		EVENT_DELETE = 2,
		EVENT_DATA = 3,
		EVENT_SOCK_CLOSE = 4,
	};

	typedef struct tagEvent
	{
		uint32_t nEventType;
		uint32_t nDataLen;
		unsigned char * pEventData;
		tagEvent()
		{
			nEventType = 0;
			nDataLen = 0;
			pEventData = nullptr;
		}
	} Event;

	typedef SOCKET fd_t;

	typedef struct tagSocketData
	{
		SOCKET sock;
		char * pData;
		unsigned int uiDataLen;
		tagSocketData()
		{
			sock = -1;
			pData = NULL;
			uiDataLen = 0;
		}
	} SocketData;

	typedef struct tagSocketInfo
	{
		SOCKET sock;
		int nSockType;
		tagSocketInfo()
		{
			sock = -1;
			nSockType = -1;
		}
	} SocketInfo;

	typedef struct tagEndpoint
	{
		fd_t fd;
		char szIp[20];
		int nPort;
		int nType : 16; //0:listen,1:client
		int nTransferData : 16;
		unsigned long long ulTime;
		std::string toString()
		{
			char szEndpoint[32] = { 0 };
			sprintf_s(szEndpoint, 32, "%s:%d", szIp, nPort);
			return std::string(szEndpoint);
		}
		tagEndpoint()
		{
			fd = -1;
			szIp[0] = '\0';
			nPort = 0;
			ulTime = 0;
			nType = 0;
			nTransferData = 0;
		}
	} Endpoint;

	class tcp_server_t
	{
	public:
		explicit tcp_server_t(char * pPath = nullptr);
		~tcp_server_t();
		int Start(unsigned short usPort, fMessageCallback fMsg, void * pUserData);
		void Stop();
		void SetMessageCallback(fMessageCallback fMsg, void * pUserData);
		void SetWaitDataTimeout(int nTimeout);
		int SendData(const char * pLinkId, const char * pData, unsigned int uiDataLen);
		unsigned short GetPort();
		int CloseLink(const char * pLinkId);
		friend void recv2(void *);
		friend void supervise(void *);
		friend void eventDispatch(void *);
	protected:
		bool addEndpoint(Endpoint *);
		void addSock(ts::SocketInfo *);
		bool addEvent(ts::Event *);
		void dispatch();

	private:
		unsigned short m_usPort;
		bool m_bStop;
		std::vector<Endpoint *> m_endpoints;
		std::mutex m_mutex4Endpoints;
		std::thread m_thdReceiver;
		std::thread m_thdSupervisor;
		std::thread m_thdEvent;
		fMessageCallback m_msgCb;
		void * m_pUserData;
		int m_nTimeout; //second
		unsigned long long m_ullInst;

		std::vector<ts::SocketInfo *> m_sockList;
		std::mutex m_mutex4SockList;

		std::queue<ts::Event *> m_eventQue;
		std::mutex m_mutex4EventQue;
		std::condition_variable m_cond4EventQue;
	};

}




#endif
