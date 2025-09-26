# bg — Backgammon (C++ TUI + server)

## Build
```bash
# macOS
brew install cmake ncurses protobuf
cmake -S . -B build -DBG_ENABLE_PROTOBUF=ON
cmake --build build -j
./build/bin/client-tui
./build/bin/bg-server
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build -j
# Raspberry Pi OS
sudo apt-get update
sudo apt-get install -y cmake g++ libncurses-dev protobuf-compiler libprotobuf-dev
cmake -S . -B build -DBG_ENABLE_PROTOBUF=ON
cmake --build build -j
