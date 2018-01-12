#include "tcp_server.h"
#include "tcp_concrete_server.h"

#include <Windows.h>
//#include <set>

std::map<unsigned long long, ts::tcp_server_t *> g_tsList;
std::mutex g_mutex4TsList;
char g_szDllPath[256];

BOOL APIENTRY DllMain(void * hInst_, DWORD dwReason_, void * pReserved_)
{
	if (dwReason_ == DLL_PROCESS_ATTACH) {
		g_tsList.clear();
		g_szDllPath[0] = '\0';
		char szPath[256] = { 0 };
		if (GetModuleFileNameA((HMODULE)hInst_, szPath, sizeof(szPath)) != 0) {
			char szDrive[32] = { 0 };
			char szDir[256] = { 0 };
			_splitpath_s(szPath, szDrive, sizeof(szDrive), szDir, sizeof(szDir), nullptr, 0, nullptr, 0);
			sprintf_s(g_szDllPath, sizeof(g_szDllPath), "%s%s", szDrive, szDir);
		}
	}
	else if (dwReason_ == DLL_PROCESS_DETACH) {
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.begin();
			while (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					delete pTcpSrv;
					pTcpSrv = NULL;
				}
				iter = g_tsList.erase(iter);
			}
		}
	}
	return TRUE;
}

unsigned long long __stdcall TS_StartServer(unsigned int uiPort_, fMessageCallback fMsgCb_,
	void * pUserData_, int nIdleTime_)
{
	unsigned long long result = 0;
	{
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (g_tsList.size() < MAX_PARALLEL_INSTANCE_NUM) {
			ts::tcp_server_t * pTcpSrv = new ts::tcp_server_t(g_szDllPath);
			if (pTcpSrv->Start((unsigned short)uiPort_, fMsgCb_, pUserData_) != -1) {
				pTcpSrv->SetWaitDataTimeout(nIdleTime_);
				unsigned long long ullKey = (unsigned long long)pTcpSrv;
				g_tsList.emplace(ullKey, pTcpSrv);
				result = ullKey;
			}
			else {
				delete pTcpSrv;
				pTcpSrv = NULL;
			}
		}
	}
	return result;
}

int __stdcall TS_StopServer(unsigned long long ulServerInst_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					pTcpSrv->Stop();
					result = 0;
					delete pTcpSrv;
					pTcpSrv = nullptr;
				}
				g_tsList.erase(iter);
			}
		}
	}
	return result;
}

int __stdcall TS_SetLogType(unsigned long long ulServerInst_, int nLogType_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					result = 0;
				}
			}
		}
	}
	return result;
}

int __stdcall TS_SendData(unsigned long long ulServerInst_, const char * pEndpoint_, const char * pData_, 
	unsigned int uiDataLen_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					if (pTcpSrv->SendData(pEndpoint_, pData_, uiDataLen_) != -1) {
						result = 0;
					}
				}
			}
		}
	}
	return result;
}

int __stdcall TS_GetPort(unsigned long long ulServerInst_, unsigned int & uiPort_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					unsigned short usPort = pTcpSrv->GetPort();
					uiPort_ = (unsigned int)usPort;
					result = 0;
				}
			}
		}
	}
	return result;
}

int __stdcall TS_SetMessageCallback(unsigned long long ulServerInst_, fMessageCallback fMsgCb_, void * pUserData_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					pTcpSrv->SetMessageCallback(fMsgCb_, pUserData_);
					result = 0;
				}
			}
		}
	}
	return result;
}

int __stdcall TS_CloseEndpoint(unsigned long long ulServerInst_, const char * pEndpoint_)
{
	int result = -1;
	if (ulServerInst_ > 0) {
		std::lock_guard<std::mutex> lock(g_mutex4TsList);
		if (!g_tsList.empty()) {
			std::map<unsigned long long, ts::tcp_server_t *>::iterator iter = g_tsList.find(ulServerInst_);
			if (iter != g_tsList.end()) {
				ts::tcp_server_t * pTcpSrv = iter->second;
				if (pTcpSrv) {
					pTcpSrv->CloseLink(pEndpoint_);
					result = 0;
				}
			}
		}
	}
	return result;
}

//std::mutex gMutex4LinkList;
//std::set<std::string> gLinkList;
//
//void __stdcall msgCb(int nMsgType, void * pMsgData, void * pUserData_)
//{
//	if (nMsgType == MSG_LINK_CONNECT) {
//		std::string strLink = (const char *)pMsgData;
//		{
//			std::lock_guard<std::mutex> lock(gMutex4LinkList);
//			gLinkList.emplace(strLink);
//		}
//	}
//	else if (nMsgType == MSG_DATA) {
//
//	}
//	else if (nMsgType = MSG_LINK_DISCONNECT) {
//		std::string strLink = (const char *)pMsgData;
//		{
//			std::lock_guard<std::mutex> lock(gMutex4LinkList);
//			if (gLinkList.count(strLink) != 0) {
//				gLinkList.erase(strLink);
//			}
//		}
//	}
//}
//
//int main()
//{
//	unsigned int uiSrvInst = TS_StartServer(20000, 0, msgCb, NULL, 30);
//	if (uiSrvInst > 0) {
//		printf("working 20000\n");
//		while (1) {
//			char c = 0;
//			scanf_s("%c", &c, 1);
//			if (c == 'q') {
//				break;
//			}
//			else if (c == 's') {
//				{
//					std::string strLink;
//					{
//						std::lock_guard<std::mutex> lock(gMutex4LinkList);
//						if (!gLinkList.empty()) {
//							std::set<std::string>::iterator iter = gLinkList.begin();
//							strLink = *iter;
//						}
//					}
//					char szMsg[64] = { 0 };
//					sprintf_s(szMsg, sizeof(szMsg), "hello, %s", strLink.c_str());
//					int nVal = TS_SendData(uiSrvInst, strLink.c_str(), szMsg, strlen(szMsg));
//					if (nVal == -1) {
//						TS_CloseEndpoint(uiSrvInst, strLink.c_str());
//					}
//				}
//			}
//			else if (c == 'c') {
//				std::string strLink;
//				{
//					std::lock_guard<std::mutex> lock(gMutex4LinkList);
//					std::set<std::string>::iterator iter = gLinkList.begin();
//					strLink = *iter;
//				}
//				TS_CloseEndpoint(uiSrvInst, strLink.c_str());
//			}
//		}
//		TS_StopServer(uiSrvInst);
//	}
//	return 0;
//}