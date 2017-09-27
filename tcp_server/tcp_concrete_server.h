#ifndef TCP_CONCRETE_SERVER__H_CEAB29558442414197F0F1328F59D844
#define TCP_CONCRETE_SERVER__H_CEAB29558442414197F0F1328F59D844

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <string>
#include <time.h>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include "tcp_common.h"

#pragma comment(lib, "ws2_32.lib")

namespace ts
{

	typedef SOCKET fd_t;

	typedef struct tagEndpoint
	{
		fd_t fd;
		char szIp[20];
		int nPort;
		int nType : 16;
		int nTransferData : 16;
		unsigned long ulTime;
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
		tcp_server_t();
		~tcp_server_t();
		int Start(unsigned short usPort, fMessageCallback fMsg, void * pUserData);
		void Stop();
		void SetMessageCallback(fMessageCallback fMsg, void * pUserData);
		void SetWaitDataTimeout(int nTimeout);
		int SendData(const char * pLinkId, const char * pData, unsigned int uiDataLen);
		unsigned short GetPort();
		int CloseLink(const char * pLinkId);
		friend void recv(void *);
		friend void supervise(void *);
	protected:
		bool addEndpoint(Endpoint *);
	private:
		unsigned short m_usPort;
		bool m_bStop;
		std::vector<Endpoint *> m_endpoints;
		std::mutex m_mutex4Endpoints;
		std::thread m_thdReceiver;
		std::thread m_thdSupervisor;
		fMessageCallback m_msgCb;
		void * m_pUserData;
		int m_nTimeout; //second
	};

}




#endif
