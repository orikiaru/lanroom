#pragma once
#include <string>

class ClientGui
{
  public:
	int Run(const char* pConfig, const std::string& sServer, int iPort, const std::string& sBind);
};
