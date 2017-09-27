#include <stdio.h>
#include "../tcp_server/tcp_server.h"

#pragma comment(lib, "tcp_server.lib")

#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <queue>
#include <condition_variable>

typedef struct tagReplyParam
{
	char szEndpoint[20];
	char * pReplyData;
	unsigned int uiReplyDataLen;
	tagReplyParam()
	{
		init();
	}
	void init()
	{
		pReplyData = NULL;
		uiReplyDataLen = 0;
		memset(szEndpoint, 0, sizeof(szEndpoint));
	}
	~tagReplyParam()
	{
		if (pReplyData && uiReplyDataLen > 0) {
			delete[] pReplyData;
			pReplyData = NULL;
			uiReplyDataLen = 0;
		}
	}
} ReplyParam;

bool gRun;
std::mutex gMutex4LinkList;
std::set<std::string> gLinkList;
unsigned long long gSrvInst;
std::queue<ReplyParam *> gReplyQue;
std::condition_variable gCond4ReplyQue;
std::mutex gMutex4ReplyQue;

bool addReply(ReplyParam *);
void dealReply();

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
			size_t nLen = pMsgContent->ulMsgDataLen;
			char * pData = new char[nLen + 1];
			memcpy_s(pData, nLen + 1, pMsgContent->pMsgData, nLen);
			pData[nLen] = '\0';
			ReplyParam * pReply = new ReplyParam();
			pReply->init();
			strcpy_s(pReply->szEndpoint, sizeof(pReply->szEndpoint), pMsgContent->szEndPoint);
			pReply->uiReplyDataLen = (unsigned int)nLen;
			pReply->pReplyData = new char[nLen + 1];
			memcpy_s(pReply->pReplyData, nLen + 1, pData, nLen);
			pReply->pReplyData[nLen] = '\0';
			if (!addReply(pReply)) {
				delete pReply;
				pReply = NULL;
			}
			delete[] pData;
			pData = NULL;
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
	gSrvInst = 0;
	unsigned short usPort = 20000;
	if (argc > 1) {
		usPort = (unsigned short)atoi(argv[1]);
	}
	unsigned long long ullSrvInst = TS_StartServer(usPort, 0, msgCb, NULL, 30);
	if (ullSrvInst > 0) {
		gRun = true;
		printf("working %hu\n", usPort);
		gSrvInst = ullSrvInst;
		std::thread t1(dealReply);
		while (1) {
			char c = 0;
			scanf_s("%c", &c, 1);
			if (c == 'q') {
				gRun = false;
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
					int nVal = TS_SendData(ullSrvInst, strLink.c_str(), szMsg, (unsigned int)strlen(szMsg));
					if (nVal == -1) {
						TS_CloseEndpoint(ullSrvInst, strLink.c_str());
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
				TS_CloseEndpoint(ullSrvInst, strLink.c_str());
			}
		}
		TS_StopServer(ullSrvInst);
		gCond4ReplyQue.notify_all();
		t1.join();
		gSrvInst = 0;
	}
	return 0;
}

bool addReply(ReplyParam * pReply)
{
	if (pReply) {
		std::lock_guard<std::mutex> lock(gMutex4ReplyQue);
		gReplyQue.emplace(pReply);
		if (gReplyQue.size() == 1) {
			gCond4ReplyQue.notify_one();
		}
		return true;
	}
	return false;
}

void dealReply()
{
	do {
		ReplyParam * pReply = NULL;
		{
			std::unique_lock<std::mutex> lock(gMutex4ReplyQue);
			gCond4ReplyQue.wait(lock, [&] {
				return (!gReplyQue.empty() || !gRun);
			});
			if (gReplyQue.empty() && !gRun) {
				break;
			}
			pReply = gReplyQue.front();
			gReplyQue.pop();
		}
		if (pReply) {
			if (gSrvInst) {
				int n = TS_SendData(gSrvInst, pReply->szEndpoint, pReply->pReplyData, pReply->uiReplyDataLen);
				printf("send reply %u data to %s: %d\n", pReply->uiReplyDataLen, pReply->szEndpoint, n);
			}
			delete pReply;
			pReply = NULL;
		}
	} while (1);
}

