#include <grpcpp/grpcpp.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <cctype>
#include <locale.h>
#include <fstream>
#include <ctime>
#include <iomanip>
#include <csignal>
#include <functional>
#include <memory>
#include <chrono>   // <-- added

#include "bg/v1/bg.grpc.pb.h"
#include "bg/v1/bg.pb.h"

#include "../board.hpp"
#include "../ncurses_renderer.hpp"

using grpc::ClientContext;
using grpc::Status;
namespace proto = ::bg::v1;

// ---------- tiny logger ----------
struct Logger {
  std::ofstream out;
  explicit Logger(const char* path){ out.open(path, std::ios::app); }
  bool ok() const { return out.good(); }
  template<typename... Args>
  void log(Args&&... parts){
    if (!out) return;
    auto t = std::time(nullptr);
    std::tm tm{};
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

static std::string trim(const std::string& s){
  auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
static bool parse2(const std::string& line, int& a, int& b){
  std::istringstream is(line);
  return (is >> a >> b) ? true : false;
}
static BG::Side toSide(proto::Side s){
  switch (s) {
    case proto::WHITE: return BG::WHITE;
    case proto::BLACK: return BG::BLACK;
    default:           return BG::NONE;
  }
}

struct Model { proto::BoardState st; uint64_t ver=0; std::string msg; };

// global flags toggled by signals / reader thread
static std::atomic<bool> g_resized{false};
static std::atomic<bool> g_need_repaint{true};
extern "C" void on_winch(int){ g_resized = true; }

static void fillBoardState(const proto::BoardState& p, BG::Board::State& out){
  for (int i = 0; i < 24; ++i){
    out.points[i].count = 0; out.points[i].side = BG::NONE;
    if (i < p.points_size()){
      const auto& pt = p.points(i);
      out.points[i].count = pt.count();
      out.points[i].side  = toSide(pt.side());
    }
  }
  out.whitebar = p.white_bar(); out.blackbar = p.black_bar();
  out.whiteoff = p.white_off(); out.blackoff = p.black_off();
}

int main(){
  // logging?
  std::unique_ptr<Logger> logHolder;
  Logger* log = nullptr;
  if (std::getenv("BG_CLIENT_LOG")){
    logHolder = std::make_unique<Logger>("bg_tui.log");
    if (logHolder->ok()) log = logHolder.get();
  }

  // ncurses init
  setlocale(LC_ALL, "");
  initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(1);
  timeout(50); // non-blocking getch with 50ms tick
  if (has_colors()) { start_color(); use_default_colors();
    init_pair(1, COLOR_WHITE,-1); init_pair(2, COLOR_CYAN,-1);
    init_pair(3, COLOR_YELLOW,-1); init_pair(4, COLOR_GREEN,-1); }

  // SIGWINCH handler
  std::signal(SIGWINCH, on_winch);

  // board window creator (centered) — needs recursion; use std::function
  WINDOW* bw = nullptr;
  std::unique_ptr<BG::NcursesRenderer> renderer;
  std::function<WINDOW*()> makeBoardWin;
  makeBoardWin = [&]()->WINDOW*{
    if (bw) { delwin(bw); bw = nullptr; }
    int rows, cols; getmaxyx(stdscr, rows, cols);
    const int W = BG::NcursesRenderer::kWidth;
    const int H = BG::NcursesRenderer::kHeight;
    int x = (cols > W) ? (cols - W)/2 : 0;
    int y = 1;
    if (rows < H + 3 || cols < W) {
      erase();
      mvprintw(0,0,"Terminal too small: need at least %dx%d. Current %dx%d.",
               W, H+3, cols, rows);
      mvprintw(2,0,"Resize the window to continue…");
      refresh();
      return nullptr;
    }
    bw = derwin(stdscr, H, W, y, x);
    return bw;
  };
  bw = makeBoardWin();
  if (!bw) { // wait until sized
    while (!bw) {
      int ch = getch(); (void)ch; // just pumping
      if (g_resized.exchange(false)) { endwin(); refresh(); resizeterm(0,0); }
      bw = makeBoardWin();
    }
  }
  renderer = std::make_unique<BG::NcursesRenderer>(bw);

  auto paintUI = [&](const Model& model, bool fullClear){
    if (fullClear) { clearok(stdscr, TRUE); erase(); }

    // header/help
    mvprintw(0, 0, "bg_tui — Enter=commit · two numbers or 'step FROM PIP' · 'roll' 'set d1 d2' 'undo' 'double' 'take' 'drop' · 'help' · 'quit'");
    wnoutrefresh(stdscr);

    // If we don't have a snapshot yet, don't paint an "empty" board.
    if (model.st.points_size() == 0) {
      // show a gentle status so users know why it's blank
      move(LINES-2, 0); clrtoeol();
      attron(COLOR_PAIR(4));
      addnstr("waiting for server snapshot…", COLS - 1);
      attroff(COLOR_PAIR(4));
      wnoutrefresh(stdscr);
      // keep the board window in sync (but empty)
      werase(bw);
      wnoutrefresh(bw);
      doupdate();
      return;
    }

    // board
    BG::Board::State s{}; fillBoardState(model.st, s);
    werase(bw);
    renderer->render(s);
    wnoutrefresh(bw);

    //==== status (phase/side/dice + last message)
    move(LINES-2, 0); clrtoeol();
    
    // phase
    std::string phaseStr = "Unknown";
    switch (model.st.phase()){
    case proto::OPENING_ROLL:  phaseStr = "OpeningRoll"; break;
    case proto::AWAITING_ROLL: phaseStr = "AwaitingRoll"; break;
    case proto::MOVING:        phaseStr = "Moving"; break;
    case proto::CUBE_OFFERED:  phaseStr = "CubeOffered"; break;
    }
    
    // side
    std::string sideStr = "NONE";
    switch (model.st.side_to_move()){
    case proto::WHITE: sideStr = "WHITE"; break;
    case proto::BLACK: sideStr = "BLACK"; break;
    default: break;
    }
    
    // dice
    std::string diceStr = "[";
    for (int i = 0; i < model.st.dice_remaining_size(); ++i){
      if (i) diceStr += ",";
      diceStr += std::to_string(model.st.dice_remaining(i));
    }
    diceStr += "]";
    
    // cube holder (optional)
    std::string holderStr = "NONE";
    switch (model.st.cube_holder()){
    case proto::WHITE: holderStr = "WHITE"; break;
    case proto::BLACK: holderStr = "BLACK"; break;
    default: break;
    }
    
    std::string info = "phase=" + phaseStr +
      "  side=" + sideStr +
      "  dice=" + diceStr +
      "  cubeHolder=" + holderStr;
    
    if (!model.msg.empty()) info += "  ·  " + model.msg;
    
    attron(COLOR_PAIR(4));
    addnstr(info.c_str(), COLS - 1); // avoid wrapping
    attroff(COLOR_PAIR(4));
    wnoutrefresh(stdscr);
    doupdate();
  };

  // gRPC stubs
  auto chan = grpc::CreateChannel("127.0.0.1:50051", grpc::InsecureChannelCredentials());
  std::unique_ptr<proto::AuthService::Stub>  auth (proto::AuthService::NewStub(chan));
  std::unique_ptr<proto::MatchService::Stub> match(proto::MatchService::NewStub(chan));

  // login
  proto::LoginReq lr; lr.set_username("alice"); lr.set_password("pw");
  proto::LoginResp lresp;
  { ClientContext ctx; Status s = auth->Login(&ctx, lr, &lresp);
    if (!s.ok()) { endwin(); fprintf(stderr, "login failed\n"); return 1; } }
  if (log) log->log("[client] login ok user=alice");

  grpc::ClientContext ctx;
  auto stream = match->Stream(&ctx);

  // join match m1
  proto::Envelope j;
  j.mutable_header()->set_proto_version(1);
  j.mutable_header()->set_match_id("m1");
  j.mutable_cmd()->mutable_join_match()->set_match_id("m1");
  j.mutable_cmd()->mutable_join_match()->set_role(proto::JoinMatch::PLAYER);
  stream->Write(j);
  if (log) log->log("[client] join m1");

  std::mutex mtx;
  std::atomic<bool> running{true};
  Model model; model.msg = "connected (type 'help')";
  g_need_repaint = true;

  // reader thread: update model ONLY; request repaint via flag
  std::thread reader([&](){
    proto::Envelope ev;
    while (stream->Read(&ev)){
      if (!ev.has_evt()) continue;
      const auto& e = ev.evt();
      {
        std::lock_guard<std::mutex> lk(mtx);
        if (e.has_snapshot()){ model.st = e.snapshot().state(); model.ver = e.snapshot().version(); model.msg = "snapshot"; if (log) log->log("[evt] snapshot v=", model.ver); }
        else if (e.has_dice_set()){ model.msg = "dice set"; if (log) log->log("[evt] dice_set"); }
        else if (e.has_step_applied()){ model.msg = "step applied"; if (log) log->log("[evt] step_applied from=", e.step_applied().from(), " pip=", e.step_applied().pip()); }
        else if (e.has_step_undone()){ model.msg = "step undone"; if (log) log->log("[evt] step_undone"); }
        else if (e.has_turn_committed()){ model.msg = "turn committed"; if (log) log->log("[evt] turn_committed"); }
        else if (e.has_error()){ model.msg = std::string("error ")+std::to_string(e.error().code())+": "+e.error().message(); if (log) log->log("[evt] error code=", e.error().code(), " msg=", e.error().message()); }
      }
      g_need_repaint = true;
    }
    running = false;
  });

  // initial snapshot request
  { proto::Envelope rq; rq.mutable_header()->set_proto_version(1); rq.mutable_header()->set_match_id("m1");
    rq.mutable_cmd()->mutable_request_snapshot(); stream->Write(rq); if (log) log->log("[client] request_snapshot"); }

  // ---- NEW: wait briefly for the first snapshot so the first paint isn't empty ----
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
      {
        std::lock_guard<std::mutex> lk(mtx);
        if (model.st.points_size() > 0) break;
      }
      if (std::chrono::steady_clock::now() >= deadline) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  // -------------------------------------------------------------------------------

  // simple input buffer (non-blocking line editor)
  std::string ibuf;

  auto draw_prompt = [&](){
    move(LINES-1, 0); clrtoeol();
    addstr("> ");
    addstr(ibuf.c_str());
    wnoutrefresh(stdscr);
    doupdate();
    // place cursor at end of buffer
    int y = LINES-1; int x = 2 + (int)ibuf.size();
    move(y, x);
  };

  // first paint (now after the brief wait above)
  paintUI(model, /*fullClear*/true);
  draw_prompt();

  auto send = [&](const proto::Envelope& e){ stream->Write(e); };

  // REPL/main loop — fully non-blocking
  while (running){
    // handle terminal resize immediately
    if (g_resized.load()){
      g_resized = false;
      flushinp(); // drop stale keys from resize burst
      endwin(); refresh();
#if defined(NCURSES_VERSION)
      resizeterm(0,0);
#endif
      bw = makeBoardWin();
      if (bw){
        renderer = std::make_unique<BG::NcursesRenderer>(bw);
        std::lock_guard<std::mutex> lk(mtx);
        paintUI(model, /*fullClear*/true);
      }
      draw_prompt();
    }

    // repaint requested by reader thread?
    if (g_need_repaint.load()){
      g_need_repaint = false;
      std::lock_guard<std::mutex> lk(mtx);
      paintUI(model, /*fullClear*/false);
      draw_prompt();
    }

    // non-blocking key processing
    int ch = getch();
    if (ch == ERR){
      continue; // no key this tick
    }

    if (ch == KEY_RESIZE){
      // some terms send KEY_RESIZE; treat like SIGWINCH
      g_resized = true;
      continue;
    }

    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8){
      if (!ibuf.empty()) ibuf.pop_back();
      draw_prompt();
      continue;
    }

    if (ch == '\n' || ch == '\r'){
      std::string line = trim(ibuf);
      ibuf.clear();
      draw_prompt();

      if (line.empty()){
        proto::Envelope e; e.mutable_header()->set_match_id("m1");
        e.mutable_cmd()->mutable_commit_turn(); send(e);
        if (log) log->log("[cmd] commit");
        { std::lock_guard<std::mutex> lk(mtx); model.msg = "commit sent"; }
        g_need_repaint = true;
        continue;
      }
      if (line == "quit" || line == "exit") break;
      if (line == "help"){
        std::lock_guard<std::mutex> lk(mtx);
        model.msg = "two numbers=step, 'step a b', Enter=commit, 'roll', 'set d1 d2', 'undo', 'double', 'take', 'drop', 'snap', 'redraw', 'quit'";
        g_need_repaint = true;
        continue;
      }
      if (line == "redraw"){
        std::lock_guard<std::mutex> lk(mtx);
        paintUI(model, /*fullClear*/true);
        draw_prompt();
        continue;
      }

      // explicit 'step a b'
      if (line.rfind("step ", 0) == 0){
        int a, b; std::istringstream is(line.substr(5));
        if ((is >> a >> b)){
          proto::Envelope e; e.mutable_header()->set_match_id("m1");
          e.mutable_cmd()->mutable_apply_step()->set_from(a);
          e.mutable_cmd()->mutable_apply_step()->set_pip(b);
          send(e); if (log) log->log("[cmd] step ", a, " ", b);
        } else {
          std::lock_guard<std::mutex> lk(mtx); model.msg = "bad step syntax: 'step FROM PIP'";
        }
        g_need_repaint = true;
        continue;
      }

      // two-number shorthand
      {
        int a, b;
        if (parse2(line, a, b)){
          proto::Envelope e; e.mutable_header()->set_match_id("m1");
          e.mutable_cmd()->mutable_apply_step()->set_from(a);
          e.mutable_cmd()->mutable_apply_step()->set_pip(b);
          send(e); if (log) log->log("[cmd] step ", a, " ", b);
          g_need_repaint = true;
          continue;
        }
      }

      // keywords
      if (line == "roll"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_roll_dice(); send(e); if (log) log->log("[cmd] roll"); g_need_repaint = true; continue; }
      if (line.rfind("set ", 0) == 0){
        int d1, d2; std::istringstream is(line.substr(4));
        if ((is >> d1 >> d2)){ proto::Envelope e; e.mutable_header()->set_match_id("m1");
          e.mutable_cmd()->mutable_set_dice()->set_d1(d1); e.mutable_cmd()->mutable_set_dice()->set_d2(d2);
          send(e); if (log) log->log("[cmd] set ", d1, " ", d2);
        } else {
          std::lock_guard<std::mutex> lk(mtx); model.msg = "bad set syntax: 'set d1 d2'";
        }
        g_need_repaint = true; continue;
      }
      if (line == "undo"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_undo_step(); send(e); if (log) log->log("[cmd] undo"); g_need_repaint = true; continue; }
      if (line == "double"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_offer_cube(); send(e); if (log) log->log("[cmd] double"); g_need_repaint = true; continue; }
      if (line == "take"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_take_cube(); send(e); if (log) log->log("[cmd] take"); g_need_repaint = true; continue; }
      if (line == "drop"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_drop_cube(); send(e); if (log) log->log("[cmd] drop"); g_need_repaint = true; continue; }
      if (line == "snap"){ proto::Envelope e; e.mutable_header()->set_match_id("m1"); e.mutable_cmd()->mutable_request_snapshot(); send(e); if (log) log->log("[cmd] snap"); g_need_repaint = true; continue; }

      { std::lock_guard<std::mutex> lk(mtx); model.msg = "unknown command (type 'help')"; }
      g_need_repaint = true;
      continue;
    }

    // Esc clears the line
    if (ch == 27){ ibuf.clear(); draw_prompt(); continue; }

    // ignore control keys we don't handle explicitly
    if (ch < 32 || ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_UP || ch == KEY_DOWN) {
      continue;
    }

    // append printable char
    ibuf.push_back(static_cast<char>(ch));
    draw_prompt();
  }

  stream->WritesDone();
  auto st = stream->Finish();
  endwin();
  if (log) log->log("[client] exit ok=", st.ok() ? 1 : 0);
  return st.ok() ? 0 : 1;
}
