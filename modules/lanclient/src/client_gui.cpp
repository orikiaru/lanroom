#include "client_gui.h"
#include "client_gui_session.h"
#include <string>
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace
{
bool IsElevated()
{
	HANDLE hToken = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		return false;
	}
	TOKEN_ELEVATION stElevation = {};
	DWORD iSize = 0;
	BOOL bOk = GetTokenInformation(hToken, TokenElevation, &stElevation, sizeof(stElevation), &iSize);
	CloseHandle(hToken);
	return bOk && stElevation.TokenIsElevated;
}

bool RelaunchElevated()
{
	wchar_t vPath[MAX_PATH];
	if (GetModuleFileNameW(NULL, vPath, MAX_PATH) == 0)
	{
		return false;
	}
	int iArgc = 0;
	wchar_t** vArgv = CommandLineToArgvW(GetCommandLineW(), &iArgc);
	std::wstring sArgs;
	for (int iIndex = 1; iIndex < iArgc; ++iIndex)
	{
		if (!sArgs.empty())
		{
			sArgs.push_back(L' ');
		}
		sArgs.push_back(L'"');
		sArgs += vArgv[iIndex];
		sArgs.push_back(L'"');
	}
	if (vArgv != NULL)
	{
		LocalFree(vArgv);
	}
	wchar_t vDirectory[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, vDirectory);
	SHELLEXECUTEINFOW stInfo = {};
	stInfo.cbSize = sizeof(stInfo);
	stInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	stInfo.lpVerb = L"runas";
	stInfo.lpFile = vPath;
	stInfo.lpDirectory = vDirectory;
	stInfo.lpParameters = sArgs.empty() ? NULL : sArgs.c_str();
	stInfo.nShow = SW_SHOWNORMAL;
	return ShellExecuteExW(&stInfo) == TRUE;
}
}

int ClientGui::Run(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind)
{
	if (!IsElevated())
	{
		if (RelaunchElevated())
		{
			return 0;
		}
	}
	return RunClientWindow(pConfig, sServer, iPort, sBind);
}
