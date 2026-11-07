#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roompeer.h"
#include "oi_rpcdata_roomattr.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class RoomCastReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::vector<RoomPeer> vPeers{};
    RoomAttr stAttr{};
    
    explicit RoomCastReq (){}
    RoomCastReq ( const std::string & l_sRoom, const std::vector<RoomPeer> & l_vPeers, const RoomAttr & l_stAttr )
    {
        sRoom = l_sRoom;
        vPeers = l_vPeers;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
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
            case 0: os.ReadPb(field_size, sRoom); break;
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
        RoomCastReq decoded(*this);
        if (in.isMember("sRoom") && !::OI::Rpc::FromJsonValue(in["sRoom"], decoded.sRoom)) return false;
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
        ::OI::Rpc::ToJsonValue(out["sRoom"], sRoom);
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
