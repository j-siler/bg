#include "logger.hpp"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>

namespace BG {

Logger::Logger(const std::string& path) {
  try {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
      std::error_code ec;
      std::filesystem::create_directories(p.parent_path(), ec);
      (void)ec;
    }
  } catch (...) {
    // best effort
  }
  out_.open(path, std::ios::out | std::ios::app);
}

void Logger::write(const LogEvent& e) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!out_) return;
  out_ << now_iso_utc() << " | " << type_name(e.type) << " | "
       << (e.who.empty() ? "-" : e.who) << " | " << e.msg << "\n";
  out_.flush();
}

std::string Logger::now_iso_utc() {
  using namespace std::chrono;
  const auto tp   = system_clock::now();
  const auto secs = time_point_cast<seconds>(tp);
  const auto t    = system_clock::to_time_t(secs);
  const auto us   = duration_cast<microseconds>(tp - secs).count();
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
  oss << '.' << std::setfill('0') << std::setw(6) << us << 'Z';
  return oss.str();
}

const char* Logger::type_name(EventType t) {
  switch (t) {
    case EventType::UserLogin:  return "UserLogin";
    case EventType::UserLogout: return "UserLogout";
    case EventType::Command:    return "Command";
    case EventType::CreateMatch:return "CreateMatch";
    case EventType::JoinMatch:  return "JoinMatch";
    case EventType::MatchEnd:   return "MatchEnd";
    case EventType::Move:       return "Move";
    case EventType::Error:      return "Error";
    case EventType::System:     return "System";
    default:                    return "Unknown";
  }
}

} // namespace BG
