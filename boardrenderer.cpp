/**
 * @file boardrenderer.cpp
 * @brief Implementation of the ASCII board renderer.
 */

#include "boardrenderer.hpp"

namespace BG {

BoardRenderer::BoardRenderer() {}


/**
 * @brief Draw a single stack of checkers or a count at a given origin.
 *
 * See BoardRenderer::renderPoint declaration for details.
 */
void BoardRenderer::renderPoint(Side s, unsigned cnt, const Origin &o){
    char ch = s==BLACK ? BC : s==WHITE ? WC : NC;
    unsigned ypos=o.y, xpos=o.x;

    // Always overwrite exactly five cells in the column so background 'x'/'o' ghosts can't bleed through.
    if (cnt<=5){
        for(unsigned i=0; i<5; ++i){
            _image[ypos][xpos] = (i<cnt) ? ch : NC;
            if(o.dir==UP) --ypos; else ++ypos;
        }
        return;
    }

    if(cnt<10){
        // 4 glyphs + 1 digit
        for(unsigned i=0; i<4; ++i){
            _image[ypos][xpos] = ch;
            if(o.dir==UP) --ypos; else ++ypos;
        }
        _image[ypos][xpos] = char('0'+cnt);
        return;
    }

    // cnt >= 10 (max 15): 3 glyphs + two digits (tens then ones along draw direction)
    char tens('1'), ones(char('0'+(cnt%10)));
    for(unsigned i=0; i<3; ++i){
        _image[ypos][xpos] = ch;
        if(o.dir==UP) --ypos; else ++ypos;
    }
    _image[ypos][xpos] = (o.dir==UP ? ones : tens);
    if(o.dir==UP) --ypos; else ++ypos;
    _image[ypos][xpos] = (o.dir==UP ? tens : ones);
}

/**
 * @brief Print the current ASCII image.
 * @param os Output stream to receive the image.
 */
void BoardRenderer::print(std::ostream &os) const {
    for(const std::string &str: _image){
        os << str;
    }
}

/**
 * @brief Render a full board snapshot onto a fresh background image.
 * @param s Board state as produced by Board::getState().
 *
 * Draw order: points 1..24, then bars/off areas.
 */
void BoardRenderer::render(const Board::State &s) {
    _image = BOARD_IMAGE; // reset before drawing
    for (unsigned i=0; i<24; ++i) {
        const Origin &o=PO[i];
        renderPoint(s.points[i].side, s.points[i].count, o);
    }
    renderPoint(WHITE, s.whitebar, WHITEBAR);
    renderPoint(BLACK, s.blackbar, BLACKBAR);
    renderPoint(WHITE, s.whiteoff, WHITEOFF);
    renderPoint(BLACK, s.blackoff, BLACKOFF);
}

/**
 * @brief Static background art for the board (with guides/labels).
 *
 * The drawing coordinates in PO[] and the bar/off origins are calibrated
 * for this exact layout.
 */
const BoardRenderer::Image BoardRenderer::BOARD_IMAGE=
    {
        " 1 1 1 1 1 1    1 2 2 2 2 2   \n",
        " 3 4 5 6 7 8    9 0 1 2 3 4   \n",
        "------------------------------\n",
        "|x       o   | |o         x| |\n",
        "|x       o   | |o         x| |\n",
        "|x       o   | |o          | |\n",
        "|x           | |o          | |\n",
        "|x           | |o          | |\n",
        "|============|=|===========|=|\n",
        "|o           | |x          | |\n",
        "|o           | |x          | |\n",
        "|o       x   | |x          | |\n",
        "|o       x   | |x         o| |\n",
        "|o       x   | |x         o| |\n",
        "------------------------------\n",
        " 1 1 1 9 8 7    6 5 4 3 2 1   \n",
        " 2 1 0                        \n"
};

} // namespace BG
