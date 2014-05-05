#include <Winsock2.h>
#include <Ws2spi.h>
#include <Windows.h>
#include <tchar.h>
#include "Debug.h"
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")
WSPUPCALLTABLE g_pUpCallTable;		// �ϲ㺯���б����LSP�������Լ���α�����ʹ����������б�
WSPPROC_TABLE g_NextProcTable;		// �²㺯���б�
TCHAR g_szCurrentApp[MAX_PATH];		// ��ǰ���ñ�DLL�ĳ��������
HANDLE hFile;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			::GetModuleFileName(NULL, g_szCurrentApp, MAX_PATH);
			hFile = CreateFile(
				L"test.txt", 
				FILE_ALL_ACCESS, 
				FILE_SHARE_READ, 
				NULL,
				OPEN_ALWAYS,
				0,
				NULL
			);
			break;
		}
	case DLL_PROCESS_DETACH:
		{
			CloseHandle(hFile);
			break;
		}
	default:
		break;
	}
	return TRUE;
}

LPWSAPROTOCOL_INFOW GetProvider(LPINT lpnTotalProtocols)
{
	DWORD dwSize = 0;
	int dwError;
	LPWSAPROTOCOL_INFOW pProtoInfo = NULL;
	
	// ȡ����Ҫ�ĳ���
	if(::WSCEnumProtocols(NULL, pProtoInfo, &dwSize, &dwError) == SOCKET_ERROR)
	{
		if(dwError != WSAENOBUFS) return NULL;
	}
	pProtoInfo = (LPWSAPROTOCOL_INFOW)::GlobalAlloc(GPTR, dwSize);
	*lpnTotalProtocols = ::WSCEnumProtocols(NULL, pProtoInfo, &dwSize, &dwError);
	return pProtoInfo;
}

void FreeProvider(LPWSAPROTOCOL_INFOW pProtoInfo)
{
	::GlobalFree(pProtoInfo);
}

int WSPAPI WSPSendTo(
	SOCKET    s,
	LPWSABUF   lpBuffers,
	DWORD    dwBufferCount,
	LPDWORD    lpNumberOfBytesSent,
	DWORD    dwFlags,
	const struct sockaddr FAR * lpTo,
	int     iTolen,
	LPWSAOVERLAPPED lpOverlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	LPWSATHREADID lpThreadId,
	LPINT    lpErrno
)
{
	char temp[1024];
	sprintf(temp, " Query send to... %s", g_szCurrentApp);
	WriteFile(hFile, (LPCVOID)temp, strlen(temp), NULL, NULL);
	SOCKADDR_IN sa = *(SOCKADDR_IN*)lpTo;
	if(sa.sin_port == htons(80))
	{
		int iError;
		g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &iError);
		*lpErrno = WSAECONNABORTED;
		WriteFile(hFile, "deny a sendto ", sizeof("deny a sendto "), NULL, NULL);
		return SOCKET_ERROR;
	}
	
	return g_NextProcTable.lpWSPSendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, 
		lpTo, iTolen, lpOverlapped, lpCompletionRoutine, lpThreadId, lpErrno);
}

int WSPAPI WSPBind(SOCKET s, const struct sockaddr* name, int namelen, LPINT lpErrno)
{
	return g_NextProcTable.lpWSPBind(s, name, namelen, lpErrno);
}

int WSPAPI WSPStartup(
	WORD wVersionRequested,
	LPWSPDATA lpWSPData,
	LPWSAPROTOCOL_INFO lpProtocolInfo,
	WSPUPCALLTABLE UpcallTable,
	LPWSPPROC_TABLE lpProcTable
)
{
	char temp[1024];
	sprintf(temp, " WSPStartup... %s \n", g_szCurrentApp);
	WriteFile(hFile, (LPCVOID)temp, strlen(temp), NULL, NULL);
	if(lpProtocolInfo->ProtocolChain.ChainLen <= 1)
	{
		return WSAEPROVIDERFAILEDINIT;
	}
	
	// �������ϵ��õĺ�����ָ�루������û��ʹ������
	g_pUpCallTable = UpcallTable;
	// ö��Э�飬�ҵ��²�Э���WSAPROTOCOL_INFOW�ṹ 
	WSAPROTOCOL_INFOW NextProtocolInfo;
	int nTotalProtos, i;
	LPWSAPROTOCOL_INFOW pProtoInfo = GetProvider(&nTotalProtos);
	// �²����ID 
	DWORD dwBaseEntryId = lpProtocolInfo->ProtocolChain.ChainEntries[1];
	for (i=0; i<nTotalProtos; i++)
	{
		if(pProtoInfo[i].dwCatalogEntryId == dwBaseEntryId)
		{
			memcpy(&NextProtocolInfo, &pProtoInfo[i], sizeof(NextProtocolInfo));
			break;
		}
	}
	if (i >= nTotalProtos)
	{
		WriteFile(hFile, (LPCVOID)"WSPStartup: Can not find underlying protocol \n", 
			strlen("WSPStartup: Can not find underlying protocol \n"), NULL, NULL);
		return WSAEPROVIDERFAILEDINIT;
	}
	
	// �����²�Э���DLL
	int dwError;
	TCHAR szBaseProviderDll[MAX_PATH];
	int nLen = MAX_PATH;
	
	// ȡ���²��ṩ����DLL·��
	if (::WSCGetProviderPath(&NextProtocolInfo.ProviderId, szBaseProviderDll, &nLen, &dwError) == SOCKET_ERROR)
	{
		sprintf(temp, " WSPStartup: WSCGetProviderPath() failed %d \n", dwError);
		WriteFile(hFile, (LPCVOID)temp,strlen(temp), NULL, NULL);
		return WSAEPROVIDERFAILEDINIT;
	}
	if(!::ExpandEnvironmentStrings(szBaseProviderDll, szBaseProviderDll, MAX_PATH))
	{
		sprintf(temp, " WSPStartup: ExpandEnvironmentStrings() failed %d \n", ::GetLastError());
		WriteFile(hFile, (LPCVOID)temp,strlen(temp), NULL, NULL);
		return WSAEPROVIDERFAILEDINIT;
	}
	
	// �����²��ṩ����
	HMODULE hModule = ::LoadLibrary(szBaseProviderDll);
	if(hModule == NULL)
	{
		sprintf(temp, " WSPStartup: LoadLibrary() failed %d \n", ::GetLastError());
		WriteFile(hFile, (LPCVOID)temp,strlen(temp), NULL, NULL);
		return WSAEPROVIDERFAILEDINIT;
	}
	// �����²��ṩ�����WSPStartup����
	LPWSPSTARTUP pfnWSPStartup = NULL;
	pfnWSPStartup = (LPWSPSTARTUP)::GetProcAddress(hModule, "WSPStartup");
	if(pfnWSPStartup == NULL)
	{
		sprintf(temp, " WSPStartup: GetProcAddress() failed %d \n", ::GetLastError());
		WriteFile(hFile, (LPCVOID)temp,strlen(temp), NULL, NULL);
		return WSAEPROVIDERFAILEDINIT;
	}
	
	// �����²��ṩ�����WSPStartup����
	LPWSAPROTOCOL_INFOW pInfo = lpProtocolInfo;
	if (NextProtocolInfo.ProtocolChain.ChainLen == BASE_PROTOCOL)
		pInfo = &NextProtocolInfo;
	int nRet = pfnWSPStartup(wVersionRequested, lpWSPData, pInfo, UpcallTable, lpProcTable);
	if (nRet != ERROR_SUCCESS)
	{
		sprintf(temp, " WSPStartup: underlying provider's WSPStartup() failed %d \n", nRet);
		WriteFile(hFile, (LPCVOID)temp,strlen(temp), NULL, NULL);
		return nRet;
	}

	// �����²��ṩ�ߵĺ�����
	g_NextProcTable = *lpProcTable;
	// �޸Ĵ��ݸ��ϲ�ĺ�����Hook����Ȥ�ĺ���
	// ������� Hook 30������������ͷд���б��� 30 ��ISP����
	lpProcTable->lpWSPSendTo = WSPSendTo;
	lpProcTable->lpWSPBind = WSPBind;
	FreeProvider(pProtoInfo);
	return nRet;
}
