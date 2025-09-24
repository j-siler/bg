// client-tui/main.cc
#include <grpcpp/grpcpp.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <cctype>
#include <locale.h>

#include "bg/v1/bg.grpc.pb.h"
#include "bg/v1/bg.pb.h"

#include "../board.hpp"
#include "../ncurses_renderer.hpp"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

namespace proto = ::bg::v1;

// helpers
static std::string trim(const std::string& s){
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

static bool parse2(const std::string& line, int& a, int& b){
  std::istringstream is(line);
  if ((is >> a >> b))
    return true;
  return false;
}

static BG::Side toSide(proto::Side s){
  switch (s) {
    case proto::WHITE: return BG::WHITE;
    case proto::BLACK: return BG::BLACK;
    default:           return BG::NONE;
  }
}

struct Model {
  proto::BoardState st;
  uint64_t ver = 0;
  std::string msg;
};

static void fillBoardState(const proto::BoardState& p, BG::Board::State& out){
  for (int i = 0; i < 24; ++i){
    out.points[i].count = 0;
    out.points[i].side  = BG::NONE;
    if (i < p.points_size()){
      const auto& pt = p.points(i);
      out.points[i].count = pt.count();
      out.points[i].side  = toSide(pt.side());
    }
  }
  out.whitebar = p.white_bar();
  out.blackbar = p.black_bar();
  out.whiteoff = p.white_off();
  out.blackoff = p.black_off();
}

int main(){
  // ncurses init
  setlocale(LC_ALL, "");
  initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
  if (has_colors()) {
    start_color(); use_default_colors();
    init_pair(1, COLOR_WHITE,  -1);
    init_pair(2, COLOR_CYAN,   -1);
    init_pair(3, COLOR_YELLOW, -1);
    init_pair(4, COLOR_GREEN,  -1);
  }

  // renderer window
  WINDOW* bw = derwin(stdscr, BG::NcursesRenderer::kHeight, BG::NcursesRenderer::kWidth,
                      1, (COLS - BG::NcursesRenderer::kWidth) / 2);
  BG::NcursesRenderer renderer(bw);

  // gRPC stubs
  auto chan = grpc::CreateChannel("127.0.0.1:50051", grpc::InsecureChannelCredentials());
  std::unique_ptr<proto::AuthService::Stub>  auth (proto::AuthService::NewStub(chan));
  std::unique_ptr<proto::MatchService::Stub> match(proto::MatchService::NewStub(chan));

  // login
  proto::LoginReq lr; lr.set_username("alice"); lr.set_password("pw");
  proto::LoginResp lresp;
  {
    ClientContext ctx;
    Status s = auth->Login(&ctx, lr, &lresp);
    if (!s.ok()) { endwin(); fprintf(stderr, "login failed\n"); return 1; }
  }

  grpc::ClientContext ctx;
  auto stream = match->Stream(&ctx);

  // join match m1
  proto::Envelope j;
  j.mutable_header()->set_proto_version(1);
  j.mutable_header()->set_match_id("m1");
  j.mutable_cmd()->mutable_join_match()->set_match_id("m1");
  j.mutable_cmd()->mutable_join_match()->set_role(proto::JoinMatch::PLAYER);
  stream->Write(j);

  std::mutex mtx;
  std::atomic<bool> running{true};
  Model model;

  // reader thread
  std::thread reader([&](){
    proto::Envelope ev;
    while (stream->Read(&ev)){
      if (!ev.has_evt()) continue;
      const auto& e = ev.evt();
      {
        std::lock_guard<std::mutex> lk(mtx);
        if (e.has_snapshot()) {
          model.st  = e.snapshot().state();
          model.ver = e.snapshot().version();
          model.msg = "snapshot";
        } else if (e.has_dice_set()) {
          model.msg = "dice set";
        } else if (e.has_step_applied()) {
          model.msg = "step applied";
        } else if (e.has_step_undone()) {
          model.msg = "step undone";
        } else if (e.has_turn_committed()) {
          model.msg = "turn committed";
        } else if (e.has_error()) {
          model.msg = std::string("error ") + std::to_string(e.error().code()) + ": " + e.error().message();
        }
      }

      BG::Board::State s{};
      fillBoardState(model.st, s);
      renderer.render(s);
      mvprintw(0, 0, "bg_tui — Enter=commit · \"FROM PIP\"=step · 'roll' 'set d1 d2' 'undo' 'double' 'take' 'drop' · 'quit'");
      move(LINES - 2, 0); clrtoeol(); attron(COLOR_PAIR(4)); printw("%s", model.msg.c_str()); attroff(COLOR_PAIR(4));
      refresh();
    }
    running = false;
  });

  auto send = [&](const proto::Envelope& e){ stream->Write(e); };

  // initial snapshot request
  {
    proto::Envelope rq;
    rq.mutable_header()->set_proto_version(1);
    rq.mutable_header()->set_match_id("m1");
    rq.mutable_cmd()->mutable_request_snapshot();
    send(rq);
  }

  // REPL
  while (running){
    move(LINES - 1, 0); clrtoeol(); printw("> "); echo(); curs_set(1);
    char buf[256]; getnstr(buf, 255); noecho(); curs_set(0);
    std::string line = trim(buf);
    if (line == "quit" || line == "exit") break;

    // empty line → commit
    if (line.empty()){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_commit_turn(); send(e); continue;
    }

    // two numbers → step
    int a, b;
    if (parse2(line, a, b)){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_apply_step()->set_from(a);
      e.mutable_cmd()->mutable_apply_step()->set_pip(b);
      send(e); continue;
    }

    // keywords
    if (line == "roll"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_roll_dice(); send(e); continue;
    }
    if (line.rfind("set ", 0) == 0){
      int d1, d2; std::istringstream is(line.substr(4));
      if ((is >> d1 >> d2)){
        proto::Envelope e; e.mutable_header()->set_match_id("m1");
        e.mutable_cmd()->mutable_set_dice()->set_d1(d1);
        e.mutable_cmd()->mutable_set_dice()->set_d2(d2);
        send(e);
      }
      continue;
    }
    if (line == "undo"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_undo_step(); send(e); continue;
    }
    if (line == "double"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_offer_cube(); send(e); continue;
    }
    if (line == "take"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_take_cube(); send(e); continue;
    }
    if (line == "drop"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_drop_cube(); send(e); continue;
    }
    if (line == "snap"){
      proto::Envelope e; e.mutable_header()->set_match_id("m1");
      e.mutable_cmd()->mutable_request_snapshot(); send(e); continue;
    }
  }

  stream->WritesDone();
  reader.join();
  auto st = stream->Finish();
  endwin();
  return st.ok() ? 0 : 1;
}
