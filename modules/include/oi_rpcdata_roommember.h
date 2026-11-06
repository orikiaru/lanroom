#pragma once
#include "oi_rpc.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class RoomMember : public ::OI::Rpc::RpcData
{
public:
    std::string sMac{};
    uint32_t iSourceIp{};
    uint16_t iSourcePort{};
    uint32_t iVirtualIp{};
    uint8_t iDirect{};
    uint8_t iSawPunch{};
    int64_t iLastSeen{};
    
    explicit RoomMember (){}
    RoomMember ( const std::string & l_sMac, const uint32_t & l_iSourceIp, const uint16_t & l_iSourcePort, const uint32_t & l_iVirtualIp, const uint8_t & l_iDirect, const uint8_t & l_iSawPunch, const int64_t & l_iLastSeen )
    {
        sMac = l_sMac;
        iSourceIp = l_iSourceIp;
        iSourcePort = l_iSourcePort;
        iVirtualIp = l_iVirtualIp;
        iDirect = l_iDirect;
        iSawPunch = l_iSawPunch;
        iLastSeen = l_iLastSeen;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sMac);
        os.WritePb(1, iSourceIp);
        os.WritePb(2, iSourcePort);
        os.WritePb(3, iVirtualIp);
        os.WritePb(4, iDirect);
        os.WritePb(5, iSawPunch);
        os.WritePb(6, iLastSeen);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, sMac); break;
            case 1: os.ReadPb(field_size, iSourceIp); break;
            case 2: os.ReadPb(field_size, iSourcePort); break;
            case 3: os.ReadPb(field_size, iVirtualIp); break;
            case 4: os.ReadPb(field_size, iDirect); break;
            case 5: os.ReadPb(field_size, iSawPunch); break;
            case 6: os.ReadPb(field_size, iLastSeen); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        RoomMember decoded(*this);
        if (in.isMember("sMac") && !::OI::Rpc::FromJsonValue(in["sMac"], decoded.sMac)) return false;
        if (in.isMember("iSourceIp") && !::OI::Rpc::FromJsonValue(in["iSourceIp"], decoded.iSourceIp)) return false;
        if (in.isMember("iSourcePort") && !::OI::Rpc::FromJsonValue(in["iSourcePort"], decoded.iSourcePort)) return false;
        if (in.isMember("iVirtualIp") && !::OI::Rpc::FromJsonValue(in["iVirtualIp"], decoded.iVirtualIp)) return false;
        if (in.isMember("iDirect") && !::OI::Rpc::FromJsonValue(in["iDirect"], decoded.iDirect)) return false;
        if (in.isMember("iSawPunch") && !::OI::Rpc::FromJsonValue(in["iSawPunch"], decoded.iSawPunch)) return false;
        if (in.isMember("iLastSeen") && !::OI::Rpc::FromJsonValue(in["iLastSeen"], decoded.iLastSeen)) return false;
        *this = std::move(decoded);
        return true;
    }
    bool FromJson(const std::string& text)
    {
        Json::CharReaderBuilder builder;
        Json::Value root;
        std::string errs;
        const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        const char* begin = text.data();
        if (!reader->parse(begin, begin + text.size(), &root, &errs)) return false;
        return FromJson(root);
    }
    void ToJson(Json::Value& out) const
    {
        out = Json::Value(Json::objectValue);
        ::OI::Rpc::ToJsonValue(out["sMac"], sMac);
        ::OI::Rpc::ToJsonValue(out["iSourceIp"], iSourceIp);
        ::OI::Rpc::ToJsonValue(out["iSourcePort"], iSourcePort);
        ::OI::Rpc::ToJsonValue(out["iVirtualIp"], iVirtualIp);
        ::OI::Rpc::ToJsonValue(out["iDirect"], iDirect);
        ::OI::Rpc::ToJsonValue(out["iSawPunch"], iSawPunch);
        ::OI::Rpc::ToJsonValue(out["iLastSeen"], iLastSeen);
    }
    std::string ToJson() const
    {
        Json::Value out;
        ToJson(out);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, out);
    }
};
}}
