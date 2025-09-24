/**
 * @file boardrenderer.hpp
 * @brief ASCII renderer for a Board::State snapshot.
 */

#ifndef BOARDRENDERER_HPP
#define BOARDRENDERER_HPP

#include "board.hpp"
#include <string>
#include <vector>
#include <iostream>

namespace BG {

/**
 * @class BoardRenderer
 * @brief Renders a Board::State to a fixed-width ASCII art image.
 *
 * Usage:
 * @code
 *   BG::Board b; BG::Board::State s; b.getState(s);
 *   BG::BoardRenderer r; r.render(s); r.print(std::cout);
 * @endcode
 */
class BoardRenderer
{
public:
    /// Construct a renderer with its default background board image.
    BoardRenderer();

    /**
     * @brief Render a snapshot onto the internal ASCII image buffer.
     * @param s A Board::State as filled by Board::getState().
     *
     * Call print() to flush the buffer to an output stream.
     */
    void render(const Board::State &s);

    /**
     * @brief Write the current ASCII image to an output stream.
     * @param os Target stream (e.g., std::cout).
     */
    void print(std::ostream &os) const;

private:
    /// A 2D ASCII image, one string per row.
    typedef std::vector<std::string> Image;

    /// Immutable background board art.
    static const Image BOARD_IMAGE;

    /// Draw buffer (initialized with BOARD_IMAGE).
    Image _image=BOARD_IMAGE;

    /**
     * @enum Dir
     * @brief Drawing direction for a point stack.
     *
     * UP   = draw towards decreasing y (upper half)
     * DOWN = draw towards increasing y (lower half)
     */
    enum class Dir{UP, DOWN};

    /// Convenience aliases for directions.
    const Dir UP=Dir::UP, DOWN=Dir::DOWN;

    /**
     * @struct Origin
     * @brief Starting coordinate and direction for drawing a stack.
     *
     * @var Origin::dir Direction to extend checkers (UP/DOWN).
     * @var Origin::x   Column index into the image.
     * @var Origin::y   Row index into the image.
     */
    struct Origin{ Dir dir; size_t x, y;};

    /**
     * @brief Draw a checker stack at a given origin.
     * @param s   Side that owns the stack (or NONE).
     * @param cnt Number of checkers on the stack.
     * @param o   Where and in which direction to draw.
     *
     * @details
     *  Always writes exactly five cells in a vertical column:
     *  - 0..5  checkers: draw first @p cnt cells with glyph, remaining cells as spaces.
     *  - 6..9  checkers: draw 4 glyphs + a single digit.
     *  - 10+   checkers: draw 3 glyphs + two digits (top-to-bottom digits depend on direction).
     *
     *  Writing spaces for unused cells ensures the background art is fully overwritten.
     */
    void renderPoint(Side s, unsigned cnt, const Origin &o);

    /// Characters used when drawing.
    const char
        WC='X', ///< White checker glyph
        BC='O', ///< Black checker glyph
        NC=' '; ///< Space used to overwrite background for unused cells

    /**
     * @brief Mapping of 24 board points to their ASCII origins.
     *
     * Indices 0..11 represent points 1..12 on the top half (draw UP),
     * indices 12..23 represent points 13..24 on the bottom half (draw DOWN).
     */
    const Origin
        PO[24]={
            /*  1 */ {UP, 26, 13},
            /*  2 */ {UP, 24, 13},
            /*  3 */ {UP, 22, 13},
            /*  4 */ {UP, 20, 13},
            /*  5 */ {UP, 18, 13},
            /*  6 */ {UP, 16, 13},
            /*  7 */ {UP, 11, 13},
            /*  8 */ {UP,  9, 13},
            /*  9 */ {UP,  7, 13},
            /* 10 */ {UP,  5, 13},
            /* 11 */ {UP,  3, 13},
            /* 12 */ {UP,  1, 13},
            /* 13 */ {DOWN,  1, 3},
            /* 14 */ {DOWN,  3, 3},
            /* 15 */ {DOWN,  5, 3},
            /* 16 */ {DOWN,  7, 3},
            /* 17 */ {DOWN,  9, 3},
            /* 18 */ {DOWN, 11, 3},
            /* 19 */ {DOWN, 16, 3},
            /* 20 */ {DOWN, 18, 3},
            /* 21 */ {DOWN, 20, 3},
            /* 22 */ {DOWN, 22, 3},
            /* 23 */ {DOWN, 24, 3},
            /* 24 */ {DOWN, 26, 3}
    };
        /// Origins for bars and bear-off ladders.
        // Row guide (0-based):
        //   0..1: top labels
        //   2   : top dashed line
        //   3..7: upper interior (5 rows)
        //   8   : center "============" line
        //   9..13: lower interior (5 rows)
        //   14  : bottom dashed line
        //   15..16: bottom labels

        // Bars at x=14, Off ladders at x=28. Keep each strictly inside its half.
        const Origin
        WHITEBAR = {UP,   14, 7},   // rows 7..3  (upper gutter, drawn upward)
        BLACKBAR = {DOWN, 14, 9},   // rows 9..13 (lower gutter, drawn downward)

        BLACKOFF = {DOWN, 28, 3},   // rows 3..7   (upper off ladder)
        WHITEOFF = {UP,   28, 13};  // rows 13..9  (lower off ladder)
};

} // namespace BG

#endif // BOARDRENDERER_HPP
