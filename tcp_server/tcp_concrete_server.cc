#include <stdio.h>
#include "tcp_concrete_server.h"

ts::tcp_server_t::tcp_server_t(char * pPath_)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	m_bStop = true;
	m_nTimeout = 30;
	m_msgCb = NULL;
	m_pUserData = NULL;
	m_usPort = 0;
	m_sockList.reserve(1024 * sizeof(ts::SocketInfo));
	m_ullInst = 0;
	m_ullInst = LOG_Init();
	if (m_ullInst) {
		pf_logger::LogConfig logConf;
		memset(&logConf, 0, sizeof(pf_logger::LogConfig));
		logConf.usLogPriority = pf_logger::eLOGPRIO_ALL;
		logConf.usLogType = pf_logger::eLOGTYPE_FILE;
		char szPath[256] = { 0 };
		if (pPath_ && strlen(pPath_)) {
			sprintf_s(szPath, sizeof(szPath), "%slog\\", pPath_);
		}
		else {
			char szPath[256] = { 0 };
			GetDllDirectoryA(sizeof(szPath), szPath);
			strcat_s(szPath, sizeof(szPath), "log\\");
		}
		CreateDirectoryExA(".\\", szPath, NULL);
		strcat_s(szPath, sizeof(szPath), "tcp_server\\");
		CreateDirectoryExA(".\\", szPath, NULL);
		strcpy_s(logConf.szLogPath, sizeof(logConf.szLogPath), szPath);
		LOG_SetConfig(m_ullInst, logConf);
	}
}

ts::tcp_server_t::~tcp_server_t()
{
	if (!m_bStop) {
		Stop();
	}
	if (m_ullInst) {
		LOG_Release(m_ullInst);
		m_ullInst = 0;
	}
	WSACleanup();
}

int ts::tcp_server_t::Start(unsigned short usPort_, fMessageCallback fMsgCb_, void * pUserData_)
{
	ts::fd_t fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_port = htons(usPort_);
	address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	int nReuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&nReuse, sizeof(nReuse));
	int nBuf = 512 * 1024;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char *)&nBuf, sizeof(nBuf));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&nBuf, sizeof(nBuf));
	if (::bind(fd, (const sockaddr *)&address, sizeof(address)) == 0) {
	}
	else {
		closesocket(fd);
		return -1;
	}
	listen(fd, SOMAXCONN);
	m_bStop = false;
	m_usPort = usPort_;
	
	//new part//
	ts::SocketInfo * pSockInfo = new ts::SocketInfo();
	pSockInfo->sock = fd;
	pSockInfo->nSockType = 0;
	addSock(pSockInfo);

	m_msgCb = fMsgCb_;
	m_pUserData = pUserData_;

	m_thdReceiver = std::thread(ts::recv2, this);
	m_thdSupervisor = std::thread(ts::supervise, this);
	m_thdEvent = std::thread(ts::eventDispatch, this);
	char szLog[256] = { 0 };
	sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]start tcp server at port=%hu\n",
		__FUNCTION__, __LINE__, usPort_);
	if (m_ullInst > 0) {
		LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
	}
	return 0;
}

void ts::tcp_server_t::Stop()
{
	if (m_bStop) {
		return;
	}
	char szLog[256] = { 0 };
	sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]stop tcp server\n", __FUNCTION__,
		__LINE__);
	if (m_ullInst > 0) {
		LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
	}
	m_bStop = true;
	m_thdReceiver.join();
	m_thdSupervisor.join();

	m_cond4EventQue.notify_all();
	m_thdEvent.join();

	if (!m_endpoints.empty()) {
		std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
		while (iter != m_endpoints.end()) {
			ts::Endpoint * pEndpoint = *iter;
			if (pEndpoint) {
				if (pEndpoint->fd != -1) {
					shutdown(pEndpoint->fd, SD_BOTH);
					closesocket(pEndpoint->fd);
					pEndpoint->fd = -1;
				}
				delete pEndpoint;
				pEndpoint = NULL;
			}
			iter = m_endpoints.erase(iter);
		}
	}
	if (!m_sockList.empty()) {
		m_sockList.clear();
	}
}

void ts::tcp_server_t::SetMessageCallback(fMessageCallback fMsgCb_, void * pUserData_)
{
	m_msgCb = fMsgCb_;
	m_pUserData = pUserData_;
}

void ts::tcp_server_t::SetWaitDataTimeout(int nTimeout_)
{
	if (nTimeout_ <= 30) {
		m_nTimeout = 30;
	}
	else {
		m_nTimeout = nTimeout_;
	}
}

int ts::tcp_server_t::SendData(const char * pLinkId_, const char * pData_, unsigned int uiDataLen_)
{
	int result = -1;
	if (pLinkId_ && strlen(pLinkId_)) {
		SOCKET closeSock = -1;
		std::lock_guard<std::mutex> lock(m_mutex4Endpoints);
		std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
		while (iter != m_endpoints.end()) {
			ts::Endpoint * pEndpoint = *iter;
			if (pEndpoint) {
				if (strcmp(pLinkId_, pEndpoint->toString().c_str()) == 0) {
					if (send(pEndpoint->fd, pData_, (int)uiDataLen_, 0) != SOCKET_ERROR) {
						result = 0;
					}
					else {
						if (m_msgCb) {
							m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), m_pUserData);
						}
						char szLog[128] = { 0 };
						sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]SendData failed, close link=%s, fd=%d\n",
							__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd);
						if (m_ullInst) {
							LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
						}
						closeSock = pEndpoint->fd;
						if (pEndpoint->fd != -1) {
							shutdown(pEndpoint->fd, SD_BOTH);
							closesocket(pEndpoint->fd);
							pEndpoint->fd = -1;
						}
						delete pEndpoint;
						pEndpoint = NULL;
						iter = m_endpoints.erase(iter);
					}
					break;
				}
			}
			iter++;
		}
		if (closeSock != -1) {
			std::lock_guard<std::mutex> lock(m_mutex4SockList);
			std::vector<ts::SocketInfo *>::iterator it = m_sockList.begin();
			for (; it != m_sockList.end(); it++) {
				ts::SocketInfo * pSockInfo = *it;
				if (pSockInfo) {
					if (pSockInfo->sock == closeSock) {
						delete pSockInfo;
						pSockInfo = NULL;
						m_sockList.erase(it);
						break;
					}
				}
			}
		}
	}
	return result;
}

unsigned short ts::tcp_server_t::GetPort()
{
	return m_usPort;
}

int ts::tcp_server_t::CloseLink(const char * pLinkId_)
{
	int result = -1;
	if (pLinkId_ && strlen(pLinkId_)) {
		SOCKET closeSock = -1;
		std::lock_guard<std::mutex> lock(m_mutex4Endpoints);
		if (!m_endpoints.empty()) {
			std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
			while (iter != m_endpoints.end()) {
				ts::Endpoint * pEndpoint = *iter;
				if (pEndpoint) {
					if (strcmp(pLinkId_, pEndpoint->toString().c_str()) == 0) {
						if (m_msgCb) {
							m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), m_pUserData);
						}
						char szLog[256] = { 0 };
						sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]CloseLink: %s, fd=%d\n",
							__FUNCTION__, __LINE__, pLinkId_, (int)pEndpoint->fd);
						if (m_ullInst) {
							LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
						}
						closeSock = pEndpoint->fd;
						if (pEndpoint->fd != -1) {
							shutdown(pEndpoint->fd, SD_BOTH);
							closesocket(pEndpoint->fd);
							pEndpoint->fd = -1;
						}
						delete pEndpoint;
						pEndpoint = NULL;
						iter = m_endpoints.erase(iter);
						result = 0;
						break;
					}
				}
				iter++;
			}
		}
		if (closeSock != -1) {
			std::lock_guard<std::mutex> lock(m_mutex4SockList);
			std::vector<ts::SocketInfo *>::iterator it = m_sockList.begin();
			for (; it != m_sockList.end(); it++) {
				ts::SocketInfo * pSockInfo = *it;
				if (pSockInfo) {
					if (pSockInfo->sock == closeSock) {
						delete pSockInfo;
						pSockInfo = NULL;
						m_sockList.erase(it);
						break;
					}
				}
			}
		}
	}
	return result;
}

bool ts::tcp_server_t::addEndpoint(Endpoint * pEndpoint_)
{ 
	bool result = false;
	if (pEndpoint_ && pEndpoint_->fd != -1) {
		std::lock_guard<std::mutex> lock(m_mutex4Endpoints);
		m_endpoints.emplace_back(pEndpoint_);
		result = true;
	}
	return result;
}

void ts::tcp_server_t::addSock(ts::SocketInfo * pSockInfo_)
{
	if (pSockInfo_) {
		std::lock_guard<std::mutex> lock(m_mutex4SockList);
		m_sockList.emplace_back(pSockInfo_);
	}
}

bool ts::tcp_server_t::addEvent(ts::Event * pEvent_)
{
	bool result = false;
	if (pEvent_) {
		std::lock_guard<std::mutex> lock(m_mutex4EventQue);
		m_eventQue.emplace(pEvent_);
		if (m_eventQue.size() == 1) {
			m_cond4EventQue.notify_all();
		}
		result = true;
	}
	return result;
}

void ts::tcp_server_t::dispatch()
{
	char szLog[256] = { 0 };
	do {
		std::unique_lock<std::mutex> lock(m_mutex4EventQue);
		m_cond4EventQue.wait(lock, [&] { return (!m_eventQue.empty() || m_bStop); });
		if (m_eventQue.empty() && m_bStop) {
			break;
		}
		ts::Event * pEvent = m_eventQue.front();
		m_eventQue.pop();
		lock.unlock();
		if (pEvent) {
			if (pEvent->pEventData && pEvent->nDataLen > 0) {
				switch (pEvent->nEventType) {
					case ts::EVENT_ADD: {
						break;
					}
					case ts::EVENT_DATA: {
						ts::SocketData sockData;
						uint32_t nSockDataLen = sizeof(ts::SocketData);
						memcpy_s(&sockData, nSockDataLen, pEvent->pEventData, nSockDataLen);
						if (sockData.uiDataLen > 0) {
							sockData.pData = new char[sockData.uiDataLen + 1];
							memcpy_s(sockData.pData, sockData.uiDataLen + 1, pEvent->pEventData + nSockDataLen, sockData.uiDataLen);
							sockData.pData[sockData.uiDataLen] = '\0';
						}
						if (sockData.sock != -1) {
							std::lock_guard<std::mutex> lk(m_mutex4Endpoints);
							std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
							while (iter != m_endpoints.end()) {
								ts::Endpoint * pEndpoint = *iter;
								if (pEndpoint->fd == sockData.sock) {
									pEndpoint->ulTime = time(nullptr);
									if (pEndpoint->nTransferData == 0) {
										pEndpoint->nTransferData = 1;
									}

									char szLog[256] = { 0 };
									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]link=%s, sock=%d, recv %u data\n",
										__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd, sockData.uiDataLen);
									LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);

									if (m_msgCb) {
										MessageContent * pMsgContent = new MessageContent();
										strcpy_s(pMsgContent->szEndPoint, sizeof(pMsgContent->szEndPoint), pEndpoint->toString().c_str());
										pMsgContent->uiMsgDataLen = sockData.uiDataLen;
										pMsgContent->pMsgData = new unsigned char[pMsgContent->uiMsgDataLen + 1];
										memcpy_s(pMsgContent->pMsgData, pMsgContent->uiMsgDataLen + 1, sockData.pData, sockData.uiDataLen);
										pMsgContent->pMsgData[pMsgContent->uiMsgDataLen] = '\0';
										pMsgContent->ulMsgTime = time(NULL);

										m_msgCb(MSG_DATA, (void *)pMsgContent, m_pUserData);

										delete[] pMsgContent->pMsgData;
										pMsgContent->pMsgData = NULL;
										delete pMsgContent;
										pMsgContent = NULL;
									}
									break;
								}
								iter++;
							}
						}
						if (sockData.uiDataLen > 0 && sockData.pData) {
							delete[] sockData.pData;
							sockData.pData = NULL;
						}
						break;
					}
					case ts::EVENT_DELETE: {
						if (pEvent->nDataLen && pEvent->pEventData) {
							ts::SocketInfo sockInfo;
							memcpy_s(&sockInfo, sizeof(ts::SocketInfo), pEvent->pEventData, pEvent->nDataLen);

							if (sockInfo.sock != -1) {
								std::lock_guard<std::mutex> lk(m_mutex4Endpoints);
								std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
								while (iter != m_endpoints.end()) {
									ts::Endpoint * pEndpoint = *iter;
									if (pEndpoint) {
										if (pEndpoint->fd == sockInfo.sock) {
											sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]link=%s, sock=%d, disconnect\n",
												__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd);
											LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);

											if (m_msgCb) {
												m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), m_pUserData);
											}

											delete pEndpoint;
											pEndpoint = NULL;
											m_endpoints.erase(iter);
											break;
										}
									}
									iter++;
								}
							}
						}
						break;
					}
					case ts::EVENT_SOCK_CLOSE: {
						if (pEvent->nDataLen && pEvent->pEventData) {
							ts::SocketInfo sockInfo;
							memcpy_s(&sockInfo, sizeof(ts::SocketInfo), pEvent->pEventData, pEvent->nDataLen);
							if (sockInfo.sock > 0) {
								std::lock_guard<std::mutex> lk(m_mutex4SockList);
								std::vector<ts::SocketInfo *>::iterator iter = m_sockList.begin();
								while (iter != m_sockList.end()) {
									ts::SocketInfo * pSockInfo = *iter;
									if (pSockInfo) {
										if (pSockInfo->sock == sockInfo.sock) {
											sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]remove sock=%d from sockList\n",
												__FUNCTION__, __LINE__, (int)pSockInfo->sock);
											LOG_Log(m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
											delete pSockInfo;
											pSockInfo = NULL;
											m_sockList.erase(iter);
											break;
										}
									}
									iter++;
								}
							}
						}
						
						break;
					}
				}
				delete[] pEvent->pEventData;
				pEvent->pEventData = NULL;
				pEvent->nDataLen = 0;
			}
			delete pEvent;
			pEvent = NULL;
		}
	} while (1);
}

void ts::recv2(void * param_)
{
	ts::tcp_server_t * pServer = (ts::tcp_server_t *)param_;
	if (pServer) {
		timeval timeout{ 1, 0 };
		FD_SET fdRead;
		char szLog[256] = { 0 };
		while (!pServer->m_bStop) {
			{
				std::lock_guard<std::mutex> lock(pServer->m_mutex4SockList);
				FD_ZERO(&fdRead);
				for (size_t i = 0; i < pServer->m_sockList.size(); i++) {
					if (pServer->m_sockList[i]->sock != -1) {
						FD_SET(pServer->m_sockList[i]->sock, &fdRead);
					}
				}
				int n = select(0, &fdRead, nullptr, nullptr, &timeout);
				if (n == 0) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
				else if (n == -1) {
					sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]select error=%d\n", __FUNCTION__, __LINE__,
						WSAGetLastError());
					LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					continue;
				}
				else if (n > 0) {

					std::vector<ts::SocketInfo *>::iterator iter = pServer->m_sockList.begin();
					while (iter != pServer->m_sockList.end()) {
						ts::SocketInfo * pSockInfo = *iter;
						if (FD_ISSET(pSockInfo->sock, &fdRead)) {
							if (pSockInfo->nSockType == 0) { //listener
								sockaddr_in clientAddress;
								memset(&clientAddress, 0, sizeof(sockaddr_in));
								int nAddressLen = sizeof(sockaddr_in);
								SOCKET sock = accept(pSockInfo->sock, (sockaddr *)&clientAddress, &nAddressLen);
								if (sock != -1) {
									int nKeepAlive = 1;
									setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&nKeepAlive, (int)sizeof(int));
									int nKeepAliveTime = 180; //3min
									setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (const char *)&nKeepAliveTime, (int)sizeof(int));
									int nKeepAliveIntvl = 10;
									setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (const char *)&nKeepAliveIntvl, (int)sizeof(int));
									int nKeepAliveProbes = 3;
									setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (const char *)&nKeepAliveProbes, (int)sizeof(int));
									ts::SocketInfo * pNewSockInfo = new ts::SocketInfo();
									pNewSockInfo->nSockType = 1;
									pNewSockInfo->sock = sock;
									pServer->m_sockList.emplace_back(pNewSockInfo);

									ts::Endpoint * pEndpoint = new ts::Endpoint();
									pEndpoint->fd = sock;
									pEndpoint->nType = 1;
									pEndpoint->nTransferData = 0;
									pEndpoint->ulTime = time(nullptr);
									inet_ntop(AF_INET, (IN_ADDR *)&clientAddress.sin_addr, pEndpoint->szIp, sizeof(pEndpoint->szIp));
									pEndpoint->nPort = ntohs(clientAddress.sin_port);
									pServer->addEndpoint(pEndpoint);

									if (pServer->m_msgCb) {
										pServer->m_msgCb(MSG_LINK_CONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
									}
									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]sock=%d, link=%s connect\n", __FUNCTION__, __LINE__,
										(int)sock, pEndpoint->toString().c_str());
									LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);

								}
							}
							else { //client
								char szRecvBuf[512 * 1024] = { 0 };
								int nRecvLen = ::recv(pSockInfo->sock, szRecvBuf, 512 * 1024, 0);
								if (nRecvLen == -1) {
									iter = pServer->m_sockList.erase(iter);
									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]sock=%d, recv error=%d\n", __FUNCTION__,
										__LINE__, (int)pSockInfo->sock, WSAGetLastError());
									LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
									shutdown(pSockInfo->sock, SD_BOTH);
									ts::Event * pEvent = new ts::Event();
									pEvent->nEventType = ts::EVENT_DELETE;
									pEvent->nDataLen = (uint32_t)sizeof(ts::SocketInfo);
									pEvent->pEventData = new unsigned char[pEvent->nDataLen + 1];
									memcpy_s(pEvent->pEventData, pEvent->nDataLen + 1, pSockInfo, pEvent->nDataLen);
									pEvent->pEventData[pEvent->nDataLen] = '\0';
									if (!pServer->addEvent(pEvent)) {
										if (pEvent) {
											if (pEvent->nDataLen > 0 && pEvent->pEventData) {
												delete[] pEvent->pEventData;
												pEvent->pEventData = NULL;
												pEvent->nDataLen = 0;
											}
											delete pEvent;
											pEvent = NULL;
										}
									}
									closesocket(pSockInfo->sock);
									pSockInfo->sock = -1;
									delete pSockInfo;
									pSockInfo = NULL;
									continue;
								}
								else if (nRecvLen == 0) {
									iter = pServer->m_sockList.erase(iter);
									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]sock=%d, recv timeout\n", __FUNCTION__,
										__LINE__, (int)pSockInfo->sock);
									LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
									
									shutdown(pSockInfo->sock, SD_BOTH);

									ts::Event * pEvent = new ts::Event();
									pEvent->nEventType = ts::EVENT_DELETE;
									pEvent->nDataLen = (uint32_t)sizeof(ts::SocketInfo);
									pEvent->pEventData = new unsigned char[pEvent->nDataLen + 1];
									memcpy_s(pEvent->pEventData, pEvent->nDataLen + 1, pSockInfo, pEvent->nDataLen);
									pEvent->pEventData[pEvent->nDataLen] = '\0';
									if (!pServer->addEvent(pEvent)) {
										if (pEvent) {
											if (pEvent->nDataLen > 0 && pEvent->pEventData) {
												delete[] pEvent->pEventData;
												pEvent->pEventData = NULL;
												pEvent->nDataLen = 0;
											}
											delete pEvent;
											pEvent = NULL;
										}
									}
									closesocket(pSockInfo->sock);
									pSockInfo->sock = -1;
									delete pSockInfo;
									pSockInfo = NULL;
									
									continue;
								}
								else if (nRecvLen > 0) {
									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]sock=%d, recv %d data\n", __FUNCTION__,
										__LINE__, (int)pSockInfo->sock, nRecvLen);
									LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);

									ts::SocketData * pSockData = new ts::SocketData();
									pSockData->sock = pSockInfo->sock;
									pSockData->uiDataLen = nRecvLen;
									pSockData->pData = new char[nRecvLen + 1];
									memcpy_s(pSockData->pData, nRecvLen + 1, szRecvBuf, nRecvLen);
									pSockData->pData[nRecvLen] = '\0';

									ts::Event * pEvent = new ts::Event();
									pEvent->nEventType = ts::EVENT_DATA;
									uint32_t nSockDataLen = (uint32_t)sizeof(ts::SocketData);
									uint32_t nDataLen = nSockDataLen + nRecvLen;
									pEvent->nDataLen = nDataLen;
									pEvent->pEventData = new unsigned char[nDataLen + 1];
									memcpy_s(pEvent->pEventData, nDataLen + 1, pSockData, nSockDataLen);
									memcpy_s(pEvent->pEventData + nSockDataLen, pSockData->uiDataLen + 1, szRecvBuf, nRecvLen);
									pEvent->pEventData[nDataLen] = '\0';

									if (!pServer->addEvent(pEvent)) {
										if (pEvent) {
											if (pEvent->pEventData && pEvent->nDataLen) {
												delete[] pEvent->pEventData;
												pEvent->pEventData = NULL;
												pEvent->nDataLen = 0;
											}
											delete pEvent;
											pEvent = NULL;
										}
									}

									if (pSockData) {
										if (pSockData->pData) {
											delete[] pSockData->pData;
											pSockData->pData = NULL;
										}
										delete pSockData;
										pSockData = NULL;
									}
								}
							}
						}
						iter++;
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

void ts::supervise(void * param_)
{
	ts::tcp_server_t * pServer = (ts::tcp_server_t *)param_;
	std::vector<SOCKET> sockList;
	while (!pServer->m_bStop) {
		{
			char szLog[256] = { 0 };
			std::lock_guard<std::mutex> lock(pServer->m_mutex4Endpoints);
			if (!pServer->m_endpoints.empty()) {
				unsigned int i = 0;
				std::vector<ts::Endpoint *>::iterator iter = pServer->m_endpoints.begin();
				while (iter != pServer->m_endpoints.end()) {
					ts::Endpoint * pEndpoint = *iter;
					if (pEndpoint) {
						if (pEndpoint->nType == 1) {
							if (pEndpoint->nTransferData == 0) {
								int nSeconds = 0;
								int nLen = sizeof(nSeconds);
								getsockopt(pEndpoint->fd, SOL_SOCKET, SO_CONNECT_TIME, (char *)&nSeconds, &nLen);
								if (nSeconds != -1 && nSeconds > 60) {
									iter = pServer->m_endpoints.erase(iter);

									//ts::SocketInfo sockInfo;
									//sockInfo.sock = pEndpoint->fd;
									//sockInfo.nSockType = 1;
									//ts::Event * pEvent = new ts::Event();
									//pEvent->nEventType = ts::EVENT_SOCK_CLOSE;
									//pEvent->nDataLen = (uint32_t)sizeof(ts::SocketInfo);
									//pEvent->pEventData = new unsigned char[pEvent->nDataLen + 1];
									//memcpy_s(pEvent->pEventData, pEvent->nDataLen + 1, &sockInfo, pEvent->nDataLen);
									//pEvent->pEventData[pEvent->nDataLen] = '\0';
									//if (!pServer->addEvent(pEvent)) {
									//	if (pEvent) {
									//		if (pEvent->pEventData) {
									//			delete[] pEvent->pEventData;
									//			pEvent->pEventData = NULL;
									//		}
									//		delete pEvent;
									//		pEvent = NULL;
									//	}
									//}

									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]link=%s, sock=%d, disconnect for linger\n",
										__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd);
									if (pServer->m_ullInst) {
										LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
									}

									sockList.emplace_back((SOCKET)pEndpoint->fd);

									shutdown(pEndpoint->fd, SD_BOTH);
									closesocket(pEndpoint->fd);
									pEndpoint->fd = -1;
									if (pServer->m_msgCb) {
										pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
									}
									delete pEndpoint;
									pEndpoint = NULL;
									
									continue;
								}
							}
							if (send(pEndpoint->fd, NULL, 0, 0) == SOCKET_ERROR) {
							//	iter = pServer->m_endpoints.erase(iter);
							//	ts::SocketInfo sockInfo;
							//	sockInfo.sock = pEndpoint->fd;
							//	sockInfo.nSockType = 1;
							//	ts::Event * pEvent = new ts::Event();
							//	pEvent->nEventType = ts::EVENT_SOCK_CLOSE;
							//	pEvent->nDataLen = (uint32_t)sizeof(ts::SocketInfo);
							//	pEvent->pEventData = new unsigned char[pEvent->nDataLen + 1];
							//	memcpy_s(pEvent->pEventData, pEvent->nDataLen + 1, &sockInfo, pEvent->nDataLen);
							//	pEvent->pEventData[pEvent->nDataLen] = '\0';
							//	if (!pServer->addEvent(pEvent)) {
							//		if (pEvent) {
							//			if (pEvent->pEventData) {
							//				delete[] pEvent->pEventData;
							//				pEvent->pEventData = NULL;
							//			}
							//			delete pEvent;
							//			pEvent = NULL;
							//		}
							//	}

								sockList.emplace_back((SOCKET)pEndpoint->fd);

								shutdown(pEndpoint->fd, SD_BOTH);
								closesocket(pEndpoint->fd);
								pEndpoint->fd = -1;
								if (pServer->m_msgCb) {
									pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
								}

								sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]link=%s, sock=%d, disconnect for error=%d\n",
									__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd, WSAGetLastError());
								LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);

								delete pEndpoint;
								pEndpoint = NULL;
								continue;
							}
							else {
								unsigned long long ulNow = (unsigned long long)time(NULL);
								if (ulNow > pEndpoint->ulTime && (int)(ulNow - pEndpoint->ulTime) > pServer->m_nTimeout + 10) {
									iter = pServer->m_endpoints.erase(iter);

									//ts::SocketInfo sockInfo;
									//sockInfo.sock = pEndpoint->fd;
									//sockInfo.nSockType = 1;
									//ts::Event * pEvent = new ts::Event();
									//pEvent->nEventType = ts::EVENT_SOCK_CLOSE;
									//pEvent->nDataLen = (uint32_t)sizeof(ts::SocketInfo);
									//pEvent->pEventData = new unsigned char[pEvent->nDataLen + 1];
									//memcpy_s(pEvent->pEventData, pEvent->nDataLen + 1, &sockInfo, pEvent->nDataLen);
									//pEvent->pEventData[pEvent->nDataLen] = '\0';
									//if (!pServer->addEvent(pEvent)) {
									//	if (pEvent) {
									//		if (pEvent->pEventData) {
									//			delete[] pEvent->pEventData;
									//			pEvent->pEventData = NULL;
									//		}
									//		delete pEvent;
									//		pEvent = NULL;
									//	}
									//}

									sprintf_s(szLog, sizeof(szLog), "[tcp_server]%s[%d]link=%s, sock=%d, disconnect for timeout\n",
										__FUNCTION__, __LINE__, pEndpoint->toString().c_str(), (int)pEndpoint->fd);
									LOG_Log(pServer->m_ullInst, szLog, pf_logger::eLOGCATEGORY_INFO, pf_logger::eLOGTYPE_FILE);
									
									sockList.emplace_back((SOCKET)pEndpoint->fd);

									shutdown(pEndpoint->fd, SD_BOTH);
									closesocket(pEndpoint->fd);
									pEndpoint->fd = -1;
									if (pServer->m_msgCb) {
										pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
									}
									delete pEndpoint;
									pEndpoint = NULL;
									
									continue;
								}
							}
						}
					}
					iter++;
				}
			}
		}

		if (!sockList.empty()) {
			std::lock_guard<std::mutex> lock(pServer->m_mutex4SockList);
			for (size_t i = 0; i < sockList.size(); i++) {
				SOCKET sock = sockList[i];
				std::vector<ts::SocketInfo *>::iterator it = pServer->m_sockList.begin();
				for (; it != pServer->m_sockList.end(); it++) {
					ts::SocketInfo * pSockInfo = *it;
					if (pSockInfo) {
						if (pSockInfo->sock == sock) {
							delete pSockInfo;
							pSockInfo = NULL;
							pServer->m_sockList.erase(it);
							break;
						}
					}
				}
			}
			sockList.clear();
		}

		int n = 0;
		while (!pServer->m_bStop) {
			if (n++ > 2) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	}
}

void ts::eventDispatch(void * param_)
{
	ts::tcp_server_t * pServer = (ts::tcp_server_t *)param_;
	if (pServer) {
		pServer->dispatch();
	}
}