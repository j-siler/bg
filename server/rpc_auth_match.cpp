#include "rpc_auth_match.hpp"

namespace BG {

using ::grpc::Status;

static SeatSide fromWire(admin::v1::SeatSide s){
  switch (s){
    case admin::v1::SEAT_WHITE:    return SeatSide::White;
    case admin::v1::SEAT_BLACK:    return SeatSide::Black;
    case admin::v1::SEAT_OBSERVER: return SeatSide::Observer;
    default:                       return SeatSide::Observer;
  }
}

Status AuthServiceImpl::Login(::grpc::ServerContext*,
                              const admin::v1::LoginReq* req,
                              admin::v1::LoginResp* resp)
{
  if (!req || req->user().empty() || req->pass().empty()) {
    resp->set_ok(false);
    resp->set_reason("missing user/pass");
    return Status::OK;
  }
  User u;
  if (auth_.login(req->user(), req->pass(), u)) {
    log_.info(EventType::UserLogin, u.id, "login via RPC");
    resp->set_ok(true);
    return Status::OK;
  }
  resp->set_ok(false);
  resp->set_reason("bad creds or already logged in");
  return Status::OK;
}

Status AuthServiceImpl::Logout(::grpc::ServerContext*,
                               const admin::v1::LogoutReq* req,
                               admin::v1::LogoutResp* resp)
{
  if (!req || req->user().empty()) {
    resp->set_ok(false);
    resp->set_reason("missing user");
    return Status::OK;
  }
  auth_.logout(req->user());
  log_.info(EventType::UserLogout, req->user(), "logout via RPC");
  resp->set_ok(true);
  return Status::OK;
}

Status MatchServiceImpl::CreateMatch(::grpc::ServerContext*,
                                     const admin::v1::CreateMatchReq* req,
                                     admin::v1::CreateMatchResp* resp)
{
  if (!req || req->name().empty()) {
    resp->set_ok(false);
    resp->set_reason("missing name");
    return Status::OK;
  }
  const uint32_t len = req->continuous() ? 0u : req->length_points();
  auto m = reg_.create(req->name(), len, req->continuous());
  if (!m) {
    resp->set_ok(false);
    resp->set_reason("create failed");
    return Status::OK;
  }
  resp->set_ok(true);
  return Status::OK;
}

Status MatchServiceImpl::JoinMatch(::grpc::ServerContext*,
                                   const admin::v1::JoinMatchReq* req,
                                   admin::v1::JoinMatchResp* resp)
{
  if (!req || req->name().empty() || req->user().empty()) {
    resp->set_ok(false);
    resp->set_reason("missing name/user");
    return Status::OK;
  }
  PlayerRef pr{req->user(), req->user()};
  std::string err;
  auto m = reg_.join(req->name(), pr, fromWire(req->side()), err);
  if (!m) {
    resp->set_ok(false);
    resp->set_reason(err);
    return Status::OK;
  }
  resp->set_ok(true);
  return Status::OK;
}

Status MatchServiceImpl::LeaveMatch(::grpc::ServerContext*,
                                    const admin::v1::LeaveMatchReq* req,
                                    admin::v1::LeaveMatchResp* resp)
{
  if (!req || req->name().empty() || req->user().empty()) {
    resp->set_ok(false);
    resp->set_reason("missing name/user");
    return Status::OK;
  }
  MatchRegistry::LeaveResult res{};
  auto m = reg_.leave(req->name(), req->user(), res);
  if (!m) {
    resp->set_ok(false);
    resp->set_reason("not found");
    return Status::OK;
  }
  switch (res){
    case MatchRegistry::LeaveResult::NotMember:
      resp->set_ok(false); resp->set_reason("not a participant"); break;
    case MatchRegistry::LeaveResult::LeftObserver:
      resp->set_ok(true);  resp->set_reason("left observer"); break;
    case MatchRegistry::LeaveResult::LeftSeat:
      resp->set_ok(true);  resp->set_reason("left seat; match suspended"); break;
    case MatchRegistry::LeaveResult::NotFound:
      resp->set_ok(false); resp->set_reason("not found"); break;
  }
  return Status::OK;
}

} // namespace BG
