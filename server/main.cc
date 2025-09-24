#include <grpcpp/grpcpp.h>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <iostream>

#include "bg/v1/bg.pb.h"
#include "bg/v1/bg.grpc.pb.h"

#include "../board.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReaderWriter;
using grpc::Status;

namespace proto = ::bg::v1;
namespace BGNS  = ::BG;

struct Conn {
  ServerReaderWriter<proto::Envelope, proto::Envelope>* rw{};
  std::string user_id;
};

struct Match {
  std::mutex m;
  uint64_t version = 0;
  BGNS::Board board;
  BGNS::Rules rules;
  std::unordered_set<Conn*> conns;

  Match(){
    board.startGame(rules); // OpeningRoll phase
  }

  proto::BoardState toProtoState() {
    proto::BoardState out;
    BGNS::Board::State s;
    board.getState(s);
    for (int i=0;i<24;++i){
      auto* p = out.add_points();
      if (s.points[i].side==BGNS::WHITE){ p->set_side(proto::WHITE); p->set_count(s.points[i].count); }
      else if (s.points[i].side==BGNS::BLACK){ p->set_side(proto::BLACK); p->set_count(s.points[i].count); }
      else { p->set_side(proto::NONE); p->set_count(0); }
    }
    out.set_white_bar(s.whitebar);
    out.set_black_bar(s.blackbar);
    out.set_white_off(s.whiteoff);
    out.set_black_off(s.blackoff);
    out.set_cube_value(board.cubeValue());
    out.set_cube_holder(board.cubeHolder()==BGNS::WHITE?proto::WHITE:
                        board.cubeHolder()==BGNS::BLACK?proto::BLACK:proto::NONE);
    switch(board.phase()){
      case BGNS::Phase::OpeningRoll:   out.set_phase(proto::OPENING_ROLL);  break;
      case BGNS::Phase::AwaitingRoll:  out.set_phase(proto::AWAITING_ROLL); break;
      case BGNS::Phase::Moving:        out.set_phase(proto::MOVING);        break;
      case BGNS::Phase::CubeOffered:   out.set_phase(proto::CUBE_OFFERED);  break;
    }
    out.set_side_to_move(board.sideToMove()==BGNS::WHITE?proto::WHITE:
                         board.sideToMove()==BGNS::BLACK?proto::BLACK:proto::NONE);
    for (auto d: board.diceRemaining()) out.add_dice_remaining(d);
    return out;
  }

  void broadcast(const proto::Envelope& ev){
    for (auto* c : conns) c->rw->Write(ev);
  }

  void sendSnapshotToAll(){
    proto::Envelope ev;
    auto* h = ev.mutable_header(); h->set_server_version(++version);
    auto* sn = ev.mutable_evt()->mutable_snapshot();
    sn->set_version(version);
    *sn->mutable_state() = toProtoState();
    broadcast(ev);
  }
};

struct Registry {
  std::mutex m;
  std::unordered_map<std::string,std::unique_ptr<Match>> matches;

  Match* getOrCreate(const std::string& id){
    std::lock_guard<std::mutex> lk(m);
    auto it = matches.find(id);
    if (it!=matches.end()) return it->second.get();
    auto mm = std::make_unique<Match>();
    auto* raw = mm.get();
    matches.emplace(id, std::move(mm));
    return raw;
  }
} REG;

class AuthServiceImpl final : public proto::AuthService::Service {
public:
  Status Login(ServerContext*, const proto::LoginReq* req, proto::LoginResp* resp) override {
    resp->set_user_id("u_"+req->username());
    resp->set_token("DEV-"+req->username()); // TODO: JWT
    return Status::OK;
  }
};

class MatchServiceImpl final : public proto::MatchService::Service {
public:
  Status Stream(ServerContext* ctx, ServerReaderWriter<proto::Envelope, proto::Envelope>* rw) override {
    Conn conn; conn.rw = rw;
    proto::Envelope in;
    Match* match = nullptr;

    while (rw->Read(&in)) {
      const auto& h = in.header();

      if (in.has_cmd() && in.cmd().has_join_match()){
        const auto& jm = in.cmd().join_match();
        match = REG.getOrCreate(jm.match_id());
        {
          std::lock_guard<std::mutex> lk(match->m);
          match->conns.insert(&conn);
        }
        match->sendSnapshotToAll();
        continue;
      }

      if (!match) {
        proto::Envelope ev; *ev.mutable_header() = h;
        ev.mutable_evt()->mutable_error()->set_code(400);
        ev.mutable_evt()->mutable_error()->set_message("JoinMatch first");
        rw->Write(ev);
        continue;
      }

      if (in.has_cmd()) {
        std::lock_guard<std::mutex> lk(match->m);
        auto& B = match->board;

        auto errorOut = [&](int code, const std::string& msg){
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_evt()->mutable_error()->set_code(code);
          ev.mutable_evt()->mutable_error()->set_message(msg);
          rw->Write(ev);
        };

        const auto& cmd = in.cmd();

        if (cmd.has_request_snapshot()){
          match->sendSnapshotToAll();
          continue;
        }

        if (cmd.has_roll_dice()){
          try{
            B.rollDice();
            proto::Envelope ev; *ev.mutable_header() = h;
            ev.mutable_header()->set_server_version(++match->version);
            auto* ds = ev.mutable_evt()->mutable_dice_set();
            for (auto d : B.diceRemaining()) ds->add_dice(d);
            ds->set_actor(B.sideToMove()==BGNS::WHITE?proto::WHITE:proto::BLACK);
            match->broadcast(ev);
          }catch(const std::exception& e){
            errorOut(409, e.what());
          }
          continue;
        }

        if (cmd.has_set_dice()){
          try{
            B.setDice(cmd.set_dice().d1(), cmd.set_dice().d2());
            proto::Envelope ev; *ev.mutable_header() = h;
            ev.mutable_header()->set_server_version(++match->version);
            auto* ds = ev.mutable_evt()->mutable_dice_set();
            for (auto d : B.diceRemaining()) ds->add_dice(d);
            ds->set_actor(B.sideToMove()==BGNS::WHITE?proto::WHITE:proto::BLACK);
            match->broadcast(ev);
          }catch(const std::exception& e){ errorOut(409, e.what()); }
          continue;
        }

        if (cmd.has_apply_step()){
          int from = cmd.apply_step().from();
          int pip  = cmd.apply_step().pip();
          if (!B.applyStep(from, pip)){
            errorOut(409, B.lastError());
            continue;
          }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          auto* st = ev.mutable_evt()->mutable_step_applied();
          st->set_from(from);
          st->set_pip(pip);
          st->set_actor(B.sideToMove()==BGNS::WHITE?proto::WHITE:proto::BLACK); // actor before commit
          st->set_to(-1);
          match->broadcast(ev);
          continue;
        }

        if (cmd.has_undo_step()){
          if (!B.undoStep()){ errorOut(409,"nothing to undo"); continue; }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          ev.mutable_evt()->mutable_step_undone();
          match->broadcast(ev);
          continue;
        }

        if (cmd.has_commit_turn()){
          if (!B.commitTurn()){ errorOut(409, B.lastError()); continue; }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          auto* tc = ev.mutable_evt()->mutable_turn_committed();
          tc->set_next_to_move(B.sideToMove()==BGNS::WHITE?proto::WHITE:proto::BLACK);
          match->broadcast(ev);
          match->sendSnapshotToAll();
          continue;
        }

        if (cmd.has_offer_cube()){
          if (!B.offerCube()){ errorOut(409, B.lastError()); continue; }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          auto* co = ev.mutable_evt()->mutable_cube_offered();
          co->set_from(B.sideToMove()==BGNS::WHITE?proto::BLACK:proto::WHITE); // offered to opponent
          co->set_cube_value(B.cubeValue()*2);
          match->broadcast(ev);
          continue;
        }

        if (cmd.has_take_cube()){
          if (!B.takeCube()){ errorOut(409, B.lastError()); continue; }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          auto* ct = ev.mutable_evt()->mutable_cube_taken();
          ct->set_holder(B.cubeHolder()==BGNS::WHITE?proto::WHITE:proto::BLACK);
          ct->set_cube_value(B.cubeValue());
          match->broadcast(ev);
          continue;
        }

        if (cmd.has_drop_cube()){
          if (!B.dropCube()){ errorOut(409, B.lastError()); continue; }
          proto::Envelope ev; *ev.mutable_header() = h;
          ev.mutable_header()->set_server_version(++match->version);
          auto* cd = ev.mutable_evt()->mutable_cube_dropped();
          cd->set_winner(B.sideToMove()==BGNS::WHITE?proto::BLACK:proto::WHITE);
          cd->set_final_cube(B.cubeValue());
          match->broadcast(ev);
          continue;
        }
      }
    }

    if (match){
      std::lock_guard<std::mutex> lk(match->m);
      match->conns.erase(&conn);
    }
    return Status::OK;
  }
};

int main(){
  ServerBuilder b;
  b.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
  AuthServiceImpl auth; MatchServiceImpl match;
  b.RegisterService(&auth); b.RegisterService(&match);
  std::unique_ptr<Server> server(b.BuildAndStart());
  std::cout << "bg_server on :50051\n";
  server->Wait();
  return 0;
}
