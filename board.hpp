/**
 * @file board.hpp
 * @brief Core backgammon board model: sides, checkers, cube, phases, and turn control.
 */

#ifndef BOARD_HPP
#define BOARD_HPP

#include <set>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

namespace BG {

/**
 * @enum Side
 * @brief Player side indicator.
 *
 * Values:
 * - WHITE: White player (moves 24→1)
 * - BLACK: Black player (moves 1→24)
 * - NONE : No owner / empty / no holder
 */
enum class Side {WHITE=0, BLACK=1, NONE};

/// Convenience constants mirroring Side values (useful in initializers).
const Side WHITE(Side::WHITE), BLACK(Side::BLACK), NONE(Side::NONE);

/**
 * @struct Checker
 * @brief A single checker with side and current location.
 *
 * @var Checker::side
 *   Owner of the checker.
 * @var Checker::position
 *   Location encoding:
 *   - 1..24 : board points
 *   - 0     : on the bar
 *   - >24   : borne off (off the board)
 */
struct Checker {
    Side side;
    unsigned char position;
};

/**
 * @typedef Checkers
 * @brief A set of pointers to checkers stacked on a point.
 */
typedef std::set<Checker*> Checkers;

/**
 * @brief Game rule options that affect flow (esp. the opening).
 */
struct Rules {
    /**
     * @brief Policy when the *opening* roll is doubles.
     */
    enum class OpeningDoublePolicy { REROLL, AUTODOUBLE };

    /// Opening doubles behavior (default: reroll until not doubles).
    OpeningDoublePolicy openingDoublePolicy = OpeningDoublePolicy::REROLL;

    /**
     * @brief Max number of auto-doubles permitted at the opening (AUTODOUBLE policy).
     * 0 means unlimited.
     */
    unsigned maxOpeningAutoDoubles = 0;
};

/**
 * @brief Coarse game phase for turn control.
 */
enum class Phase {
    OpeningRoll,   ///< Before first move: one die each; doubles handled by @ref Rules.
    AwaitingRoll,  ///< A player must roll (or set) two dice to begin their turn.
    Moving,        ///< Dice are set; zero or more per-die steps may be applied/undone.
    CubeOffered    ///< A cube offer is pending; opponent must take or drop.
};

/**
 * @struct Board::State
 * @brief Lightweight, POD-style snapshot of the board for rendering/UI.
 */
class Board
{
public:
    /**
     * @brief Construct the board in standard starting position.
     */
    Board();

    struct State {
        struct Point {
            Side side=NONE; unsigned count=0;
        } points[24];

        /// Current cube value (1, 2, 4, ...).
        unsigned cube=1;

        /// Checkers on bars and borne off, by side.
        unsigned whitebar=0, blackbar=0, whiteoff=0, blackoff=0;
    };

    /**
     * @brief Result info after a resignation (cube drop) or future game-end conditions.
     */
    struct GameResult {
        bool over=false;     ///< True if the game has ended.
        Side winner=NONE;    ///< Winner side when over==true.
        unsigned finalCube=1;///< Cube value that applies to the result.
        bool resigned=false; ///< True if ended via dropCube() (resignation).
    };

    /**
     * @brief Fill a State with the current board snapshot.
     * @param[out] s Destination snapshot.
     */
    void getState(State &s) const;

    /**
     * @brief Human-readable summary (occupied points only).
     */
    std::string to_string() const;

    // ===== Phases / lifecycle =================================================

    /**
     * @brief Reset to the initial position, center cube, clear turn state and result.
     * @param rules Rule options (opening doubles behavior, caps).
     *
     * After this call, phase()==OpeningRoll. No dice are set yet.
     */
    void startGame(const Rules& rules = Rules());

    /// Current coarse phase of play.
    Phase phase() const { return _phase; }

    /// Get whose turn it is right now (undefined in OpeningRoll until resolved).
    Side sideToMove() const { return _actor; }

    /// True if the game has ended (e.g., by cube drop).
    bool gameOver() const { return _result.over; }

    /// Final result descriptor (valid when gameOver()==true).
    GameResult result() const { return _result; }

    // ===== Opening ============================================================

    /**
     * @brief Perform the opening roll internally (one die per side).
     * @return {whiteDie, blackDie} for the last throw attempted.
     * @throws std::logic_error if phase()!=OpeningRoll.
     */
    std::pair<int,int> rollOpening();

    /**
     * @brief Supply an external opening throw (e.g., from UI or tests).
     * @return true if resolved (non-doubles), false if doubles processed and roll again needed.
     * @throws std::invalid_argument on out-of-range values.
     * @throws std::logic_error if phase()!=OpeningRoll.
     */
    bool setOpeningDice(int whiteDie, int blackDie);

    /// Number of opening auto-doubles applied so far (AUTODOUBLE policy only).
    unsigned openingAutoDoubles() const { return _openingAutoDoubles; }

    // ===== Turn & dice ========================================================

    /// True if a dice roll is required next (i.e., before any steps can be applied).
    bool needsRoll() const;

    /**
     * @brief Roll two dice internally and prepare a new turn (handles doubles).
     * @return {d1,d2} as rolled (doubles expanded internally to four pips).
     * @throws std::logic_error if phase()!=AwaitingRoll.
     */
    std::pair<int,int> rollDice();

    /**
     * @brief Provide an external roll (e.g., from UI or a deterministic test).
     * @throws std::invalid_argument on out-of-range values.
     * @throws std::logic_error if phase()!=AwaitingRoll.
     */
    void setDice(int d1, int d2);

    /// Remaining pip values (one element per still-unused die).
    std::vector<int> diceRemaining() const { return _diceLeft; }

    /**
     * @brief Attempt one per-die step from a location using one remaining die.
     * @param from  Board point 1..24; use 0 to enter from the bar.
     * @param pip   Pip value to consume (must be present in diceRemaining()).
     * @return true if applied; false if illegal (see lastError()).
     *
     * @note Enforces per-step legality; global obligations (max dice usage, higher die)
     *       are validated at commitTurn().
     */
    bool applyStep(int from, int pip);

    /**
     * @brief Undo the last successfully applied step of this turn.
     * @return true if something was undone; false if no steps exist.
     */
    bool undoStep();

    /**
     * @brief Finalize the turn: validate global-move obligations and switch side.
     * @return true if the partial sequence is a valid completion of the roll; false otherwise.
     *
     * If no legal move existed at all, an empty commit passes the turn.
     */
    bool commitTurn();

    /// True if any legal step exists with the current dice and board.
    bool hasAnyLegalStep() const;

    /// Return a machine-friendly explanation of the last rule failure.
    std::string lastError() const { return _lastErr; }

    // ===== Convenience queries ===============================================

    unsigned countAt(Side s, int point) const;
    unsigned countBar(Side s) const;
    unsigned countOff(Side s) const;

    // ===== Doubling cube ======================================================

    /// Current cube value (1,2,4,...).
    unsigned cubeValue() const { return _cubeval; }

    /// Current cube holder (NONE if centered).
    Side cubeHolder() const { return _cubeholder; }

    /**
     * @brief Offer the cube (only by sideToMove() and only before rolling).
     * @return true if the offer is now pending; false if not allowed (see lastError()).
     *
     * Preconditions:
     *  - phase()==AwaitingRoll
     *  - cubeHolder()==NONE or cubeHolder()==sideToMove()
     */
    bool offerCube();

    /**
     * @brief Opponent accepts a pending cube offer; doubles cube value and transfers holder.
     * @return true on success; false if no pending offer (see lastError()).
     *
     * Postconditions:
     *  - cubeValue() *= 2
     *  - cubeHolder() becomes the taker (opponent of the offerer)
     *  - phase()==AwaitingRoll; offerer remains sideToMove()
     */
    bool takeCube();

    /**
     * @brief Opponent declines a pending cube offer (resigns the game).
     * @return true on success; false if no pending offer (see lastError()).
     *
     * Postconditions:
     *  - gameOver()==true
     *  - result().winner == offerer, result().finalCube == cubeValue()
     */
    bool dropCube();

private:
    // ===== Static initial layout helpers =====================================
    static const unsigned char INIT_BLACK[15];
    static const unsigned char INIT_WHITE[15];

    // ===== Core board containers =============================================
    Checker _checkers[2][15] =
        {
        {
         {BLACK, 1}, {BLACK, 1},
         {BLACK, 12}, {BLACK, 12}, {BLACK, 12}, {BLACK, 12}, {BLACK, 12},
         {BLACK, 17}, {BLACK, 17}, {BLACK, 17},
         {BLACK, 19}, {BLACK, 19}, {BLACK, 19}, {BLACK, 19}, {BLACK, 19}
        },
        {
         {WHITE, 24}, {WHITE, 24},
         {WHITE, 13}, {WHITE, 13}, {WHITE, 13}, {WHITE, 13}, {WHITE, 13},
         {WHITE, 8}, {WHITE, 8}, {WHITE, 8},
         {WHITE, 6}, {WHITE, 6}, {WHITE, 6}, {WHITE, 6}, {WHITE, 6}
        }
};

    Checkers _points[24];

    unsigned _whitebar=0, _blackbar=0,_whiteoff=0, _blackoff=0;

    unsigned _cubeval=1;
    Side _cubeholder=NONE;

    // ===== Turn/cube state ====================================================
    Rules _rules{};
    Phase _phase = Phase::OpeningRoll;
    Side  _actor = NONE;               ///< side to move; NONE during OpeningRoll
    std::vector<int> _diceLeft;        ///< remaining pips for current actor
    unsigned _openingAutoDoubles = 0;  ///< count of opening auto-doubles
    std::string _lastErr;              ///< last rule error/message

    // Cube offer / result
    Side _cubePendingFrom = NONE;      ///< Offerer when phase==CubeOffered
    GameResult _result{};              ///< Final result if game ended via drop

    // Per-turn bookkeeping
    struct Step {
        int from=0, to=0, pip=0;
        bool hit=false, entered=false, borneOff=false;
        Checker* moved=nullptr;
        Checker* hitChecker=nullptr;
    };
    std::vector<Step> _steps;          ///< applied steps this turn

    // Snapshot of the board at turn start (for commit validation)
    struct SimpleState {
        unsigned w[25]{}, b[25]{}; // 1..24 used
        unsigned wbar=0, bbar=0, woff=0, boff=0;
    };
    SimpleState _turnStart{};
    std::vector<int> _turnStartDice{};
    Side _turnStartActor = NONE;

    // ===== Internal helpers ===================================================
    void rebuildPointsFromCheckerPositions();
    static Side opponent(Side s) { return s==WHITE?BLACK : s==BLACK?WHITE : NONE; }
    static int sideIndex(Side s){ return s==WHITE?1 : s==BLACK?0 : -1; }

    // movement math
    static inline int destPoint(Side s, int from, int pip){
        return s==WHITE ? (from==0 ? 25-pip : from-pip)
                        : (from==0 ? pip : from+pip);
    }
    static inline bool inBoard(int p){ return p>=1 && p<=24; }
    static inline bool isHome(Side s, int p){ return s==WHITE ? (p>=1 && p<=6) : (p>=19 && p<=24); }

    // board queries
    unsigned pointCount(int p) const;                 // total checkers on point
    Side     pointSide(int p) const;                  // NONE if empty else owner
    unsigned sidePointCount(Side s, int p) const;     // count for s on p (0 or size)

    // mutations
    Checker* popFromPoint(int p);                     // remove one checker from p
    void     pushToPoint(int p, Checker* c);          // add checker to p
    Checker* popFromBar(Side s);                      // remove from bar
    void     pushToBar(Side s, Checker* c);           // add to bar
    void     pushOff(Side s, Checker* c);             // bear off

    void snapshotTurnStart();                         // fill _turnStart/actor/dice

    // validation helpers for bearing off
    bool allInHome(Side s) const;
    bool anyFurtherFromHome(Side s, int from) const;

    // commit-time search
    static unsigned dfsMax(const SimpleState& st, Side actor, const std::vector<int>& dice, size_t usedMask);
    static unsigned maxPlayableDice(const SimpleState& st, Side actor, const std::vector<int>& dice);
};

} // namespace BG

#endif // BOARD_HPP
