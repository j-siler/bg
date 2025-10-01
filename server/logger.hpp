#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>

namespace BG {

/** Types of events we log. Extend freely. */
enum class EventType : uint16_t {
  UserLogin = 1,
  UserLogout,
  Command,        ///< Any command received (parsed)
  CreateMatch,
  JoinMatch,
  MatchEnd,
  Move,           ///< Single checker movement within a turn
  Error,
  System
};

/** One log record (lightweight, human-readable for now). */
struct LogEvent {
  EventType type{};
  std::string who;   ///< actor id/name (may be empty for system)
  std::string msg;   ///< free-form text
};

/**
 * @brief Simple, thread-safe, append-only logger.
 *        Format (one line): ISO8601Z | TYPE | who | msg
 */
class Logger {
public:
  explicit Logger(const std::string& path);

  /** Append a record. Thread-safe. Never throws; best-effort. */
  void write(const LogEvent& e);

  /** Convenience helpers */
  void info(EventType t, std::string who, std::string msg) { write({t, std::move(who), std::move(msg)}); }
  void error(std::string who, std::string msg) { write({EventType::Error, std::move(who), std::move(msg)}); }

private:
  std::mutex mu_;
  std::ofstream out_;
  static std::string now_iso_utc();
  static const char* type_name(EventType t);
};

} // namespace BG
