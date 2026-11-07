#pragma once
#include "oi_rpc.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class RoomAttr : public ::OI::Rpc::RpcData
{
public:
    uint8_t iTcpRelay{};
    uint32_t iVersion{};
    
    explicit RoomAttr (){}
    RoomAttr ( const uint8_t & l_iTcpRelay, const uint32_t & l_iVersion )
    {
        iTcpRelay = l_iTcpRelay;
        iVersion = l_iVersion;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, iTcpRelay);
        os.WritePb(1, iVersion);
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
            case 0: os.ReadPb(field_size, iTcpRelay); break;
            case 1: os.ReadPb(field_size, iVersion); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        RoomAttr decoded(*this);
        if (in.isMember("iTcpRelay") && !::OI::Rpc::FromJsonValue(in["iTcpRelay"], decoded.iTcpRelay)) return false;
        if (in.isMember("iVersion") && !::OI::Rpc::FromJsonValue(in["iVersion"], decoded.iVersion)) return false;
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
        ::OI::Rpc::ToJsonValue(out["iTcpRelay"], iTcpRelay);
        ::OI::Rpc::ToJsonValue(out["iVersion"], iVersion);
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
