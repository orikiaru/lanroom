#pragma once
#include "oi_rpc.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class RelayReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    uint32_t iVirtualIp{};
    std::string sFrame{};
    
    explicit RelayReq (){}
    RelayReq ( const std::string & l_sRoom, const uint32_t & l_iVirtualIp, const std::string & l_sFrame )
    {
        sRoom = l_sRoom;
        iVirtualIp = l_iVirtualIp;
        sFrame = l_sFrame;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, iVirtualIp);
        os.WritePb(2, sFrame);
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
            case 0: os.ReadPb(field_size, sRoom); break;
            case 1: os.ReadPb(field_size, iVirtualIp); break;
            case 2: os.ReadPb(field_size, sFrame); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        RelayReq decoded(*this);
        if (in.isMember("sRoom") && !::OI::Rpc::FromJsonValue(in["sRoom"], decoded.sRoom)) return false;
        if (in.isMember("iVirtualIp") && !::OI::Rpc::FromJsonValue(in["iVirtualIp"], decoded.iVirtualIp)) return false;
        if (in.isMember("sFrame") && !::OI::Rpc::FromJsonValue(in["sFrame"], decoded.sFrame)) return false;
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
        ::OI::Rpc::ToJsonValue(out["iVirtualIp"], iVirtualIp);
        ::OI::Rpc::ToJsonValue(out["sFrame"], sFrame);
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
