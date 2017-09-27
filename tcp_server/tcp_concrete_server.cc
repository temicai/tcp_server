#include <stdio.h>
#include "tcp_concrete_server.h"

ts::tcp_server_t::tcp_server_t()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	m_bStop = true;
	m_nTimeout = 30;
	m_msgCb = NULL;
	m_pUserData = NULL;
	m_usPort = 0;
}

ts::tcp_server_t::~tcp_server_t()
{
	if (!m_bStop) {
		Stop();
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
	Endpoint * pEndpoint = new Endpoint();
	pEndpoint->fd = fd;
	pEndpoint->nPort = usPort_;
	pEndpoint->ulTime = (unsigned long)time(NULL);
	pEndpoint->nType = 0;
	pEndpoint->nTransferData = 0;
	sprintf_s(pEndpoint->szIp, sizeof(pEndpoint->szIp), "*");
	if (!addEndpoint(pEndpoint)) {
		delete pEndpoint;
		pEndpoint = NULL;
		closesocket(fd);
		return -1;
	}
	m_msgCb = fMsgCb_;
	m_pUserData = pUserData_;
	m_thdReceiver = std::thread(ts::recv, this);
	m_thdSupervisor = std::thread(ts::supervise, this);
	return 0;
}

void ts::tcp_server_t::Stop()
{
	m_bStop = true;
	m_thdReceiver.join();
	m_thdSupervisor.join();
	if (!m_endpoints.empty()) {
		std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
		while (iter != m_endpoints.end()) {
			ts::Endpoint * pEndpoint = *iter;
			if (pEndpoint) {
				if (pEndpoint->fd != -1) {
					closesocket(pEndpoint->fd);
				}
				delete pEndpoint;
				pEndpoint = NULL;
			}
			iter = m_endpoints.erase(iter);
		}
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
						if (pEndpoint->fd != -1) {
							closesocket(pEndpoint->fd);
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
		std::lock_guard<std::mutex> lock(m_mutex4Endpoints);
		std::vector<ts::Endpoint *>::iterator iter = m_endpoints.begin();
		while (iter != m_endpoints.end()) {
			ts::Endpoint * pEndpoint = *iter;
			if (pEndpoint) {
				if (strcmp(pLinkId_, pEndpoint->toString().c_str()) == 0) {
					if (m_msgCb) {
						m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), m_pUserData);
					}
					if (pEndpoint->fd != -1) {
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

void ts::recv(void * param_)
{
	ts::tcp_server_t * pServer = (ts::tcp_server_t *)param_;
	if (pServer) {
		timeval timeout{ 0, 0 };
		FD_SET fdRead;
		while (!pServer->m_bStop) {
			{ 
				std::lock_guard<std::mutex> lock(pServer->m_mutex4Endpoints);
				if (!pServer->m_endpoints.empty()) {
					FD_ZERO(&fdRead);
					size_t nSize = pServer->m_endpoints.size();
					for (size_t i = 0; i < nSize; i++) {
						FD_SET(pServer->m_endpoints[i]->fd, &fdRead);
					}
					int n = select(0, &fdRead, NULL, NULL, &timeout);
					if (n == 0) {
						continue;
					}
					else if (n == -1) {
						break;
					}
					else if (n > 0) {
						for (size_t i = 0; i < pServer->m_endpoints.size(); i++) {
							if (FD_ISSET(pServer->m_endpoints[i]->fd, &fdRead)) {
								if (pServer->m_endpoints[i]->nType == 0) {
									sockaddr_in clientAddress;
									memset(&clientAddress, 0, sizeof(sockaddr_in));
									int nAddressLen = sizeof(sockaddr_in);
									fd_t accFd = accept(pServer->m_endpoints[i]->fd, (sockaddr *)&clientAddress, &nAddressLen);
									if (accFd != -1) {
										int nNodelay = 1;
										setsockopt(accFd, IPPROTO_TCP, TCP_NODELAY, (const char *)&nNodelay, sizeof(int));
										int nSendBuf = 512 * 1024;
										setsockopt(accFd, SOL_SOCKET, SO_SNDBUF, (const char *)&nSendBuf, sizeof(int));
										int nRecvBuf = 512 * 1024;
										setsockopt(accFd, SOL_SOCKET, SO_RCVBUF, (const char *)&nRecvBuf, sizeof(int));

										//setsockopt(accFd, IPPROTO_TCP, TCP_MAXRT, )
										Endpoint * pNewEndpoint = new Endpoint();
										pNewEndpoint->fd = accFd;
										pNewEndpoint->nType = 1;
										pNewEndpoint->nTransferData = 0;
										pNewEndpoint->ulTime = (unsigned long)time(NULL);
										inet_ntop(AF_INET, (IN_ADDR *)&clientAddress.sin_addr, pNewEndpoint->szIp, sizeof(pNewEndpoint->szIp));
										pNewEndpoint->nPort = ntohs(clientAddress.sin_port);
										pServer->m_endpoints.emplace_back(pNewEndpoint);
										//printf("new connection: %s\n", pNewEndpoint->toString().c_str());
										if (pServer->m_msgCb) {
											pServer->m_msgCb(MSG_LINK_CONNECT, (void *)pNewEndpoint->toString().c_str(), pServer->m_pUserData);
										}
									}
								}
								else {
									char szRecvBuf[512 * 1024] = { 0 };
									int nRecvLen = ::recv(pServer->m_endpoints[i]->fd, szRecvBuf, 512 * 1024, 0);
									if (nRecvLen == -1) {
										if (pServer->m_msgCb) {
											pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pServer->m_endpoints[i]->toString().c_str(),
												pServer->m_pUserData);
										}
										closesocket(pServer->m_endpoints[i]->fd);
										printf("connection %s disconnect check by select recv=-1\n", pServer->m_endpoints[i]->toString().c_str());
										pServer->m_endpoints.erase(pServer->m_endpoints.begin() + i);
									}
									else if (nRecvLen == 0) {
										if (pServer->m_msgCb) {
											pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pServer->m_endpoints[i]->toString().c_str(),
												pServer->m_pUserData);
										}
										closesocket(pServer->m_endpoints[i]->fd);
										printf("connection %s disconnect check by select recv=0\n", pServer->m_endpoints[i]->toString().c_str());
										pServer->m_endpoints.erase(pServer->m_endpoints.begin() + i);
										continue;
									}
									else if (nRecvLen > 0) {
										//printf("recv from %s, %d data %s\n", pServer->m_endpoints[i]->toString().c_str(), nRecvLen, szRecvBuf);
										pServer->m_endpoints[i]->ulTime = (unsigned long)time(NULL);
										pServer->m_endpoints[i]->nTransferData = 1;
										if (pServer->m_msgCb) {
											MessageContent msgContent;
											strcpy_s(msgContent.szEndPoint, sizeof(msgContent.szEndPoint), 
												pServer->m_endpoints[i]->toString().c_str());
											msgContent.ulMsgTime = pServer->m_endpoints[i]->ulTime;
											msgContent.ulMsgDataLen = nRecvLen;
											msgContent.pMsgData = new unsigned char[msgContent.ulMsgDataLen + 1];
											memcpy_s(msgContent.pMsgData, msgContent.ulMsgDataLen + 1, szRecvBuf, msgContent.ulMsgDataLen);
											msgContent.pMsgData[msgContent.ulMsgDataLen] = '\0';
											pServer->m_msgCb(MSG_DATA, (void *)&msgContent, pServer->m_pUserData);
											delete[] msgContent.pMsgData;
											msgContent.pMsgData = NULL;
										}
									}
								}
							}
						}
					}
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
}

void ts::supervise(void * param_)
{
	ts::tcp_server_t * pServer = (ts::tcp_server_t *)param_;
	while (!pServer->m_bStop) {
		{
			std::lock_guard<std::mutex> lock(pServer->m_mutex4Endpoints);
			if (!pServer->m_endpoints.empty()) {
				std::vector<ts::Endpoint *>::iterator iter = pServer->m_endpoints.begin();
				while (iter != pServer->m_endpoints.end()) {
					ts::Endpoint * pEndpoint = *iter;
					if (pEndpoint) {
						if (pEndpoint->nType == 1) {
							if (pEndpoint->nTransferData == 0) {
								int nSeconds = 0;
								int nLen = sizeof(nSeconds);
								getsockopt(pEndpoint->fd, SOL_SOCKET, SO_CONNECT_TIME, (char *)&nSeconds, &nLen);
								if (nSeconds != -1 && nSeconds > 30) {
									printf("link=%s, sock=%d, connect time=%d, linger no data\n", pEndpoint->toString().c_str(),
										(int)pEndpoint->fd, nSeconds);
									closesocket(pEndpoint->fd);
									if (pServer->m_msgCb) {
										pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
									}
									delete pEndpoint;
									iter = pServer->m_endpoints.erase(iter);
									continue;
								}
							}
							if (send(pEndpoint->fd, NULL, 0, 0) == SOCKET_ERROR) {
								printf("connection %s disconnect for error=%d\n", pEndpoint->toString().c_str(), WSAGetLastError());
								closesocket(pEndpoint->fd);
								if (pServer->m_msgCb) {
									pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
								}
								delete pEndpoint;
								iter = pServer->m_endpoints.erase(iter);
								continue;
							}
							else {
								unsigned long ulNow = (unsigned long)time(NULL);
								if (ulNow > pEndpoint->ulTime && (int)(ulNow - pEndpoint->ulTime) > pServer->m_nTimeout + 10) {
									printf("connection %s disconnect for timeout\n", pEndpoint->toString().c_str());
									closesocket(pEndpoint->fd);
									if (pServer->m_msgCb) {
										pServer->m_msgCb(MSG_LINK_DISCONNECT, (void *)pEndpoint->toString().c_str(), pServer->m_pUserData);
									}
									delete pEndpoint;
									iter = pServer->m_endpoints.erase(iter);
									continue;
								}
							}
						}
					}
					iter++;
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

//int main()
//{
//	ts::tcp_server_t * pServer = new ts::tcp_server_t();
//	if (pServer->Start(20000, NULL, NULL) != -1) {
//		printf("tcp server now is working on port:20000\n");
//		getchar();
//		pServer->Stop();
//	}
//	delete pServer;
//	pServer = NULL;
//	return 0;
//}



