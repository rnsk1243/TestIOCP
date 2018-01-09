/*
## 소켓 서버 : 1 v n - IOCP
1. socket()            : 소켓생성
2. bind()            : 소켓설정
3. listen()            : 수신대기열생성
4. accept()            : 연결대기
5. read()&write()
    WIN recv()&send    : 데이터 읽고쓰기
6. close()
    WIN closesocket    : 소켓종료
*/

#include<stdio.h>
#include<stdlib.h>
#include<process.h>
#include<WinSock2.h>
#include<Windows.h>
#include<iostream>
#include<thread>
//using namespace std;
#include<tchar.h>
//#include "stdafx.h"
//#include <winsock2.h>
 
#pragma comment(lib, "Ws2_32.lib")
 
#define MAX_BUFFER        1024
#define SERVER_PORT        3500
 
struct SOCKETINFO
{
    WSAOVERLAPPED overlapped;
    WSABUF dataBuffer;
    SOCKET socket;
    char messageBuffer[MAX_BUFFER];
    int receiveBytes;
    int sendBytes;
};
 
DWORD WINAPI makeThread(LPVOID hIOCP);
 
int _tmain(int argc, _TCHAR* argv[])
{
    // Winsock Start - windock.dll 로드
    WSADATA WSAData;
    if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0)
    {
        printf("Error - Can not load 'winsock.dll' file\n");
        return 1;
    }
 
    // 1. 소켓생성  
    SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	//cout << "Error Code1 = " << WSAGetLastError() << endl;
    if (listenSocket == INVALID_SOCKET)
    {
        printf("Error - Invalid socket\n");
		//cout << listenSocket << endl;
        return 1;
    }
 
    // 서버정보 객체설정
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
    serverAddr.sin_family = PF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
 
    // 2. 소켓설정
    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
    {
        printf("Error - Fail bind\n");
        // 6. 소켓종료
        closesocket(listenSocket);
        // Winsock End
        WSACleanup();
        return 1;
    }
	//cout << "Error Code2 = " << WSAGetLastError() << endl;
	//bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN));
	//cout << "Error Code2.5 = " << WSAGetLastError() << endl;
    // 3. 수신대기열생성
	int listenResult = listen(listenSocket, 5);
	std::cout << "Error Code3 = " << WSAGetLastError() << std::endl;
    if (listenResult == SOCKET_ERROR)
    {
        printf("Error - Fail listen\n");
		//cout << "listen(listenSocket, 5) = " << listenResult << endl;
		//cout << "Error Code4 = " << WSAGetLastError() << endl;
        // 6. 소켓종료
        closesocket(listenSocket);
        // Winsock End
        WSACleanup();
        return 1;
    }
 
    // 완료결과를 처리하는 객체(CP : Completion Port) 생성
    HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);    
 
    // 워커스레드 생성
    // - CPU * 2개
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    int threadCount = systemInfo.dwNumberOfProcessors * 2;
    unsigned long threadId;
    // - thread Handler 선언
    HANDLE *hThread = (HANDLE *)malloc(threadCount * sizeof(HANDLE));
    // - thread 생성
    for (int i = 0; i < threadCount; i++)
    {
        hThread[i] = CreateThread(NULL, 0, makeThread, &hIOCP, 0, &threadId);
    }
 
    SOCKADDR_IN clientAddr;
    int addrLen = sizeof(SOCKADDR_IN);
    memset(&clientAddr, 0, addrLen);
    SOCKET clientSocket;
    SOCKETINFO *socketInfo;
    DWORD receiveBytes;
    DWORD flags;
     
    while (1)
    {
        clientSocket = accept(listenSocket, (struct sockaddr *)&clientAddr, &addrLen);
        if (clientSocket == INVALID_SOCKET)
        {
            printf("Error - Accept Failure\n");
            return 1;
        }
 
        socketInfo = (struct SOCKETINFO *)malloc(sizeof(struct SOCKETINFO));
        memset((void *)socketInfo, 0x00, sizeof(struct SOCKETINFO));
        socketInfo->socket = clientSocket;
        socketInfo->receiveBytes = 0;
        socketInfo->sendBytes = 0;
        socketInfo->dataBuffer.len = MAX_BUFFER;
        socketInfo->dataBuffer.buf = socketInfo->messageBuffer;
        flags = 0;
 
        hIOCP = CreateIoCompletionPort((HANDLE)clientSocket, hIOCP, (DWORD)socketInfo, 0);
 
        // 중첩 소캣을 지정하고 완료시 실행될 함수를 넘겨준다.
        if (WSARecv(socketInfo->socket, &socketInfo->dataBuffer, 1, &receiveBytes, &flags, &(socketInfo->overlapped), NULL))
        {
            if (WSAGetLastError() != WSA_IO_PENDING)
            {
                printf("Error - IO pending Failure\n");
                return 1;
            }
        }
    }
 
    // 6-2. 리슨 소켓종료
    closesocket(listenSocket);
 
    // Winsock End
    WSACleanup();
     
    return 0;
}
 
DWORD WINAPI makeThread(LPVOID hIOCP)
{
    HANDLE threadHandler = *((HANDLE *)hIOCP);
    DWORD receiveBytes;
    DWORD sendBytes;
    DWORD completionKey;
    DWORD flags;
    struct SOCKETINFO *eventSocket;
    while (1)
    {
        // 입출력 완료 대기
        if (GetQueuedCompletionStatus(threadHandler, &receiveBytes, (PULONG_PTR)&completionKey, (LPOVERLAPPED *)&eventSocket, INFINITE) == 0)
        {
            printf("Error - GetQueuedCompletionStatus Failure\n");
            closesocket(eventSocket->socket);
            free(eventSocket);
            return 1;
        }
 
        eventSocket->dataBuffer.len = receiveBytes;
         
        if (receiveBytes == 0)
        {
            closesocket(eventSocket->socket);
            free(eventSocket);
            continue;
        }
        else
        {
            printf("TRACE - Receive message : %s (%d bytes)\n", eventSocket->dataBuffer.buf, eventSocket->dataBuffer.len);
             
            if (WSASend(eventSocket->socket, &(eventSocket->dataBuffer), 1, &sendBytes, 0, NULL, NULL) == SOCKET_ERROR)
            {
                if (WSAGetLastError() != WSA_IO_PENDING)
                {
                    printf("Error - Fail WSASend(error_code : %d)\n", WSAGetLastError());
                }
            }
 
            printf("TRACE - Send message : %s (%d bytes)\n", eventSocket->dataBuffer.buf, eventSocket->dataBuffer.len);
                         
            memset(eventSocket->messageBuffer, 0x00, MAX_BUFFER);
            eventSocket->receiveBytes = 0;
            eventSocket->sendBytes = 0;
            eventSocket->dataBuffer.len = MAX_BUFFER;
            eventSocket->dataBuffer.buf = eventSocket->messageBuffer;
            flags = 0;
 
            if (WSARecv(eventSocket->socket, &(eventSocket->dataBuffer), 1, &receiveBytes, &flags, &eventSocket->overlapped, NULL) == SOCKET_ERROR)
            {
                if (WSAGetLastError() != WSA_IO_PENDING)
                {
                    printf("Error - Fail WSARecv(error_code : %d)\n", WSAGetLastError());
                }
            }                
        }        
    }
}


/////////////////////////////////////////////////////////////////////////////
//
//#include<stdio.h>
//#include<stdlib.h>
//#include<process.h>
//#include<WinSock2.h>
//#include<Windows.h>
//#include<iostream>
//#include<thread>
//
//#define BUF_SIZE 100
//#define READ 3
//#define WRITE 5
//
//struct PER_HANDLE_DATA
//{
//	SOCKET hClntSock;
//	SOCKADDR_IN clntAdr;
//};
//
//struct PER_IO_DATA
//{
//	OVERLAPPED overlapped;
//	WSABUF wsaBuf;
//	char buffer[BUF_SIZE];
//	int rwMode;
//};
//
//DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);
//
//int main(int argc, char* argv[])
//{
//	WSADATA wsaData;
//	HANDLE hComPort;
//	SYSTEM_INFO sysInfo;
//	PER_IO_DATA* ioInfo;
//	PER_HANDLE_DATA* handleInfo;
//
//	SOCKET hServSock;
//	SOCKADDR_IN servAdr;
//	DWORD recvBytes, i, flags = 0;
//	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
//	{
//		printf_s("WSAStartup() error!");
//	}
//	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
//	GetSystemInfo(&sysInfo);
//	for (i = 0; i < 3; i++)
//	{
//		_beginthreadex(NULL, 0, EchoThreadMain, (LPVOID)hComPort, 0, NULL); // 스레드 만듬.
//		std::thread workerThread(EchoThreadMain, &hComPort);
//		workerThread.detach();
//		CloseHandle(workerThread.native_handle());
//	}
//
//	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED); // 오버랩트 옵션줘서 서버소켓 만듬.
//	if (hServSock == INVALID_SOCKET)
//	{
//		printf_s("socket error");
//		return 0;
//	}
//	memset(&servAdr, 0, sizeof(servAdr));
//	servAdr.sin_family = AF_INET;
//	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
//	servAdr.sin_port = htons(9000);
//
//	if (bind(hServSock, (sockaddr*)&servAdr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
//	{
//		return 0;
//	}
//	if (listen(hServSock, 5) == SOCKET_ERROR)
//	{
//		printf_s("listen Error");
//		return 0;
//	}
//	else
//	{
//		printf_s("listen...");
//	}
//
//	while (true)
//	{
//		printf_s("1\n");
//		SOCKET hClntSock;
//		printf_s("2\n");
//		SOCKADDR_IN clntAdr;
//		printf_s("3\n");
//		int addrLen = sizeof(clntAdr);
//		printf_s("4\n");
//		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);
//		printf_s("5\n");
//		handleInfo = new PER_HANDLE_DATA(); //(PER_HANDLE_DATA*)malloc(sizeof(PER_HANDLE_DATA));
//		printf_s("6\n");
//		handleInfo->hClntSock = hClntSock;
//		printf_s("7\n");
//		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
//		printf_s("8\n");
//		 완료포트와 클라이언트 소켓 연결, 이제 소켓이 IO완료시 GetQueued함수 반환됨. 이때 handleInfo 얻을 수 있음
//		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0); // hComPort와 클라이언트 소켓 연결 
//		printf_s("9\n");
//		ioInfo = new PER_IO_DATA(); //(PER_IO_DATA*)malloc(sizeof(PER_IO_DATA));
//		printf_s("10\n");
//		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
//		printf_s("11\n");
//		ioInfo->wsaBuf.len = BUF_SIZE;
//		printf_s("12\n");
//		ioInfo->wsaBuf.buf = ioInfo->buffer;
//		printf_s("13\n");
//		ioInfo->rwMode = READ;
//		printf_s("14\n");
//		WSARecv(handleInfo->hClntSock, &(ioInfo->wsaBuf), 1, &recvBytes, &flags, &(ioInfo->overlapped), NULL); // GetQueued함수가 반환할때 OVERLAPPED구조체 얻을 수 있다.
//		printf_s("15\n");
//		Sleep(100);
//	}
//	return 0;
//}
//
//DWORD WINAPI EchoThreadMain(LPVOID pComPort)
//{
//	HANDLE hComPort = (HANDLE)pComPort;
//	SOCKET sock;
//	DWORD bytesTrans;
//	PER_HANDLE_DATA* handleInfo = nullptr;
//	PER_IO_DATA* ioInfo = nullptr;
//	DWORD flags = 0;
//
//	while (true)
//	{
//		bool isSucces = GetQueuedCompletionStatus(hComPort, &bytesTrans, (PULONG_PTR)handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE);
//		if (!isSucces)
//		{
//			Sleep(1000);
//			return 0;
//			continue;
//		}
//		if (handleInfo == nullptr)
//		{
//			return 0;
//		}
//		sock = handleInfo->hClntSock;
//
//		if (ioInfo->rwMode == READ)
//		{
//			printf_s("message received!");
//			if (bytesTrans == 0)
//			{
//				closesocket(sock);
//				free(handleInfo); free(ioInfo);
//				continue;
//			}
//
//			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
//			ioInfo->wsaBuf.len = bytesTrans;
//			ioInfo->rwMode = WRITE;
//			WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
//
//			ioInfo = (PER_IO_DATA*)malloc(sizeof(PER_IO_DATA));
//			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
//			ioInfo->wsaBuf.len = BUF_SIZE;
//			ioInfo->wsaBuf.buf = ioInfo->buffer;
//			ioInfo->rwMode = READ;
//			WSARecv(sock, &(ioInfo->wsaBuf), 1, NULL, &flags, &(ioInfo->overlapped), NULL);
//		}
//		else
//		{
//			printf_s("message sent!");
//			free(ioInfo);
//		}
//
//	}
//	return 0;
//}