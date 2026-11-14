#pragma once
#include "oi_rpc.h"
#include "oi_rpcdata_roommember.h"
#include "oi_rpcdata_roomattr.h"

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
        static constexpr std::array<::OI::Base::PbField, 3> fields = {{
            OI_PB_FIELD(RoomRecord, 0, iRelayCount),
            OI_PB_FIELD(RoomRecord, 1, vMembers),
            OI_PB_FIELD(RoomRecord, 2, stAttr),
        }};
        std::array<uint8_t, 3> seen{};
        return os.ReadFields(this, fields, seen);
    }
};
}}
