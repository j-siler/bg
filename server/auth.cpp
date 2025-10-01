#include "auth.hpp"

namespace BG {

bool AuthManager::login(const std::string& user, const std::string& pass, User& out) {
  if (user.empty() || pass.empty()) return false; // stub rule
  std::lock_guard<std::mutex> lock(mu_);
  if (logged_.find(user) != logged_.end()) return false; // already logged in
  logged_.insert(user);
  out.id = user;
  out.name = user;
  return true;
}

void AuthManager::logout(const std::string& user) {
  std::lock_guard<std::mutex> lock(mu_);
  logged_.erase(user);
}

bool AuthManager::isLoggedIn(const std::string& user) const {
  std::lock_guard<std::mutex> lock(mu_);
  return logged_.find(user) != logged_.end();
}

} // namespace BG
