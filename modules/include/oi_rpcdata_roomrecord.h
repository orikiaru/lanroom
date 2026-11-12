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
};
}}
