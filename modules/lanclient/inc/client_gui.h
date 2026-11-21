#pragma once
#include <string>

#if defined(_WIN32) || defined(__APPLE__)
// 0: 当前进程已经有权限。1: 已重新拉起，调用方应退出 0。-1: 提权失败。
int ClientEnsureElevated();
#endif

class ClientGui
{
  public:
	int Run(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind);
};
