#include "match.hpp"
#include <algorithm>

namespace BG {

static std::string norm(const std::string& s){
  std::string t = s;
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c){ return std::tolower(c); });
  return t;
}

std::shared_ptr<Match> MatchRegistry::ensure_(const std::string& name, uint32_t length_points, bool continuous){
  auto it = by_name_.find(name);
  if (it != by_name_.end()) return it->second;
  auto m = std::make_shared<Match>();
  m->id   = name;
  m->name = name;
  m->cfg.length_points = length_points;
  m->cfg.continuous    = continuous;
  by_name_.emplace(name, m);
  return m;
}

std::shared_ptr<Match> MatchRegistry::get_unlocked_(const std::string& name) const {
  auto it = by_name_.find(name);
  return it == by_name_.end() ? nullptr : it->second;
}

std::shared_ptr<Match> MatchRegistry::create(std::string name, uint32_t length_points, bool continuous){
  std::lock_guard<std::mutex> lk(mu_);
  if (length_points == 0) continuous = true; // canonicalize
  auto m = ensure_(name, length_points, continuous);
  log_.info(EventType::CreateMatch, "-", "create: " + name +
            (m->cfg.continuous ? " continuous" : (" len=" + std::to_string(m->cfg.length_points))));
  m->broadcast("Match created", log_);
  return m;
}

std::shared_ptr<Match> MatchRegistry::get(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  return get_unlocked_(name);
}

std::shared_ptr<Match> MatchRegistry::join(const std::string& name,
                                           const PlayerRef& player,
                                           SeatSide side,
                                           std::string& err_out)
{
  std::lock_guard<std::mutex> lk(mu_);
  auto m = get_unlocked_(name);
  if (!m) { err_out = "match not found: " + name; return nullptr; }

  if (m->hasPlayer(player.id)) { err_out = "already joined"; return nullptr; }

  switch (side) {
    case SeatSide::White:
      if (m->seats.white) { err_out = "white seat taken"; return nullptr; }
      m->seats.white = player; break;
    case SeatSide::Black:
      if (m->seats.black) { err_out = "black seat taken"; return nullptr; }
      m->seats.black = player; break;
    case SeatSide::Observer:
      m->seats.observers.insert(player.id); break;
  }

  log_.info(EventType::JoinMatch, player.id, "join " + name + " as " + seatSideName(side));
  m->broadcast(player.name + " joined as " + seatSideName(side), log_);
  return m;
}

std::shared_ptr<Match> MatchRegistry::leave(const std::string& name,
                                            const std::string& user_id,
                                            LeaveResult& result_out)
{
  std::lock_guard<std::mutex> lk(mu_);
  auto m = get_unlocked_(name);
  if (!m) { result_out = LeaveResult::NotFound; return nullptr; }

  bool was_white = (m->seats.white && m->seats.white->id == user_id);
  bool was_black = (m->seats.black && m->seats.black->id == user_id);
  bool was_obs   = (m->seats.observers.erase(user_id) > 0);

  if (!was_white && !was_black && !was_obs) {
    result_out = LeaveResult::NotMember;
    return m;
  }

  if (was_white) m->seats.white.reset();
  if (was_black) m->seats.black.reset();

  if (was_white || was_black) {
    m->game.suspended = true;
    result_out = LeaveResult::LeftSeat;
    log_.info(EventType::MatchEnd, user_id, "left seat; suspending match " + name);
    m->broadcast("Player left seat; match suspended", log_);
  } else {
    result_out = LeaveResult::LeftObserver;
    log_.info(EventType::Command, user_id, "left observer in " + name);
  }
  return m;
}

SeatSide parseSeatSide(const std::string& s, bool& ok){
  auto t = norm(s);
  ok = true;
  if (t == "white" || t == "w") return SeatSide::White;
  if (t == "black" || t == "b") return SeatSide::Black;
  if (t == "observer" || t == "obs" || t == "o") return SeatSide::Observer;
  ok = false; return SeatSide::Observer;
}

const char* seatSideName(SeatSide s){
  switch (s){
    case SeatSide::White: return "white";
    case SeatSide::Black: return "black";
    case SeatSide::Observer: return "observer";
  }
  return "observer";
}

} // namespace BG
