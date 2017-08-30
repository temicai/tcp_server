#include <stdio.h>
#include "../tcp_server/tcp_server.h"

#pragma comment(lib, "tcp_server.lib")

#include <mutex>
#include <set>
#include <string>


std::mutex gMutex4LinkList;
std::set<std::string> gLinkList;

void __stdcall msgCb(int nMsgType, void * pMsgData, void * pUserData_)
{
	if (nMsgType == MSG_LINK_CONNECT) {
		std::string strLink = (const char *)pMsgData;
		{
			std::lock_guard<std::mutex> lock(gMutex4LinkList);
			gLinkList.emplace(strLink);
			printf("connection %s\n", strLink.c_str());
		}
	}
	else if (nMsgType == MSG_DATA) {
		MessageContent * pMsgContent = (MessageContent *)pMsgData;
		if (pMsgContent) {
			printf("recv from %s, %d data\n", pMsgContent->szEndPoint, pMsgContent->ulMsgDataLen);
		}
	}
	else if (nMsgType = MSG_LINK_DISCONNECT) {
		std::string strLink = (const char *)pMsgData;
		{
			std::lock_guard<std::mutex> lock(gMutex4LinkList);
			if (gLinkList.count(strLink) != 0) {
				gLinkList.erase(strLink);
				printf("disconnection %s\n", strLink.c_str());
			}
		}
	}
}

int main(int argc, char ** argv)
{
	unsigned short usPort = 20000;
	if (argc > 1) {
		usPort = (unsigned short)atoi(argv[1]);
	}
	unsigned int uiSrvInst = TS_StartServer(usPort, 0, msgCb, NULL, 30);
	if (uiSrvInst > 0) {
		printf("working %hu\n", usPort);
		while (1) {
			char c = 0;
			scanf_s("%c", &c, 1);
			if (c == 'q') {
				break;
			}
			else if (c == 's') {
				{
					std::string strLink;
					{
						std::lock_guard<std::mutex> lock(gMutex4LinkList);
						if (!gLinkList.empty()) {
							std::set<std::string>::iterator iter = gLinkList.begin();
							strLink = *iter;
						}
					}
					char szMsg[64] = { 0 };
					sprintf_s(szMsg, sizeof(szMsg), "hello, %s", strLink.c_str());
					int nVal = TS_SendData(uiSrvInst, strLink.c_str(), szMsg, strlen(szMsg));
					if (nVal == -1) {
						TS_CloseEndpoint(uiSrvInst, strLink.c_str());
					}
				}
			}
			else if (c == 'c') {
				std::string strLink;
				{
					std::lock_guard<std::mutex> lock(gMutex4LinkList);
					std::set<std::string>::iterator iter = gLinkList.begin();
					strLink = *iter;
				}
				TS_CloseEndpoint(uiSrvInst, strLink.c_str());
			}
		}
		TS_StopServer(uiSrvInst);
	}
	return 0;
}