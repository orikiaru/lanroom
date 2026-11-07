#pragma once
#include "oi_net_session.h"
#include <string>

class NicSession : public ::OI::Net::NetSession
{
	size_t m_iNeed = 0;

	void Consume();
	void Command(const std::string& sLine);

  public:
	explicit NicSession(::OI::Net::SessionManager* pManager);
	virtual void OnDataIn();
	virtual void OnDataOut();
};
