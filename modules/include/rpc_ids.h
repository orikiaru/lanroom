#pragma once
#include "oi_header.h"
#include "rpc_enums.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_heartbeatres.h"
#include "oi_rpcdata_leaveres.h"
#include "oi_rpcdata_punchres.h"
#include "oi_rpcdata_frameres.h"
#include "oi_rpcdata_proberes.h"
#include "oi_rpcdata_carryres.h"
#include "oi_rpcdata_roomcastres.h"
namespace OI { namespace Rpc {
template<typename T> struct RpcProtoId;
}}
namespace OI { namespace RpcData {
class JoinReq;
class JoinRes;
class HeartbeatReq;
class HeartbeatRes;
class LeaveReq;
class LeaveRes;
class PunchReq;
class PunchRes;
class FrameReq;
class FrameRes;
class ProbeReq;
class ProbeRes;
class CarryReq;
class CarryRes;
class RoomCastReq;
class RoomCastRes;
}}
namespace OI { namespace Rpc {
template<> struct RpcProtoId<::OI::RpcData::JoinReq> { using Response = ::OI::RpcData::JoinRes; static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::JoinRes> { static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatReq> { using Response = ::OI::RpcData::HeartbeatRes; static const proto_type_t iValue = 4102; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatRes> { static const proto_type_t iValue = 4102; };
template<> struct RpcProtoId<::OI::RpcData::LeaveReq> { using Response = ::OI::RpcData::LeaveRes; static const proto_type_t iValue = 4109; };
template<> struct RpcProtoId<::OI::RpcData::LeaveRes> { static const proto_type_t iValue = 4109; };
template<> struct RpcProtoId<::OI::RpcData::PunchReq> { using Response = ::OI::RpcData::PunchRes; static const proto_type_t iValue = 4103; };
template<> struct RpcProtoId<::OI::RpcData::PunchRes> { static const proto_type_t iValue = 4103; };
template<> struct RpcProtoId<::OI::RpcData::FrameReq> { using Response = ::OI::RpcData::FrameRes; static const proto_type_t iValue = 4104; };
template<> struct RpcProtoId<::OI::RpcData::FrameRes> { static const proto_type_t iValue = 4104; };
template<> struct RpcProtoId<::OI::RpcData::ProbeReq> { using Response = ::OI::RpcData::ProbeRes; static const proto_type_t iValue = 4106; };
template<> struct RpcProtoId<::OI::RpcData::ProbeRes> { static const proto_type_t iValue = 4106; };
template<> struct RpcProtoId<::OI::RpcData::CarryReq> { using Response = ::OI::RpcData::CarryRes; static const proto_type_t iValue = 4107; };
template<> struct RpcProtoId<::OI::RpcData::CarryRes> { static const proto_type_t iValue = 4107; };
template<> struct RpcProtoId<::OI::RpcData::RoomCastReq> { using Response = ::OI::RpcData::RoomCastRes; static const proto_type_t iValue = 4108; };
template<> struct RpcProtoId<::OI::RpcData::RoomCastRes> { static const proto_type_t iValue = 4108; };
}}

