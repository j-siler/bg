/**
 * @file ncurses_renderer.hpp
 * @brief UTF-8 ncurses renderer for BG::Board::State (narrow-char API).
 */
#ifndef BG_NCURSES_RENDERER_HPP
#define BG_NCURSES_RENDERER_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <curses.h>   // macOS-friendly
#include "board.hpp"

namespace BG {

class NcursesRenderer {
public:
    explicit NcursesRenderer(WINDOW* win);

    void render(const Board::State& s);
    bool checkSize() const;

    // Make the frame symmetric: 29 cols total, right border at x=28.
    static constexpr int kHeight = 17; // rows: 0..16
    static constexpr int kWidth  = 29; // cols: 0..28

private:
    WINDOW* _win;

    enum class Dir { UP, DOWN };
    struct Origin { Dir dir; int x, y; };

    // UTF-8 glyphs (plain narrow strings)
    const char* WCHK = "○"; // white checker
    const char* BCHK = "●"; // black checker
    const char* EMPTY= "~"; // eraser

    // Color pairs
    static constexpr short CP_WHITE = 1;
    static constexpr short CP_BLACK = 2;
    static constexpr short CP_BORDER= 3;
    static constexpr short CP_TEXT  = 4;

    // Point origins (match your ASCII renderer)
    const Origin PO[24] = {
        /*  1 */ {Dir::UP,   25, 13},
        /*  2 */ {Dir::UP,   23, 13},
        /*  3 */ {Dir::UP,   21, 13},
        /*  4 */ {Dir::UP,   19, 13},
        /*  5 */ {Dir::UP,   17, 13},
        /*  6 */ {Dir::UP,   15, 13},
        /*  7 */ {Dir::UP,   11, 13},
        /*  8 */ {Dir::UP,    9, 13},
        /*  9 */ {Dir::UP,    7, 13},
        /* 10 */ {Dir::UP,    5, 13},
        /* 11 */ {Dir::UP,    3, 13},
        /* 12 */ {Dir::UP,    1, 13},
        /* 13 */ {Dir::DOWN,  1,  3},
        /* 14 */ {Dir::DOWN,  3,  3},
        /* 15 */ {Dir::DOWN,  5,  3},
        /* 16 */ {Dir::DOWN,  7,  3},
        /* 17 */ {Dir::DOWN,  9,  3},
        /* 18 */ {Dir::DOWN, 11,  3},
        /* 19 */ {Dir::DOWN, 15,  3},
        /* 20 */ {Dir::DOWN, 17,  3},
        /* 21 */ {Dir::DOWN, 19,  3},
        /* 22 */ {Dir::DOWN, 21,  3},
        /* 23 */ {Dir::DOWN, 23,  3},
        /* 24 */ {Dir::DOWN, 25,  3},
    };

    // Bars / off ladders — centered bar at x=14; off ladders moved to x=27
    const Origin WHITEBAR = {Dir::UP,   13,  7}; // rows 7..3  (upper gutter, drawn up)
    const Origin BLACKBAR = {Dir::DOWN, 13,  9}; // rows 9..13 (lower gutter, drawn down)
    const Origin BLACKOFF = {Dir::DOWN, 27,  3}; // rows 3..7   (upper off, x=27)
    const Origin WHITEOFF = {Dir::UP,   27, 13}; // rows 13..9  (lower off, x=27)

    // Utilities
    static bool inwin(WINDOW* w, int y, int x);
    static void put(WINDOW* w, int y, int x, const char* s, short color_pair=0);
    static void putch(WINDOW* w, int y, int x, char ch, short color_pair=0);

    void drawChrome(); // borders, separators, numbers
    void drawStack(Side side, unsigned cnt, const Origin& o);
};

} // namespace BG

#endif // BG_NCURSES_RENDERER_HPP
