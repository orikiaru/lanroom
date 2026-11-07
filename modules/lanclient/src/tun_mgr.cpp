#include "tun_mgr.h"
#include "lan_addr.h"
#include "room_mgr.h"
#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include "wintun.h"
#include <stdio.h>

namespace
{
const GUID kAdapterGuid = {0x8f6a1c2e, 0x4b7d, 0x4e11, {0x9c, 0x3a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60}};

struct WintunApi
{
	HMODULE hModule = NULL;
	WINTUN_CREATE_ADAPTER_FUNC* CreateAdapter = NULL;
	WINTUN_OPEN_ADAPTER_FUNC* OpenAdapter = NULL;
	WINTUN_CLOSE_ADAPTER_FUNC* CloseAdapter = NULL;
	WINTUN_START_SESSION_FUNC* StartSession = NULL;
	WINTUN_END_SESSION_FUNC* EndSession = NULL;
	WINTUN_GET_READ_WAIT_EVENT_FUNC* GetReadWaitEvent = NULL;
	WINTUN_RECEIVE_PACKET_FUNC* ReceivePacket = NULL;
	WINTUN_RELEASE_RECEIVE_PACKET_FUNC* ReleaseReceivePacket = NULL;
	WINTUN_ALLOCATE_SEND_PACKET_FUNC* AllocateSendPacket = NULL;
	WINTUN_SEND_PACKET_FUNC* SendPacket = NULL;
};

WintunApi g_stApi;
WINTUN_ADAPTER_HANDLE g_hAdapter = NULL;
WINTUN_SESSION_HANDLE g_hSession = NULL;
std::wstring g_sAdapterName;

std::wstring AdapterName(int iIndex)
{
	if (iIndex == 0)
	{
		return L"lanroom";
	}
	return L"lanroom" + std::to_wstring(iIndex + 1);
}

std::string Narrow(const std::wstring& sWide)
{
	std::string sText;
	sText.reserve(sWide.size());
	for (wchar_t iChar : sWide)
	{
		sText.push_back(iChar < 128 ? (char)iChar : '?');
	}
	return sText;
}
std::mutex g_stWriteLock;
std::atomic<bool> g_bRun{false};
std::thread g_stRead;

std::wstring DllPath()
{
	wchar_t vPath[MAX_PATH];
	DWORD iCount = GetModuleFileNameW(NULL, vPath, MAX_PATH);
	std::wstring sPath(vPath, vPath + iCount);
	size_t iSlash = sPath.find_last_of(L'\\');
	if (iSlash == std::wstring::npos)
	{
		return L"wintun.dll";
	}
	return sPath.substr(0, iSlash + 1) + L"wintun.dll";
}

bool LoadApi()
{
	if (g_stApi.hModule != NULL)
	{
		return true;
	}
	g_stApi.hModule = LoadLibraryW(DllPath().c_str());
	if (g_stApi.hModule == NULL)
	{
		return false;
	}
	g_stApi.CreateAdapter = (WINTUN_CREATE_ADAPTER_FUNC*)GetProcAddress(g_stApi.hModule, "WintunCreateAdapter");
	g_stApi.OpenAdapter = (WINTUN_OPEN_ADAPTER_FUNC*)GetProcAddress(g_stApi.hModule, "WintunOpenAdapter");
	g_stApi.CloseAdapter = (WINTUN_CLOSE_ADAPTER_FUNC*)GetProcAddress(g_stApi.hModule, "WintunCloseAdapter");
	g_stApi.StartSession = (WINTUN_START_SESSION_FUNC*)GetProcAddress(g_stApi.hModule, "WintunStartSession");
	g_stApi.EndSession = (WINTUN_END_SESSION_FUNC*)GetProcAddress(g_stApi.hModule, "WintunEndSession");
	g_stApi.GetReadWaitEvent = (WINTUN_GET_READ_WAIT_EVENT_FUNC*)GetProcAddress(g_stApi.hModule, "WintunGetReadWaitEvent");
	g_stApi.ReceivePacket = (WINTUN_RECEIVE_PACKET_FUNC*)GetProcAddress(g_stApi.hModule, "WintunReceivePacket");
	g_stApi.ReleaseReceivePacket = (WINTUN_RELEASE_RECEIVE_PACKET_FUNC*)GetProcAddress(g_stApi.hModule, "WintunReleaseReceivePacket");
	g_stApi.AllocateSendPacket = (WINTUN_ALLOCATE_SEND_PACKET_FUNC*)GetProcAddress(g_stApi.hModule, "WintunAllocateSendPacket");
	g_stApi.SendPacket = (WINTUN_SEND_PACKET_FUNC*)GetProcAddress(g_stApi.hModule, "WintunSendPacket");
	return g_stApi.CreateAdapter && g_stApi.OpenAdapter && g_stApi.CloseAdapter && g_stApi.StartSession && g_stApi.EndSession && g_stApi.GetReadWaitEvent && g_stApi.ReceivePacket && g_stApi.ReleaseReceivePacket && g_stApi.AllocateSendPacket && g_stApi.SendPacket;
}
}

TunMgr::TunMgr()
{
}

bool TunMgr::Open()
{
	if (!LoadApi())
	{
		m_sStatus = "找不到 wintun.dll";
		return false;
	}
	DWORD iLastError = ERROR_ACCESS_DENIED;
	for (int iIndex = 0; iIndex < 4; ++iIndex)
	{
		std::wstring sName = AdapterName(iIndex);
		GUID stGuid = kAdapterGuid;
		stGuid.Data4[7] = (unsigned char)iIndex;
		WINTUN_ADAPTER_HANDLE hAdapter = g_stApi.OpenAdapter(sName.c_str());
		bool bCreated = false;
		if (hAdapter == NULL)
		{
			hAdapter = g_stApi.CreateAdapter(sName.c_str(), L"lanroom", &stGuid);
			bCreated = hAdapter != NULL;
		}
		if (hAdapter == NULL)
		{
			iLastError = GetLastError();
			continue;
		}
		WINTUN_SESSION_HANDLE hSession = g_stApi.StartSession(hAdapter, WINTUN_MIN_RING_CAPACITY);
		if (hSession == NULL)
		{
			iLastError = GetLastError();
			if (bCreated)
			{
				g_stApi.CloseAdapter(hAdapter);
			}
			continue;
		}
		g_hAdapter = hAdapter;
		g_hSession = hSession;
		g_sAdapterName = sName;
		m_sStatus = Narrow(sName);
		return true;
	}
	m_sStatus = iLastError == ERROR_ACCESS_DENIED ? "创建虚拟网卡需要管理员权限" : "创建虚拟网卡失败";
	return false;
}

void TunMgr::Close()
{
	std::lock_guard<std::mutex> stLock(g_stWriteLock);
	if (g_hSession != NULL)
	{
		g_stApi.EndSession(g_hSession);
		g_hSession = NULL;
	}
	if (g_hAdapter != NULL)
	{
		g_stApi.CloseAdapter(g_hAdapter);
		g_hAdapter = NULL;
	}
}

void RunNetsh(const std::wstring& sCommand)
{
	std::wstring sMutable = L"netsh " + sCommand;
	STARTUPINFOW stStart = {};
	stStart.cb = sizeof(stStart);
	PROCESS_INFORMATION stProcess = {};
	if (!CreateProcessW(NULL, &sMutable[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &stStart, &stProcess))
	{
		return;
	}
	WaitForSingleObject(stProcess.hProcess, 5000);
	CloseHandle(stProcess.hProcess);
	CloseHandle(stProcess.hThread);
}

void TunMgr::ApplyAddress()
{
	uint32_t iAddress = RoomMgr::GetInstance().Current().iVirtualIp;
	if (iAddress == 0 || iAddress == m_iAddress || g_hAdapter == NULL)
	{
		return;
	}
	std::string sText = ::Lan::Ipv4::Text(iAddress);
	std::wstring sAddress(sText.begin(), sText.end());
	std::wstring sName = g_sAdapterName.empty() ? L"lanroom" : g_sAdapterName;
	RunNetsh(L"interface ipv4 set subinterface \"" + sName + L"\" mtu=" + std::to_wstring(::Lan::kFrameMax) + L" store=active");
	RunNetsh(L"interface ip set address name=\"" + sName + L"\" static " + sAddress + L" 255.255.255.0");
	m_iAddress = iAddress;
	m_sStatus = Narrow(sName) + " " + sText + "/24";
}

void TunMgr::ReadLoop()
{
	HANDLE hWait = g_stApi.GetReadWaitEvent(g_hSession);
	while (g_bRun.load())
	{
		ApplyAddress();
		DWORD iSize = 0;
		BYTE* pPacket = g_stApi.ReceivePacket(g_hSession, &iSize);
		if (pPacket != NULL)
		{
			if (iSize > 0 && iSize <= ::Lan::kFrameMax)
			{
				RoomMgr::GetInstance().PushOutbound(std::vector<uint8_t>(pPacket, pPacket + iSize));
			}
			g_stApi.ReleaseReceivePacket(g_hSession, pPacket);
			continue;
		}
		if (GetLastError() != ERROR_NO_MORE_ITEMS)
		{
			break;
		}
		WaitForSingleObject(hWait, 200);
	}
}

bool TunMgr::Write(const std::vector<uint8_t>& vFrame)
{
	std::lock_guard<std::mutex> stLock(g_stWriteLock);
	if (g_hSession == NULL || vFrame.empty() || vFrame.size() > ::Lan::kFrameMax)
	{
		return false;
	}
	BYTE* pPacket = g_stApi.AllocateSendPacket(g_hSession, (DWORD)vFrame.size());
	if (pPacket == NULL)
	{
		return false;
	}
	memcpy(pPacket, vFrame.data(), vFrame.size());
	g_stApi.SendPacket(g_hSession, pPacket);
	return true;
}

#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <thread>

namespace
{
int g_iFd = -1;
std::mutex g_stWriteLock;
std::atomic<bool> g_bRun{false};
std::thread g_stRead;
}

TunMgr::TunMgr()
{
}

bool TunMgr::Open()
{
	g_iFd = open("/dev/net/tun", O_RDWR);
	if (g_iFd < 0)
	{
		m_sStatus = "打不开 /dev/net/tun";
		return false;
	}
	ifreq stRequest;
	memset(&stRequest, 0, sizeof(stRequest));
	stRequest.ifr_flags = IFF_TUN | IFF_NO_PI;
	strncpy(stRequest.ifr_name, "lanroom0", IFNAMSIZ - 1);
	if (ioctl(g_iFd, TUNSETIFF, &stRequest) != 0)
	{
		close(g_iFd);
		g_iFd = -1;
		m_sStatus = "创建 tun 失败";
		return false;
	}
	int iSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (iSocket >= 0)
	{
		stRequest.ifr_mtu = (int)::Lan::kFrameMax;
		ioctl(iSocket, SIOCSIFMTU, &stRequest);
		close(iSocket);
	}
	m_sStatus = "lanroom0";
	return true;
}

void TunMgr::Close()
{
	std::lock_guard<std::mutex> stLock(g_stWriteLock);
	if (g_iFd >= 0)
	{
		close(g_iFd);
		g_iFd = -1;
	}
}

void TunMgr::ApplyAddress()
{
	uint32_t iAddress = RoomMgr::GetInstance().Current().iVirtualIp;
	if (iAddress == 0 || iAddress == m_iAddress || g_iFd < 0)
	{
		return;
	}
	int iSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (iSocket < 0)
	{
		return;
	}
	ifreq stRequest;
	memset(&stRequest, 0, sizeof(stRequest));
	strncpy(stRequest.ifr_name, "lanroom0", IFNAMSIZ - 1);
	sockaddr_in* pAddress = (sockaddr_in*)&stRequest.ifr_addr;
	pAddress->sin_family = AF_INET;
	pAddress->sin_addr.s_addr = htonl(iAddress);
	if (ioctl(iSocket, SIOCSIFADDR, &stRequest) != 0)
	{
		close(iSocket);
		return;
	}
	pAddress->sin_addr.s_addr = htonl(0xFFFFFF00u);
	ioctl(iSocket, SIOCSIFNETMASK, &stRequest);
	if (ioctl(iSocket, SIOCGIFFLAGS, &stRequest) == 0)
	{
		stRequest.ifr_flags |= IFF_UP | IFF_RUNNING;
		ioctl(iSocket, SIOCSIFFLAGS, &stRequest);
	}
	close(iSocket);
	m_iAddress = iAddress;
	m_sStatus = "lanroom0 " + ::Lan::Ipv4::Text(iAddress) + "/24";
}

void TunMgr::ReadLoop()
{
	std::vector<uint8_t> vPacket(::Lan::kFrameMax);
	while (g_bRun.load())
	{
		ApplyAddress();
		int iCount = (int)read(g_iFd, vPacket.data(), vPacket.size());
		if (iCount <= 0)
		{
			if (!g_bRun.load())
			{
				break;
			}
			continue;
		}
		RoomMgr::GetInstance().PushOutbound(std::vector<uint8_t>(vPacket.begin(), vPacket.begin() + iCount));
	}
}

bool TunMgr::Write(const std::vector<uint8_t>& vFrame)
{
	std::lock_guard<std::mutex> stLock(g_stWriteLock);
	if (g_iFd < 0 || vFrame.empty() || vFrame.size() > ::Lan::kFrameMax)
	{
		return false;
	}
	return write(g_iFd, vFrame.data(), vFrame.size()) == (ssize_t)vFrame.size();
}
#endif

bool TunMgr::Start()
{
	if (g_bRun.load())
	{
		return true;
	}
	if (!Open())
	{
		return false;
	}
	g_bRun.store(true);
	g_stRead = std::thread(&TunMgr::ReadLoop, this);
	return true;
}

void TunMgr::Stop()
{
	g_bRun.store(false);
	Close();
	if (g_stRead.joinable())
	{
		g_stRead.join();
	}
}

std::string TunMgr::Status() const
{
	return m_sStatus;
}
