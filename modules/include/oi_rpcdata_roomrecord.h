#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roommember.h"
#include "oi_rpcdata_roomattr.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class RoomRecord : public ::OI::Rpc::RpcData
{
public:
    uint32_t iRelayCount{};
    std::vector<RoomMember> vMembers{};
    RoomAttr stAttr{};
    
    explicit RoomRecord (){}
    RoomRecord ( const uint32_t & l_iRelayCount, const std::vector<RoomMember> & l_vMembers, const RoomAttr & l_stAttr )
    {
        iRelayCount = l_iRelayCount;
        vMembers = l_vMembers;
        stAttr = l_stAttr;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iRelayCount);
        os.WritePb(1, vMembers);
        os.WritePb(2, stAttr);
        return os;
    }
    virtual ::OI::Base::BytesStream& deserialize(::OI::Base::BytesStream& os)
    {
        vMembers.clear();
        while (os.RemainSize() > 0)
        {
            uint32_t field_seq = 0;
            uint32_t field_size = 0;
            os.ReadPbHeader(field_seq, field_size);
            switch (field_seq)
            {
            case 0: os.ReadPb(field_size, iRelayCount); break;
            case 1: os.ReadPb(field_size, vMembers); break;
            case 2: os.ReadPb(field_size, stAttr); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        RoomRecord decoded(*this);
        if (in.isMember("iRelayCount") && !::OI::Rpc::FromJsonValue(in["iRelayCount"], decoded.iRelayCount)) return false;
        if (in.isMember("vMembers") && !::OI::Rpc::FromJsonValue(in["vMembers"], decoded.vMembers)) return false;
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
        ::OI::Rpc::ToJsonValue(out["iRelayCount"], iRelayCount);
        ::OI::Rpc::ToJsonValue(out["vMembers"], vMembers);
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
