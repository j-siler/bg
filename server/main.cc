#include <grpcpp/grpcpp.h>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <algorithm> // std::remove

#include "bg/v1/bg.grpc.pb.h"
#include "bg/v1/bg.pb.h"

#include "../board.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;

namespace proto = ::bg::v1;
namespace BGNS  = ::BG;

// ---------- tiny logger ----------
struct Logger {
  std::ofstream out;
  explicit Logger(const char* path){ out.open(path, std::ios::app); }
  template<typename... Args>
  void log(Args&&... parts){
    if (!out) return;
    auto t = std::time(nullptr); std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " ";
    (out << ... << parts) << "\n";
    out.flush();
  }
};
// ---------------------------------

// A single in-memory match ("m1") with one Board and many subscribers.
struct Match {
  BGNS::Board board;
  uint64_t version = 0;

  std::mutex mtx;
  std::vector< ServerReaderWriter<proto::Envelope, proto::Envelope>* > subs;

  std::unique_ptr<Logger> log;

  Match(){
    if (std::getenv("BG_SERVER_LOG"))
      log = std::make_unique<Logger>("bg_server.log");

    // Start a new game: opening doubles are rerolled, no auto-doubles.
    BGNS::Rules rules{};
    rules.openingDoublePolicy = BGNS::Rules::OpeningDoublePolicy::REROLL;
    rules.maxOpeningAutoDoubles = 0;
    board.startGame(rules);
  }

  // Convert the current board to proto::BoardState
  proto::BoardState toProtoState(){
    proto::BoardState out;

    // points
    for (int p=1; p<=24; ++p){
      unsigned cntW = board.countAt(BGNS::WHITE, p);
      unsigned cntB = board.countAt(BGNS::BLACK, p);
      auto* pt = out.add_points();
      if (cntW==0 && cntB==0){ pt->set_side(proto::NONE); pt->set_count(0); }
      else if (cntW>0){ pt->set_side(proto::WHITE); pt->set_count(cntW); }
      else { pt->set_side(proto::BLACK); pt->set_count(cntB); }
    }

    // bars/off
    out.set_white_bar(board.countBar(BGNS::WHITE));
    out.set_black_bar(board.countBar(BGNS::BLACK));
    out.set_white_off(board.countOff(BGNS::WHITE));
    out.set_black_off(board.countOff(BGNS::BLACK));

    // cube holder (no numeric cube field in your proto)
    auto h = board.cubeHolder();
    out.set_cube_holder(h==BGNS::WHITE?proto::WHITE : h==BGNS::BLACK?proto::BLACK : proto::NONE);

    // phase
    switch (board.phase()){
      case BGNS::Phase::OpeningRoll:  out.set_phase(proto::OPENING_ROLL); break;
      case BGNS::Phase::AwaitingRoll: out.set_phase(proto::AWAITING_ROLL); break;
      case BGNS::Phase::Moving:       out.set_phase(proto::MOVING); break;
      case BGNS::Phase::CubeOffered:  out.set_phase(proto::CUBE_OFFERED); break;
    }

    // side to move
    auto s = board.sideToMove();
    out.set_side_to_move(s==BGNS::WHITE?proto::WHITE : s==BGNS::BLACK?proto::BLACK : proto::NONE);

    // dice
    for (int d : board.diceRemaining()) out.add_dice_remaining(d);

    return out;
  }

  void sendError(ServerReaderWriter<proto::Envelope, proto::Envelope>* rw, int code, const std::string& msg){
    proto::Envelope ev;
    auto* e = ev.mutable_evt()->mutable_error();
    e->set_code(code);
    e->set_message(msg);
    rw->Write(ev);
    if (log) log->log("[err] code=", code, " msg=", msg);
  }

  void sendSnapshot(ServerReaderWriter<proto::Envelope, proto::Envelope>* rw){
    proto::Envelope ev;
    auto* snap = ev.mutable_evt()->mutable_snapshot();
    snap->set_version(++version);
    *snap->mutable_state() = toProtoState();
    rw->Write(ev);
  }

  void broadcastSnapshot(){
    proto::Envelope ev;
    auto* snap = ev.mutable_evt()->mutable_snapshot();
    snap->set_version(++version);
    *snap->mutable_state() = toProtoState();
    for (auto* s : subs) s->Write(ev);
  }

  void broadcastMsg(const char* m){
    if (!log) return;
    log->log(m);
  }
};

static Match g_match;

// ---------------- Services ----------------

class AuthServiceImpl final : public proto::AuthService::Service {
public:
  Status Login(ServerContext*, const proto::LoginReq* req, proto::LoginResp* resp) override {
    // Accept anything for now; echo a token
    resp->set_user_id(req->username());
    resp->set_token("ok");
    if (g_match.log) g_match.log->log("[auth] user=", req->username(), " logged in");
    return Status::OK;
  }
};

class MatchServiceImpl final : public proto::MatchService::Service {
public:
  Status Stream(ServerContext*,
                ServerReaderWriter<proto::Envelope, proto::Envelope>* rw) override
  {
    {
      std::lock_guard<std::mutex> lk(g_match.mtx);
      g_match.subs.push_back(rw);
    }

    proto::Envelope in;
    while (rw->Read(&in)){
      if (!in.has_cmd()) continue;
      const auto& cmd = in.cmd();

      std::lock_guard<std::mutex> lk(g_match.mtx);

      // join
      if (cmd.has_join_match()){
        g_match.broadcastMsg("[cmd] join_match");
        g_match.sendSnapshot(rw);
        continue;
      }

      // snapshot request
      if (cmd.has_request_snapshot()){
        g_match.broadcastMsg("[cmd] request_snapshot");
        g_match.sendSnapshot(rw);
        continue;
      }

      // roll: OpeningRoll vs normal
      if (cmd.has_roll_dice()){
        try{
          if (g_match.board.phase()==BGNS::Phase::OpeningRoll){
            auto wb = g_match.board.rollOpening(); (void)wb;
            g_match.broadcastMsg("[cmd] roll (opening)");
            g_match.broadcastSnapshot();
          } else {
            g_match.board.rollDice();
            g_match.broadcastMsg("[cmd] roll");
            g_match.broadcastSnapshot();
          }
        } catch (const std::exception& ex){
          g_match.sendError(rw, 409, ex.what());
        }
        continue;
      }

      // set dice: OpeningRoll vs normal
      if (cmd.has_set_dice()){
        int d1 = cmd.set_dice().d1();
        int d2 = cmd.set_dice().d2();
        try{
          if (g_match.board.phase()==BGNS::Phase::OpeningRoll){
            bool ok = g_match.board.setOpeningDice(d1,d2);
            g_match.broadcastMsg("[cmd] set (opening)");
            if (!ok){
              g_match.sendError(rw, 409, "opening doubles — reroll required");
            }
            g_match.broadcastSnapshot();
          } else {
            g_match.board.setDice(d1,d2);
            g_match.broadcastMsg("[cmd] set");
            g_match.broadcastSnapshot();
          }
        } catch (const std::exception& ex){
          g_match.sendError(rw, 409, ex.what());
        }
        continue;
      }

      // step
      if (cmd.has_apply_step()){
        int from = cmd.apply_step().from();
        int pip  = cmd.apply_step().pip();
        bool ok = g_match.board.applyStep(from, pip);
        if (!ok){
          g_match.sendError(rw, 409, g_match.board.lastError());
        } else {
          g_match.broadcastMsg("[cmd] step");
          g_match.broadcastSnapshot();
        }
        continue;
      }

      // undo
      if (cmd.has_undo_step()){
        bool ok = g_match.board.undoStep();
        if (!ok){
          g_match.sendError(rw, 409, "undoStep failed");
        } else {
          g_match.broadcastMsg("[cmd] undo");
          g_match.broadcastSnapshot();
        }
        continue;
      }

      // commit
      if (cmd.has_commit_turn()){
        bool ok = g_match.board.commitTurn();
        if (!ok){
          g_match.sendError(rw, 409, g_match.board.lastError());
        } else {
          g_match.broadcastMsg("[cmd] commit");
          g_match.broadcastSnapshot();
        }
        continue;
      }

      // doubling cube
      if (cmd.has_offer_cube()){
        if (!g_match.board.offerCube()) g_match.sendError(rw, 409, g_match.board.lastError());
        else { g_match.broadcastMsg("[cmd] double"); g_match.broadcastSnapshot(); }
        continue;
      }
      if (cmd.has_take_cube()){
        if (!g_match.board.takeCube()) g_match.sendError(rw, 409, g_match.board.lastError());
        else { g_match.broadcastMsg("[cmd] take"); g_match.broadcastSnapshot(); }
        continue;
      }
      if (cmd.has_drop_cube()){
        if (!g_match.board.dropCube()) g_match.sendError(rw, 409, g_match.board.lastError());
        else { g_match.broadcastMsg("[cmd] drop"); g_match.broadcastSnapshot(); }
        continue;
      }

      // unknown -> snapshot (helps debugging)
      g_match.sendSnapshot(rw);
    }

    // remove subscriber
    {
      std::lock_guard<std::mutex> lk(g_match.mtx);
      auto& v = g_match.subs;
      v.erase(std::remove(v.begin(), v.end(), rw), v.end());
    }
    return Status::OK;
  }
};

// ---------------- main ----------------

int main(int argc, char** argv){
  (void)argc; (void)argv;

  std::string addr("0.0.0.0:50051");
  AuthServiceImpl auth;
  MatchServiceImpl match;

  ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&auth);
  builder.RegisterService(&match);
  std::unique_ptr<Server> server(builder.BuildAndStart());

  if (g_match.log) g_match.log->log("[server] listening on ", addr);

  server->Wait();
  return 0;
}
