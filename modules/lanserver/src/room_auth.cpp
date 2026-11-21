#include "room_auth.h"
#include <cstdint>
#include <cstring>
#include <stdlib.h>
#include <time.h>
#include <vector>

namespace
{
uint32_t Rotr(uint32_t iValue, uint32_t iBits)
{
	return (iValue >> iBits) | (iValue << (32 - iBits));
}

void Sha256(const uint8_t* pData, size_t iLength, uint8_t vOut[32])
{
	static const uint32_t kK[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};
	uint32_t vHash[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::vector<uint8_t> vMessage(pData, pData + iLength);
	vMessage.push_back(0x80);
	while ((vMessage.size() % 64) != 56)
	{
		vMessage.push_back(0);
	}
	uint64_t iBits = (uint64_t)iLength * 8;
	for (int iShift = 56; iShift >= 0; iShift -= 8)
	{
		vMessage.push_back((uint8_t)(iBits >> iShift));
	}
	for (size_t iOffset = 0; iOffset < vMessage.size(); iOffset += 64)
	{
		uint32_t vWords[64];
		for (int iIndex = 0; iIndex < 16; ++iIndex)
		{
			const uint8_t* pWord = &vMessage[iOffset + iIndex * 4];
			vWords[iIndex] = ((uint32_t)pWord[0] << 24) | ((uint32_t)pWord[1] << 16) | ((uint32_t)pWord[2] << 8) | pWord[3];
		}
		for (int iIndex = 16; iIndex < 64; ++iIndex)
		{
			uint32_t iSmall = Rotr(vWords[iIndex - 15], 7) ^ Rotr(vWords[iIndex - 15], 18) ^ (vWords[iIndex - 15] >> 3);
			uint32_t iLarge = Rotr(vWords[iIndex - 2], 17) ^ Rotr(vWords[iIndex - 2], 19) ^ (vWords[iIndex - 2] >> 10);
			vWords[iIndex] = vWords[iIndex - 16] + iSmall + vWords[iIndex - 7] + iLarge;
		}
		uint32_t a = vHash[0], b = vHash[1], c = vHash[2], d = vHash[3], e = vHash[4], f = vHash[5], g = vHash[6], h = vHash[7];
		for (int iIndex = 0; iIndex < 64; ++iIndex)
		{
			uint32_t iSum1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
			uint32_t iChoice = (e & f) ^ ((~e) & g);
			uint32_t iTemp1 = h + iSum1 + iChoice + kK[iIndex] + vWords[iIndex];
			uint32_t iSum0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
			uint32_t iMajor = (a & b) ^ (a & c) ^ (b & c);
			uint32_t iTemp2 = iSum0 + iMajor;
			h = g;
			g = f;
			f = e;
			e = d + iTemp1;
			d = c;
			c = b;
			b = a;
			a = iTemp1 + iTemp2;
		}
		vHash[0] += a;
		vHash[1] += b;
		vHash[2] += c;
		vHash[3] += d;
		vHash[4] += e;
		vHash[5] += f;
		vHash[6] += g;
		vHash[7] += h;
	}
	for (int iIndex = 0; iIndex < 8; ++iIndex)
	{
		vOut[iIndex * 4] = (uint8_t)(vHash[iIndex] >> 24);
		vOut[iIndex * 4 + 1] = (uint8_t)(vHash[iIndex] >> 16);
		vOut[iIndex * 4 + 2] = (uint8_t)(vHash[iIndex] >> 8);
		vOut[iIndex * 4 + 3] = (uint8_t)vHash[iIndex];
	}
}

void HmacSha256(const std::string& sKey, const std::string& sMessage, uint8_t vOut[32])
{
	uint8_t vKey[64];
	memset(vKey, 0, sizeof(vKey));
	if (sKey.size() > 64)
	{
		Sha256(reinterpret_cast<const uint8_t*>(sKey.data()), sKey.size(), vKey);
	}
	else
	{
		memcpy(vKey, sKey.data(), sKey.size());
	}
	uint8_t vInner[64];
	uint8_t vOuter[64];
	for (int iIndex = 0; iIndex < 64; ++iIndex)
	{
		vInner[iIndex] = vKey[iIndex] ^ 0x36;
		vOuter[iIndex] = vKey[iIndex] ^ 0x5c;
	}
	std::vector<uint8_t> vFirst(vInner, vInner + 64);
	vFirst.insert(vFirst.end(), sMessage.begin(), sMessage.end());
	uint8_t vInnerHash[32];
	Sha256(vFirst.data(), vFirst.size(), vInnerHash);
	std::vector<uint8_t> vSecond(vOuter, vOuter + 64);
	vSecond.insert(vSecond.end(), vInnerHash, vInnerHash + 32);
	Sha256(vSecond.data(), vSecond.size(), vOut);
}

int B64Value(char cChar)
{
	if (cChar >= 'A' && cChar <= 'Z') return cChar - 'A';
	if (cChar >= 'a' && cChar <= 'z') return cChar - 'a' + 26;
	if (cChar >= '0' && cChar <= '9') return cChar - '0' + 52;
	if (cChar == '-' || cChar == '+') return 62;
	if (cChar == '_' || cChar == '/') return 63;
	return -1;
}

bool B64UrlDecode(const std::string& sText, std::string& sOut)
{
	std::string sPad = sText;
	while (sPad.size() % 4 != 0)
	{
		sPad.push_back('=');
	}
	sOut.clear();
	for (size_t iIndex = 0; iIndex < sPad.size(); iIndex += 4)
	{
		int iA = B64Value(sPad[iIndex]);
		int iB = B64Value(sPad[iIndex + 1]);
		int iC = sPad[iIndex + 2] == '=' ? 0 : B64Value(sPad[iIndex + 2]);
		int iD = sPad[iIndex + 3] == '=' ? 0 : B64Value(sPad[iIndex + 3]);
		if (iA < 0 || iB < 0 || iC < 0 || iD < 0)
		{
			return false;
		}
		sOut.push_back((char)((iA << 2) | (iB >> 4)));
		if (sPad[iIndex + 2] != '=')
		{
			sOut.push_back((char)(((iB & 15) << 4) | (iC >> 2)));
		}
		if (sPad[iIndex + 3] != '=')
		{
			sOut.push_back((char)(((iC & 3) << 6) | iD));
		}
	}
	return true;
}

bool SameBytes(const uint8_t* pLeft, const uint8_t* pRight, size_t iLength)
{
	uint8_t iDiff = 0;
	for (size_t iIndex = 0; iIndex < iLength; ++iIndex)
	{
		iDiff |= pLeft[iIndex] ^ pRight[iIndex];
	}
	return iDiff == 0;
}
}

bool TicketOwner(const std::string& sSecret, const std::string& sRoom, const std::string& sToken, std::string& sOwner)
{
	const size_t iDot = sToken.find('.');
	if (iDot == std::string::npos || sSecret.empty())
	{
		return false;
	}
	std::string sPayload;
	std::string sSignature;
	if (!B64UrlDecode(sToken.substr(0, iDot), sPayload) || !B64UrlDecode(sToken.substr(iDot + 1), sSignature) || sSignature.size() != 32)
	{
		return false;
	}
	uint8_t vMac[32];
	HmacSha256(sSecret, sPayload, vMac);
	if (!SameBytes(vMac, reinterpret_cast<const uint8_t*>(sSignature.data()), 32))
	{
		return false;
	}
	const size_t iFirst = sPayload.find('\n');
	const size_t iSecond = iFirst == std::string::npos ? std::string::npos : sPayload.find('\n', iFirst + 1);
	if (iFirst == std::string::npos || iSecond == std::string::npos)
	{
		return false;
	}
	if (sPayload.substr(0, iFirst) != sRoom)
	{
		return false;
	}
	sOwner = sPayload.substr(iFirst + 1, iSecond - iFirst - 1);
	const long long iExpires = atoll(sPayload.c_str() + iSecond + 1);
	return !sOwner.empty() && iExpires > (long long)time(nullptr);
}
