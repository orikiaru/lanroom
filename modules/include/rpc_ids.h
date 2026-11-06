#pragma once
#include "oi_header.h"
#include "oi_rpcdata_joinres.h"
#include "oi_rpcdata_heartbeatres.h"
namespace OI { namespace Rpc {
template<typename T> struct RpcProtoId;
}}
namespace OI { namespace RpcData {
class JoinReq;
class JoinRes;
class HeartbeatReq;
class HeartbeatRes;
}}
namespace OI { namespace Rpc {
template<> struct RpcProtoId<::OI::RpcData::JoinReq> { using Response = ::OI::RpcData::JoinRes; static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::JoinRes> { static const proto_type_t iValue = 4101; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatReq> { using Response = ::OI::RpcData::HeartbeatRes; static const proto_type_t iValue = 4102; };
template<> struct RpcProtoId<::OI::RpcData::HeartbeatRes> { static const proto_type_t iValue = 4102; };
}}

