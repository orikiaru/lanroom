#pragma once
#include "oi_rpc.h"
#include "oi_json.h"
#include <memory>
#include <string>

namespace OI { namespace RpcData {
class ProbeReq : public ::OI::Rpc::RpcData
{
public:
    std::string sRoom{};
    std::string sMac{};
    uint8_t iSawPunch{};
    
    explicit ProbeReq (){}
    ProbeReq ( const std::string & l_sRoom, const std::string & l_sMac, const uint8_t & l_iSawPunch )
    {
        sRoom = l_sRoom;
        sMac = l_sMac;
        iSawPunch = l_iSawPunch;
        
    }
    virtual ::OI::Base::BytesStream& serialize(::OI::Base::BytesStream& os) const
    {
        os.WritePb(0, sRoom);
        os.WritePb(1, sMac);
        os.WritePb(2, iSawPunch);
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
            case 1: os.ReadPb(field_size, sMac); break;
            case 2: os.ReadPb(field_size, iSawPunch); break;
            default: os.SkipPb(field_size); break;
            }
        }
        return os;
    }
    bool FromJson(const Json::Value& in)
    {
        if (!in.isObject()) return false;
        ProbeReq decoded(*this);
        if (in.isMember("sRoom") && !::OI::Rpc::FromJsonValue(in["sRoom"], decoded.sRoom)) return false;
        if (in.isMember("sMac") && !::OI::Rpc::FromJsonValue(in["sMac"], decoded.sMac)) return false;
        if (in.isMember("iSawPunch") && !::OI::Rpc::FromJsonValue(in["iSawPunch"], decoded.iSawPunch)) return false;
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
        ::OI::Rpc::ToJsonValue(out["sMac"], sMac);
        ::OI::Rpc::ToJsonValue(out["iSawPunch"], iSawPunch);
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
