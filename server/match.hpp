#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include "logger.hpp"  // BG::Logger, BG::EventType

namespace BG {

/** Side selection for joining a match. */
enum class SeatSide : uint8_t { White = 0, Black = 1, Observer = 2 };

struct PlayerRef {
  std::string id;   ///< stable id (e.g., username)
  std::string name; ///< display (can be same as id)
};

struct MatchSeat {
  std::optional<PlayerRef> white;
  std::optional<PlayerRef> black;
  std::unordered_set<std::string> observers; // by user id
};

struct MatchConfig {
  uint32_t length_points = 1; // 0 = continuous (money)
  bool     continuous = false;
};

/** Minimal game placeholder; real Board::State lives elsewhere. */
struct GameStub {
  bool suspended = false;      ///< true if a seat left
};

struct Match {
  std::string id;              ///< key (we use name as id for now)
  std::string name;
  MatchConfig cfg{};
  MatchSeat seats{};
  GameStub game{};

  void broadcast(const std::string& notice, Logger& log) {
    log.info(EventType::System, name, notice);
  }

  bool hasPlayer(const std::string& user_id) const {
    if (seats.white && seats.white->id == user_id) return true;
    if (seats.black && seats.black->id == user_id) return true;
    return seats.observers.count(user_id) > 0;
  }
};

class MatchRegistry {
public:
  explicit MatchRegistry(Logger& logger) : log_(logger) {}

  /** Create or return existing match with same name. */
  std::shared_ptr<Match> create(std::string name, uint32_t length_points, bool continuous);

  /** Lookup by name (exact). */
  std::shared_ptr<Match> get(const std::string& name) const;

  /** Join as seat or observer. Returns match on success, null on failure. */
  std::shared_ptr<Match> join(const std::string& name,
                              const PlayerRef& player,
                              SeatSide side,
                              std::string& err_out);

  enum class LeaveResult { NotFound, NotMember, LeftObserver, LeftSeat };

  /** Leave (drop from seat/observer). */
  std::shared_ptr<Match> leave(const std::string& name,
                               const std::string& user_id,
                               LeaveResult& result_out);

private:
  std::shared_ptr<Match> ensure_(const std::string& name, uint32_t length_points, bool continuous);
  std::shared_ptr<Match> get_unlocked_(const std::string& name) const;

  mutable std::mutex mu_;
  std::unordered_map<std::string, std::shared_ptr<Match>> by_name_;
  Logger& log_;
};

/// String helpers for the CLI layer (optional)
SeatSide parseSeatSide(const std::string& s, bool& ok);
const char* seatSideName(SeatSide s);

} // namespace BG
