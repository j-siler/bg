# Backgammon Project — Current Status (Checkpoint)

## 1. Project Overview
This project implements a networked backgammon platform with:
- **Board logic** (rules, state, dice, cube).
- **Text-based renderers**:
  - ASCII renderer for testing.
  - Ncurses renderer (`bg_tui`) for interactive play.
- **Server (`bg_server`)**:  
  gRPC-based, handles matches, moves, snapshots.
- **Admin server (`bg_admin`)**:  
  gRPC-based, handles logins, match creation, monitoring.
- **Smoke test (`bg_smoke`)**:  
  Simple REPL for debugging.

**Current state:**
- Board and renderers: working and stable.
- Client TUI: connects, logs in, joins matches, renders board.
- Server: builds, runs, responds to gRPC. Needs session tokens and phase/proto alignment.
- Admin server: builds and runs; reflection removed; basic services exposed.
- Logging: flat-file, thread-safe, event-based.

The GitHub repo (`j-siler/bg`) is the **authoritative source of truth**.  
Commits serve as checkpoints; all future development assumes repo state as baseline.

---

## 2. File Inventory

### Root
- **board.hpp / board.cpp**  
  Core board model (points, bars, off, cube, dice).  
  **Status:** Stable.

- **boardrenderer.hpp / boardrenderer.cpp**  
  ASCII rendering of board state, used in snapshot tests.  
  **Status:** Stable.

- **ncurses_renderer.hpp / ncurses_renderer.cpp**  
  Ncurses-specific renderer (UTF-8 board, checkers, gutters, bear-off).  
  **Status:** Stable after cosmetic fixes (gutters, spacing, colors).

- **CMakeLists.txt (root)**  
  Top-level build configuration. Builds server and client subtrees.  
  **Status:** Stable.

- **.gitignore**  
  Standard ignores.  
  **Status:** Stable.

---

### server/
- **main.cc**  
  Main game server. Hosts gRPC service, streams board events.  
  **Status:** Builds, but proto-phase enums need alignment with `.proto`.

- **admin_server_main.cpp**  
  Entry point for admin server (`bg_admin`).  
  **Status:** Builds, reflection removed, minimal services exposed.

- **auth.hpp / auth.cpp**  
  Auth manager: login/logout stub. Tracks logged-in users.  
  **Status:** Works, but no token/session enforcement.

- **match.hpp / match.cpp**  
  Match registry: create, join, leave, seat allocation (White, Black, Observer).  
  **Status:** Functional; lacks strict checks and persistence.

- **logger.hpp / logger.cpp**  
  Thread-safe logger, writes ISO-UTC microsecond timestamps to file.  
  **Status:** Stable.

- **rpc_auth_match.hpp / rpc_auth_match.cpp**  
  gRPC service implementations for auth + match management.  
  **Status:** Builds; needs token enforcement for security.

- **smoke_main.cpp**  
  Simple REPL for manual smoke testing.  
  **Status:** Functional.

- **CMakeLists.txt (server)**  
  Handles proto generation and building `bg_server`, `bg_admin`, `bg_smoke`.  
  **Status:** Stable.

---

### client-tui/
- **main.cc**  
  Ncurses-based client. Connects to server, logs in, joins a match, renders board, handles input.  
  **Status:** Functional.

- **CMakeLists.txt (client-tui)**  
  Builds `bg_tui` with generated protos.  
  **Status:** Stable.

---

### proto/
- **bg.proto**  
  Game RPCs: login, match management, board state snapshots.  
  **Status:** Authoritative schema. Must align with server code.

- **admin.proto**  
  Admin RPCs for login/logout, match management.  
  **Status:** Builds; working with admin server.

---

## 3. Gaps and Known Issues
- **Auth:**  
  No session tokens; users can issue commands affecting others.  
  *Next step:* issue tokens at login, require them for subsequent commands.

- **Server phases:**  
  `main.cc` references enums (`OPENING_ROLL`, `CUBE_OFFERED`) that differ from `bg.proto`.  
  *Next step:* align enum values with proto.

- **Match limits:**  
  Only supports one global match in `bg_server`.  
  *Next step:* tie `MatchRegistry` into server-side match handling.

- **Logging:**  
  Currently flat file only.  
  *Next step:* consider structured logs or DB in future.

- **Security:**  
  No TLS or encryption.  
  *Next step:* plan for TLS, but not urgent for local dev.

---

## 4. Roadmap (Next Steps)
1. **Session tokens:** Add token issuance on login and require them on all subsequent calls.  
2. **Fix proto mismatch:** Align `main.cc` with `bg.proto` enums.  
3. **Match registry integration:** Replace single global match with multi-match registry.  
4. **Admin vs game services:** Keep admin control separate from player-facing RPCs.  
5. **Client refinement:** Expand `bg_tui` command handling, polish rendering.  
6. **Long-term:** TLS support, persistence, cube logic, full rule enforcement.

---

## 5. Notes for New Sessions
- Always sync to the GitHub repo (`j-siler/bg`) for ground truth.  
- Avoid applying patches piecemeal; prefer full drop-in files to keep state consistent.  
- Commits are checkpoints; use them to recover from drift.  
- Export session state if needed, but repo is authoritative.

---
