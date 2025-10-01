#pragma once
#include <memory>
#include <grpcpp/grpcpp.h>
#include "auth.hpp"
#include "match.hpp"
#include "logger.hpp"

// Use the ADMIN proto (separate from your game proto).
#include "admin/v1/admin.grpc.pb.h"
#include "admin/v1/admin.pb.h"

namespace BG {

class AuthServiceImpl final : public admin::v1::AuthService::Service {
public:
  AuthServiceImpl(AuthManager& auth, Logger& log) : auth_(auth), log_(log) {}
  ::grpc::Status Login(::grpc::ServerContext*,
                       const admin::v1::LoginReq* req,
                       admin::v1::LoginResp* resp) override;
  ::grpc::Status Logout(::grpc::ServerContext*,
                        const admin::v1::LogoutReq* req,
                        admin::v1::LogoutResp* resp) override;
private:
  AuthManager& auth_;
  Logger& log_;
};

class MatchServiceImpl final : public admin::v1::MatchService::Service {
public:
  MatchServiceImpl(MatchRegistry& reg, Logger& log) : reg_(reg), log_(log) {}
  ::grpc::Status CreateMatch(::grpc::ServerContext*,
                             const admin::v1::CreateMatchReq* req,
                             admin::v1::CreateMatchResp* resp) override;
  ::grpc::Status JoinMatch(::grpc::ServerContext*,
                           const admin::v1::JoinMatchReq* req,
                           admin::v1::JoinMatchResp* resp) override;
  ::grpc::Status LeaveMatch(::grpc::ServerContext*,
                            const admin::v1::LeaveMatchReq* req,
                            admin::v1::LeaveMatchResp* resp) override;
private:
  MatchRegistry& reg_;
  Logger& log_;
};

} // namespace BG
