#include "ncurses_renderer.hpp"
#include <algorithm>
#include <cstdio>

namespace BG {

// ---- Interior color policy (typed, file-local) ----
// Interior variants: same foregrounds as existing pairs, unified background (kBoardBG)
static constexpr short CP_FIELD      = 5;  // interior fill for "plain space"
static constexpr short CP_WHITE_INT  = 6;  // interior variant of CP_WHITE
static constexpr short CP_BLACK_INT  = 7;  // interior variant of CP_BLACK
static constexpr short CP_BORDER_INT = 8;  // interior variant of CP_BORDER
static constexpr short CP_TEXT_INT   = 9;  // interior variant of CP_TEXT

static constexpr short kBoardBG      = COLOR_BLACK; // change to COLOR_GREEN for "felt"

// Board interior: rows 3..13, cols 1..(kWidth-3)-1 (left edge to T column exclusive)
static inline bool in_board_interior(int y, int x, int kWidth){
    return (y >= 3 && y <= 13 && x >= 1 && x < (kWidth - 3));
}

NcursesRenderer::NcursesRenderer(WINDOW* win) : _win(win) {
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_WHITE,  COLOR_WHITE,  -1);
        init_pair(CP_BLACK,  COLOR_CYAN,   -1);  // choose any contrasting color
        init_pair(CP_BORDER, COLOR_YELLOW, -1);
        init_pair(CP_TEXT,   COLOR_GREEN,  -1);

        // Interior variants: same FG, unified BG across the playing field
        init_pair(CP_FIELD,      COLOR_WHITE,   kBoardBG);
        init_pair(CP_WHITE_INT,  COLOR_MAGENTA, kBoardBG);
        init_pair(CP_BLACK_INT,  COLOR_CYAN,    kBoardBG);
        init_pair(CP_BORDER_INT, COLOR_YELLOW,  kBoardBG);
        init_pair(CP_TEXT_INT,   COLOR_GREEN,   kBoardBG);
    }
}

bool NcursesRenderer::checkSize() const {
    int h=0,w=0; getmaxyx(_win,h,w);
    return (h >= kHeight) && (w >= kWidth);
}

static inline bool inwin_xy(WINDOW* w, int y, int x){
    int h=0, ww=0; getmaxyx(w,h,ww);
    return (y>=0 && y<h && x>=0 && x<ww);
}

bool NcursesRenderer::inwin(WINDOW* w, int y, int x){
    return inwin_xy(w,y,x);
}

void NcursesRenderer::put(WINDOW* w, int y, int x, const char* s, short cp){
    if (!inwin_xy(w,y,x)) return;

    // Inside the board interior, remap to interior variants so BG is unified.
    short eff = cp;
    if (in_board_interior(y, x, kWidth)) {
        if      (cp == CP_WHITE)  eff = CP_WHITE_INT;
        else if (cp == CP_BLACK)  eff = CP_BLACK_INT;
        else if (cp == CP_BORDER) eff = CP_BORDER_INT;
        else if (cp == CP_TEXT)   eff = CP_TEXT_INT;
        else if (cp == 0)         eff = CP_FIELD;     // plain spaces
        else                      eff = CP_FIELD;     // any other pair → safe default
    }

    if (eff) wattron(w, COLOR_PAIR(eff));
    mvwaddstr(w, y, x, s);   // UTF-8 via narrow API
    if (eff) wattroff(w, COLOR_PAIR(eff));
}

void NcursesRenderer::putch(WINDOW* w, int y, int x, char ch, short cp){
    char buf[2] = { ch, 0 };
    // Delegate to put() so the mapping logic stays centralized.
    put(w, y, x, buf, cp);
}

void NcursesRenderer::drawChrome(){
    // Derived columns for the right gutter
    const int X_LEFT   = 0;
    const int X_INNER  = kWidth - 3; // T column (left edge of bear-off gutter)
    const int X_OFF    = kWidth - 2; // off-lane column (between inner and outer)
    const int X_RIGHT  = kWidth - 1; // new outer right border

    // Clear our rect: interior gets unified BG; elsewhere keep terminal defaults
    for (int y=0; y<kHeight; ++y){
        for (int x=0; x<kWidth; ++x){
            const bool interior = in_board_interior(y, x, kWidth);
            put(_win, y, x, " ", interior ? CP_FIELD : 0);
        }
    }

    // ---- Numbers (aligned to point columns) ----
    // Top: points 13..24
    for (int p=13; p<=24; ++p){
        int x = PO[p-1].x;
        int tens = p / 10, ones = p % 10;
        if (tens) putch(_win, 0, x, char('0'+tens), CP_TEXT);
        putch(_win, 1, x, char('0'+ones), CP_TEXT);
    }
    // Bottom: points 12..1
    for (int p=12; p>=1; --p){
        int x = PO[p-1].x;
        if (p >= 10) putch(_win, 15, x, '1', CP_TEXT);
        putch(_win, 16, x, char('0'+(p%10)), CP_TEXT);
    }

    // ---- Outer border (top/bottom) with right gutter ----
    put(_win,  2, X_LEFT,  "┌", CP_BORDER);
    put(_win, 14, X_LEFT,  "└", CP_BORDER);

    // Main-board horizontal runs up to (but not including) the inner T column
    for (int x=X_LEFT+1; x<X_INNER; ++x){
        put(_win,  2, x, "─", CP_BORDER);
        put(_win, 14, x, "─", CP_BORDER);
    }

    // T-junctions at the inner gutter edge
    put(_win,  2, X_INNER, "┬", CP_BORDER);
    put(_win, 14, X_INNER, "┴", CP_BORDER);

    // One extra segment into the gutter
    put(_win,  2, X_OFF, "─", CP_BORDER);
    put(_win, 14, X_OFF, "─", CP_BORDER);

    // New outer right corners
    put(_win,  2, X_RIGHT, "┐", CP_BORDER);
    put(_win, 14, X_RIGHT, "┘", CP_BORDER);

    // Verticals: left border, inner gutter line, outer border
    for (int y=3; y<=13; ++y){
        put(_win, y, X_LEFT,  "│", CP_BORDER);
        put(_win, y, X_INNER, "│", CP_BORDER);
        put(_win, y, X_RIGHT, "│", CP_BORDER);
    }

// Center thick separator (home line): span full width to the right border
    for (int x = X_LEFT+1; x < X_INNER; ++x) put(_win, 8, x, "═", CP_BORDER);
    put(_win, 8, X_LEFT,  "╞", CP_BORDER);   // left border join
    put(_win, 8, X_INNER, "╪", CP_BORDER);    // inner gutter join
    
    for (int x = X_INNER+1; x < X_RIGHT; ++x) put(_win, 8, x, "═", CP_BORDER);
    put(_win, 8, X_RIGHT, "╡", CP_BORDER);    // right border join

    // ---- Center bar rails (already shifted left by 1) ----
    for (int y=3; y<=13; ++y){
        put(_win, y, 12, "│", CP_BORDER);
        put(_win, y, 14, "│", CP_BORDER);
    }
    // Intersections with top/bottom borders
    put(_win, 2, 12, "┬", CP_BORDER);
    put(_win, 2, 14, "┬", CP_BORDER);
    put(_win, 14, 12, "┴", CP_BORDER);
    put(_win, 14, 14, "┴", CP_BORDER);
    // Intersections with center thick line: use vertical single + horizontal double
    put(_win, 8, 12, "╪", CP_BORDER);
    put(_win, 8, 14, "╪", CP_BORDER);
}

void NcursesRenderer::drawStack(Side side, unsigned cnt, const Origin& o){
    const char* glyph = (side==BLACK ? BCHK : (side==WHITE ? WCHK : EMPTY));
    int y=o.y, x=o.x;
    auto step = [&](){ if (o.dir==Dir::UP) --y; else ++y; };
    auto putg = [&](){ put(_win, y, x, glyph, (side==WHITE?CP_WHITE:CP_BLACK)); step(); };
    auto pute = [&](){ put(_win, y, x, EMPTY, 0); step(); };

    if (cnt<=5){
        for (unsigned i=0;i<5;++i) (i<cnt? putg() : pute());
        return;
    }
    if (cnt<10){
        for (unsigned i=0;i<4;++i) putg();
        putch(_win, y, x, char('0'+cnt), (side==WHITE?CP_WHITE:CP_BLACK));
        return;
    }
    // 10..15 → 3 glyphs + two digits
    for (unsigned i=0;i<3;++i) putg();
    char tens = char('0' + (cnt/10));
    char ones = char('0' + (cnt%10));
    if (o.dir==Dir::UP){
        putch(_win, y, x, ones, (side==WHITE?CP_WHITE:CP_BLACK)); step();
        putch(_win, y, x, tens, (side==WHITE?CP_WHITE:CP_BLACK));
    } else {
        putch(_win, y, x, tens, (side==WHITE?CP_WHITE:CP_BLACK)); step();
        putch(_win, y, x, ones, (side==WHITE?CP_WHITE:CP_BLACK));
    }
}

void NcursesRenderer::render(const Board::State& s){
    if (!checkSize()){
        werase(_win);
        put(_win, 0, 0, "Window too small for board.", CP_TEXT);
        wrefresh(_win);
        return;
    }

    drawChrome();

    // Points 1..24
    for (int i=0;i<24;++i){
        const auto &pt = s.points[i];
        drawStack(pt.side, pt.count, PO[i]);
    }

    // Bars / off ladders
    // If any off-ladder origin lands on the right border after the shift,
    // nudge it left by 1 so we don't overwrite the border column.
    Origin wo = WHITEOFF;
    Origin bo = BLACKOFF;
    const int right_border_x = kWidth - 1; // outer right border
    if (wo.x >= right_border_x) wo.x = right_border_x - 1;
    if (bo.x >= right_border_x) bo.x = right_border_x - 1;

    drawStack(WHITE, s.whitebar, WHITEBAR);
    drawStack(BLACK, s.blackbar, BLACKBAR);
    drawStack(WHITE, s.whiteoff, wo);
    drawStack(BLACK, s.blackoff, bo);

    wrefresh(_win);
}

} // namespace BG

//The end.  Remove this line
