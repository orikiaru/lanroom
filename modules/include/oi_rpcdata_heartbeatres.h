#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class HeartbeatRes : public ::OI::Rpc::RpcData
{
public:
    int32_t iCode{};
    uint32_t iVirtualIp{};
    std::vector<RoomPeer> vPeers{};
    
    explicit HeartbeatRes (){}
    HeartbeatRes ( const int32_t & l_iCode, const uint32_t & l_iVirtualIp, const std::vector<RoomPeer> & l_vPeers )
    {
        iCode = l_iCode;
        iVirtualIp = l_iVirtualIp;
        vPeers = l_vPeers;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iCode);
        os.WritePb(1, iVirtualIp);
        os.WritePb(2, vPeers);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        vPeers.clear();
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, iCode); break;
            case 1: os.ReadPb(field_size, iVirtualIp); break;
            case 2: os.ReadPb(field_size, vPeers); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        HeartbeatRes decoded(*this);
        if (in.isMember("iCode") && !::OI::Rpc::FromJsonValue(in["iCode"], decoded.iCode)) return false;
        if (in.isMember("iVirtualIp") && !::OI::Rpc::FromJsonValue(in["iVirtualIp"], decoded.iVirtualIp)) return false;
        if (in.isMember("vPeers") && !::OI::Rpc::FromJsonValue(in["vPeers"], decoded.vPeers)) return false;
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
        ::OI::Rpc::ToJsonValue(out["iCode"], iCode);
        ::OI::Rpc::ToJsonValue(out["iVirtualIp"], iVirtualIp);
        ::OI::Rpc::ToJsonValue(out["vPeers"], vPeers);
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
