#include <iostream>
#include <sstream>
#include <vector>
#include <optional>
#include "auth.hpp"
#include "match.hpp"
#include "logger.hpp"

using std::string;

namespace {

std::vector<string> split(const string& line){
  std::istringstream is(line);
  std::vector<string> t;
  string w;
  while (is >> w) t.push_back(w);
  return t;
}

void help(){
  std::cout <<
    "Commands:\n"
    "  help\n"
    "  login <user> <pass>\n"
    "  logout\n"
    "  create <match> [length|c]\n"
    "  join <match> <white|black|observer>\n"
    "  leave <match>\n"
    "  quit / exit\n";
}

} // anon

int main(){
  using namespace BG;

  Logger logger{"logs/server-smoke.log"};
  AuthManager auth;
  MatchRegistry matches(logger);

  std::optional<User> current;
  std::optional<string> current_match;
  std::optional<SeatSide> current_role;

  std::cout << "bg_smoke — minimal REPL. Type 'help'.\n";

  string line;
  while (true){
    std::cout << "> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    auto args = split(line);
    if (args.empty()) continue;

    auto cmd = args[0];

    if (cmd == "help"){ help(); continue; }
    if (cmd == "quit" || cmd == "exit") { std::cout << "bye\n"; break; }

    if (cmd == "login"){
      if (args.size() < 3) { std::cout << "usage: login <user> <pass>\n"; continue; }
      if (current) { std::cout << "already logged in as '"<<current->id<<"'\n"; continue; }
      User u;
      if (auth.login(args[1], args[2], u)) {
        current = u;
        logger.info(EventType::UserLogin, u.id, "login ok (smoke)");
        std::cout << "logged in as '" << u.id << "'\n";
      } else {
        logger.error(args[1], "login failed (stub or already logged in)");
        std::cout << "login failed (bad creds or already logged in)\n";
      }
      continue;
    }

    if (cmd == "logout"){
      if (!current) { std::cout << "not logged in\n"; continue; }
      if (current_match) {
        std::cout << "leave '"<< *current_match <<"' first\n";
        continue;
      }
      auth.logout(current->id);
      logger.info(EventType::UserLogout, current->id, "logout (smoke)");
      std::cout << "logged out '" << current->id << "'\n";
      current.reset();
      continue;
    }

    if (cmd == "create"){
      if (!current) { std::cout << "login first\n"; continue; }
      if (args.size() < 2) { std::cout << "usage: create <match> [length|c]\n"; continue; }
      const string name = args[1];
      uint32_t len = 1;
      bool continuous = false;
      if (args.size() >= 3) {
        if (args[2] == "c" || args[2] == "C") {
          continuous = true; len = 0;
        } else {
          try { len = static_cast<uint32_t>(std::stoul(args[2])); }
          catch (...) { std::cout << "length must be integer or 'c'\n"; continue; }
        }
      }
      auto m = matches.create(name, len, continuous);
      if (m) {
        std::cout << "created match '" << name << "' "
                  << (m->cfg.continuous ? "continuous" : ("len=" + std::to_string(m->cfg.length_points))) << "\n";
      }
      continue;
    }

    if (cmd == "join"){
      if (!current) { std::cout << "login first\n"; continue; }
      if (current_match) { std::cout << "already in '"<< *current_match <<"'; leave first\n"; continue; }
      if (args.size() < 3) { std::cout << "usage: join <match> <white|black|observer>\n"; continue; }
      bool ok=false;
      auto side = BG::parseSeatSide(args[2], ok);
      if (!ok) { std::cout << "seat must be white|black|observer\n"; continue; }
      BG::PlayerRef pr{current->id, current->name};
      string err;
      auto m = matches.join(args[1], pr, side, err);
      if (m) {
        current_match = args[1];
        current_role  = side;
        std::cout << "joined '" << args[1] << "' as " << BG::seatSideName(side) << "\n";
      } else {
        std::cout << "join failed: " << err << "\n";
      }
      continue;
    }

    if (cmd == "leave"){
      if (!current) { std::cout << "login first\n"; continue; }
      if (args.size() < 2) { std::cout << "usage: leave <match>\n"; continue; }
      BG::MatchRegistry::LeaveResult res{};
      auto m = matches.leave(args[1], current->id, res);
      if (!m) { std::cout << "no such match\n"; continue; }

      switch (res){
        case BG::MatchRegistry::LeaveResult::NotMember:
          std::cout << "not a participant in '"<< args[1] <<"'\n"; break;
        case BG::MatchRegistry::LeaveResult::LeftObserver:
          std::cout << "left observer in '"<< args[1] <<"'\n"; break;
        case BG::MatchRegistry::LeaveResult::LeftSeat:
          std::cout << "left seat; match suspended '"<< args[1] <<"'\n"; break;
        case BG::MatchRegistry::LeaveResult::NotFound:
          std::cout << "no such match\n"; break;
      }

      if (current_match && *current_match == args[1]) {
        current_match.reset();
        current_role.reset();
      }
      continue;
    }

    std::cout << "unknown command; try 'help'\n";
  }

  return 0;
}
