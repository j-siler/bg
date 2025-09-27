#include "ncurses_renderer.hpp"
#include <algorithm>
#include <cstdio>

namespace BG {

NcursesRenderer::NcursesRenderer(WINDOW* win) : _win(win) {
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_WHITE,  COLOR_WHITE,  -1);
        init_pair(CP_BLACK,  COLOR_CYAN,   -1);  // choose any contrasting color
        init_pair(CP_BORDER, COLOR_YELLOW, -1);
        init_pair(CP_TEXT,   COLOR_GREEN,  -1);
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
    if (cp) wattron(w, COLOR_PAIR(cp));
    mvwaddstr(w, y, x, s);   // UTF-8 via narrow API
    if (cp) wattroff(w, COLOR_PAIR(cp));
}

void NcursesRenderer::putch(WINDOW* w, int y, int x, char ch, short cp){
    char buf[2] = { ch, 0 };
    put(w, y, x, buf, cp);
}

void NcursesRenderer::drawChrome(){
    // Derived columns for the right gutter
    const int X_LEFT   = 0;
    const int X_INNER  = kWidth - 3; // T column (left edge of bear-off gutter)
    const int X_OFF    = kWidth - 2; // off-lane column (between inner and outer)
    const int X_RIGHT  = kWidth - 1; // new outer right border

    // Clear our rect
    for (int y=0; y<kHeight; ++y)
        for (int x=0; x<kWidth; ++x)
            put(_win, y, x, " ");

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

    // Center thick separator (home line) across main board only
    for (int x=X_LEFT+1; x<X_INNER; ++x) put(_win, 8, x, "═", CP_BORDER);
    put(_win, 8, X_LEFT,  "╞", CP_BORDER);
    put(_win, 8, X_INNER, "╪", CP_BORDER); // crossing at inner gutter line

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
