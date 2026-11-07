#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class HeartbeatRes : public ::OI::Rpc::RpcData
{
public:
    uint32_t iVirtualIp{};
    std::vector<RoomPeer> vPeers{};
    RoomAttr stAttr{};
    
    explicit HeartbeatRes (){}
    HeartbeatRes ( const uint32_t & l_iVirtualIp, const std::vector<RoomPeer> & l_vPeers, const RoomAttr & l_stAttr )
    {
        iVirtualIp = l_iVirtualIp;
        vPeers = l_vPeers;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iVirtualIp);
        os.WritePb(1, vPeers);
        os.WritePb(2, stAttr);
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
            case 0: os.ReadPb(field_size, iVirtualIp); break;
            case 1: os.ReadPb(field_size, vPeers); break;
            case 2: os.ReadPb(field_size, stAttr); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        HeartbeatRes decoded(*this);
        if (in.isMember("iVirtualIp") && !::OI::Rpc::FromJsonValue(in["iVirtualIp"], decoded.iVirtualIp)) return false;
        if (in.isMember("vPeers") && !::OI::Rpc::FromJsonValue(in["vPeers"], decoded.vPeers)) return false;
        if (in.isMember("stAttr") && !::OI::Rpc::FromJsonValue(in["stAttr"], decoded.stAttr)) return false;
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
        ::OI::Rpc::ToJsonValue(out["iVirtualIp"], iVirtualIp);
        ::OI::Rpc::ToJsonValue(out["vPeers"], vPeers);
        ::OI::Rpc::ToJsonValue(out["stAttr"], stAttr);
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
