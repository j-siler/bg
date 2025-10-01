#pragma once
#include <string>
#include <optional>
#include <unordered_set>
#include <mutex>

namespace BG {

/** Logged-in user identity carried in server context. */
struct User {
  std::string id;    ///< stable id (for now we use the username)
  std::string name;  ///< display name (same as id for now)
};

/**
 * @brief In-memory login registry with stub validation.
 *        Thread-safe. No persistence (yet).
 */
class AuthManager {
public:
  AuthManager() = default;

  /**
   * @brief Validate credentials (stub) and add user to logged-in set.
   *        For now: returns true iff both user & pass are non-empty AND user not already logged in.
   */
  bool login(const std::string& user, const std::string& pass, User& out);

  /** Remove user from logged-in set (idempotent). */
  void logout(const std::string& user);

  /** Is user currently logged in? */
  bool isLoggedIn(const std::string& user) const;

private:
  mutable std::mutex mu_;
  std::unordered_set<std::string> logged_;
};

} // namespace BG
