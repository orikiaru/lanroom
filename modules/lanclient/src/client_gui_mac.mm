#include "client_gui.h"
#include "client_gui_session.h"
#include <crt_externs.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace
{
std::string ShellQuote(const std::string& sText)
{
	std::string sOut = "'";
	for (char iChar : sText)
	{
		if (iChar == '\'')
		{
			sOut += "'\\''";
		}
		else
		{
			sOut.push_back(iChar);
		}
	}
	sOut.push_back('\'');
	return sOut;
}

std::string AppleQuote(const std::string& sText)
{
	std::string sOut;
	sOut.push_back('"');
	for (unsigned char iChar : sText)
	{
		if (iChar == '\\' || iChar == '"')
		{
			sOut.push_back('\\');
		}
		sOut.push_back((char)iChar);
	}
	sOut.push_back('"');
	return sOut;
}

bool RelaunchElevated()
{
	char vDirectory[PATH_MAX];
	if (getcwd(vDirectory, sizeof(vDirectory)) == NULL)
	{
		return false;
	}
	int iArgc = *_NSGetArgc();
	char** vArgv = *_NSGetArgv();
	if (iArgc < 1 || vArgv == NULL || vArgv[0] == NULL)
	{
		return false;
	}
	std::string sCommand = "cd " + ShellQuote(vDirectory) + " && exec";
	for (int iIndex = 0; iIndex < iArgc; ++iIndex)
	{
		sCommand += " ";
		sCommand += ShellQuote(vArgv[iIndex]);
	}
	std::string sScript = "do shell script " + AppleQuote(sCommand) + " with administrator privileges";
	const char* vArgs[] = {"osascript", "-e", sScript.c_str(), NULL};
	pid_t iPid = 0;
	if (posix_spawn(&iPid, "/usr/bin/osascript", NULL, NULL, (char* const*)vArgs, environ) != 0)
	{
		return false;
	}
	int iStatus = 0;
	if (waitpid(iPid, &iStatus, 0) < 0)
	{
		return false;
	}
	return WIFEXITED(iStatus) && WEXITSTATUS(iStatus) == 0;
}
}

int ClientGui::Run(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind)
{
	if (geteuid() != 0)
	{
		if (!RelaunchElevated())
		{
			fprintf(stderr, "创建虚拟网卡需要管理员权限\n");
			return 1;
		}
		return 0;
	}
	return RunClientWindow(pConfig, sServer, iPort, sBind);
}
