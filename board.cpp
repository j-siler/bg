/**
 * @file board.cpp
 * @brief Board implementation: initialization, serialization, opening/turn control, legality, undo, commit, and cube.
 */

#include "board.hpp"
#include <sstream>
#include <algorithm>
#include <random>

namespace BG {

// ===== Static initial layouts (point numbers) ================================
const unsigned char Board::INIT_BLACK[15] = {
    1,1, 12,12,12,12,12, 17,17,17, 19,19,19,19,19
};
const unsigned char Board::INIT_WHITE[15] = {
    24,24, 13,13,13,13,13, 8,8,8, 6,6,6,6,6
};

// ===== Construction / baseline snapshot =====================================

Board::Board() {
    rebuildPointsFromCheckerPositions();
}

void Board::rebuildPointsFromCheckerPositions() {
    // clear all stacks/counters
    for (auto &pt : _points) pt.clear();
    _whitebar=_blackbar=_whiteoff=_blackoff=0;

    // Whites
    for(unsigned i=0; i<15; i++) {
        Checker* c(&_checkers[1][i]);
        auto p(c->position);
        if(p>=1 && p<=24) {
            _points[p-1].insert(c);
        }
        else if(p==0){
            ++_whitebar;
        }
        else{
            ++_whiteoff;
        }
    }

    // Blacks
    for(unsigned i=0; i<15; i++) {
        Checker* c(&_checkers[0][i]);
        auto p(c->position);
        if(p>=1 && p<=24) {
            _points[p-1].insert(c);
        }
        else if(p==0){
            ++_blackbar;
        }
        else{
            ++_blackoff;
        }
    }
}

std::string Board::to_string() const
{
    std::ostringstream os;
    os <<"Board\n" "Point ";
    for (unsigned i=0; i<24; i++) {
        const Checkers& checkers(_points[i]);
        if (checkers.empty())
            continue;
        os << i+1;
        auto c(*checkers.begin()); {
            os << " " << (c->side==BLACK ? "B" : "W") << checkers.size() << " ";
        }
        if (i==11)
            os << "\nPoint ";
    }
    os << '\n';
    return os.str();
}

void Board::getState(State &s) const
{
    for (unsigned i=0; i<24; i++) {
        const Checkers &c(_points[i]);
        s.points[i].count=c.size();
        s.points[i].side = c.empty() ? NONE : (*c.begin())->side;
    }
    s.whitebar=_whitebar;
    s.blackbar=_blackbar;
    s.whiteoff=_whiteoff;
    s.blackoff=_blackoff;
    s.cube=_cubeval;
}

// ===== Lifecycle / phases ====================================================

void Board::startGame(const Rules& rules) {
    for (unsigned i=0;i<15;i++) {
        _checkers[0][i].side=BLACK; _checkers[0][i].position=INIT_BLACK[i];
        _checkers[1][i].side=WHITE; _checkers[1][i].position=INIT_WHITE[i];
    }
    rebuildPointsFromCheckerPositions();

    _cubeval=1; _cubeholder=NONE; _cubePendingFrom=NONE;
    _rules=rules;
    _phase=Phase::OpeningRoll;
    _actor=NONE;
    _diceLeft.clear();
    _openingAutoDoubles=0;
    _lastErr.clear();
    _steps.clear();
    _turnStart = SimpleState{};
    _turnStartDice.clear();
    _turnStartActor = NONE;
    _result = GameResult{}; // clear any previous game result
}

static inline int roll_die(std::mt19937 &rng){
    static std::uniform_int_distribution<int> d(1,6);
    return d(rng);
}

void Board::snapshotTurnStart(){
    SimpleState s{};
    for(int p=1;p<=24;p++){
        const Checkers& c=_points[p-1];
        if(c.empty()) continue;
        Side owner=(*c.begin())->side;
        if(owner==WHITE) s.w[p]=c.size(); else s.b[p]=c.size();
    }
    s.wbar=_whitebar; s.bbar=_blackbar; s.woff=_whiteoff; s.boff=_blackoff;
    _turnStart = s;
    _turnStartDice = _diceLeft;
    _turnStartActor = _actor;
}

std::pair<int,int> Board::rollOpening() {
    if (_phase!=Phase::OpeningRoll) throw std::logic_error("rollOpening: not in OpeningRoll phase");
    std::random_device rd;
    std::mt19937 rng(rd());

    while (true) {
        int w = roll_die(rng);
        int b = roll_die(rng);
        if (w!=b) {
            if (w>b) { _actor = WHITE; _diceLeft = {w,b}; }
            else     { _actor = BLACK; _diceLeft = {b,w}; }
            _phase = Phase::Moving;
            _lastErr.clear();
            _steps.clear();
            snapshotTurnStart();
            return {w,b};
        }
        // doubles on opening
        if (_rules.openingDoublePolicy==Rules::OpeningDoublePolicy::REROLL) {
            continue;
        } else {
            if (_rules.maxOpeningAutoDoubles==0 || _openingAutoDoubles<_rules.maxOpeningAutoDoubles) {
                _cubeval <<= 1; ++_openingAutoDoubles;
            }
            // roll again
        }
    }
}

bool Board::setOpeningDice(int whiteDie, int blackDie) {
    if (_phase!=Phase::OpeningRoll) throw std::logic_error("setOpeningDice: not in OpeningRoll phase");
    if (whiteDie<1||whiteDie>6||blackDie<1||blackDie>6)
        throw std::invalid_argument("setOpeningDice: dice out of range");
    if (whiteDie!=blackDie) {
        if (whiteDie>blackDie) { _actor=WHITE; _diceLeft={whiteDie, blackDie}; }
        else                   { _actor=BLACK; _diceLeft={blackDie, whiteDie}; }
        _phase=Phase::Moving; _lastErr.clear(); _steps.clear();
        snapshotTurnStart();
        return true;
    }
    if (_rules.openingDoublePolicy==Rules::OpeningDoublePolicy::AUTODOUBLE) {
        if (_rules.maxOpeningAutoDoubles==0 || _openingAutoDoubles<_rules.maxOpeningAutoDoubles) {
            _cubeval <<= 1; ++_openingAutoDoubles;
        }
    }
    return false;
}

// ===== Turn & dice ===========================================================

bool Board::needsRoll() const {
    return _phase==Phase::AwaitingRoll && !_result.over;
}

std::pair<int,int> Board::rollDice() {
    if (_result.over){ throw std::logic_error("rollDice: game over"); }
    if (_phase!=Phase::AwaitingRoll) throw std::logic_error("rollDice: not in AwaitingRoll phase");
    std::random_device rd;
    std::mt19937 rng(rd());
    int d1 = roll_die(rng), d2 = roll_die(rng);
    _diceLeft.clear();
    if (d1==d2) _diceLeft = {d1,d1,d1,d1};
    else        _diceLeft = {d1,d2};
    _phase = Phase::Moving;
    _lastErr.clear();
    _steps.clear();
    snapshotTurnStart();
    return {d1,d2};
}

void Board::setDice(int d1, int d2) {
    if (_result.over){ throw std::logic_error("setDice: game over"); }
    if (_phase!=Phase::AwaitingRoll) throw std::logic_error("setDice: not in AwaitingRoll phase");
    if (d1<1||d1>6||d2<1||d2>6) throw std::invalid_argument("setDice: dice out of range");
    _diceLeft.clear();
    if (d1==d2) _diceLeft = {d1,d1,d1,d1};
    else        _diceLeft = {d1,d2};
    _phase = Phase::Moving;
    _lastErr.clear();
    _steps.clear();
    snapshotTurnStart();
}

// ===== low-level board queries/mutations ====================================

unsigned Board::pointCount(int p) const {
    if (!inBoard(p)) return 0U;
    return (unsigned)_points[p-1].size();
}

Side Board::pointSide(int p) const {
    if (!inBoard(p)) return NONE;
    const Checkers& c = _points[p-1];
    if (c.empty()) return NONE;
    return (*c.begin())->side;
}

unsigned Board::sidePointCount(Side s, int p) const {
    if (!inBoard(p)) return 0U;
    const Checkers& c = _points[p-1];
    if (c.empty()) return 0U;
    return (*c.begin())->side==s ? (unsigned)c.size() : 0U;
}

Checker* Board::popFromPoint(int p){
    Checkers& c = _points[p-1];
    auto it = c.begin();
    Checker* ck = *it;
    c.erase(it);
    return ck;
}

void Board::pushToPoint(int p, Checker* c){
    c->position = (unsigned char)p;
    _points[p-1].insert(c);
}

Checker* Board::popFromBar(Side s){
    Checker* found = nullptr;
    int idx = sideIndex(s);
    if (idx<0) return nullptr;
    for(int i=0;i<15;i++){
        Checker* c = &_checkers[idx][i];
        if (c->position==0){ found=c; break; }
    }
    if (!found) return nullptr;
    if (s==WHITE) { if (_whitebar==0) return nullptr; --_whitebar; }
    else          { if (_blackbar==0) return nullptr; --_blackbar; }
    return found;
}

void Board::pushToBar(Side s, Checker* c){
    c->position = 0;
    if (s==WHITE) ++_whitebar; else ++_blackbar;
}

void Board::pushOff(Side s, Checker* c){
    c->position = 25; // any >24
    if (s==WHITE) ++_whiteoff; else ++_blackoff;
}

// ===== legality helpers ======================================================

bool Board::allInHome(Side s) const {
    if (s==WHITE) {
        if (_whitebar>0) return false;
        unsigned inHome=0;
        for(int p=1;p<=6;p++){ inHome += sidePointCount(WHITE,p); }
        return (inHome + _whiteoff)==15;
    } else if (s==BLACK) {
        if (_blackbar>0) return false;
        unsigned inHome=0;
        for(int p=19;p<=24;p++){ inHome += sidePointCount(BLACK,p); }
        return (inHome + _blackoff)==15;
    }
    return false;
}

bool Board::anyFurtherFromHome(Side s, int from) const {
    if (s==WHITE){
        for(int p=from+1;p<=24;p++) if (sidePointCount(WHITE,p)>0) return true;
        return false;
    } else {
        for(int p=1;p<from;p++) if (sidePointCount(BLACK,p)>0) return true;
        return false;
    }
}

// ===== apply/undo/commit =====================================================

bool Board::applyStep(int from, int pip) {
    if (_result.over){ _lastErr="applyStep: game over"; return false; }
    if (_phase!=Phase::Moving) { _lastErr="applyStep: not in Moving phase"; return false; }
    if (_diceLeft.empty())     { _lastErr="applyStep: no dice remaining";   return false; }

    auto it = std::find(_diceLeft.begin(), _diceLeft.end(), pip);
    if (it==_diceLeft.end()) { _lastErr="applyStep: pip not available"; return false; }

    if ((_actor==WHITE && _whitebar>0) || (_actor==BLACK && _blackbar>0)){
        if (from!=0){ _lastErr="applyStep: must enter from bar first"; return false; }
    }

    if (from==0){
        if ((_actor==WHITE && _whitebar==0) || (_actor==BLACK && _blackbar==0)){
            _lastErr="applyStep: bar empty"; return false; }
    } else {
        if (!inBoard(from)){ _lastErr="applyStep: invalid source point"; return false; }
        if (sidePointCount(_actor, from)==0){ _lastErr="applyStep: no checker at source"; return false; }
    }

    int to = destPoint(_actor, from, pip);
    bool borne=false, hit=false;

    if (inBoard(to)){
        Side dstSide = pointSide(to);
        unsigned dstCnt = pointCount(to);
        if (dstSide!=NONE && dstSide!=_actor && dstCnt>=2){
            _lastErr="applyStep: destination blocked"; return false;
        }
    } else {
        if (!allInHome(_actor)){ _lastErr="applyStep: cannot bear off, not all checkers in home"; return false; }
        if (_actor==WHITE){
            if (from!=pip && anyFurtherFromHome(WHITE, from)){
                _lastErr="applyStep: must use exact roll or bear off highest checker"; return false;
            }
        } else {
            if (from != 25-pip && anyFurtherFromHome(BLACK, from)){
                _lastErr="applyStep: must use exact roll or bear off highest checker"; return false;
            }
        }
        borne=true;
    }

    Step st{}; st.from=from; st.to=to; st.pip=pip; st.borneOff=borne; st.entered=(from==0);

    Checker* mover=nullptr;
    if (from==0){
        mover = popFromBar(_actor);
        if (!mover){ _lastErr="applyStep: internal bar underflow"; return false; }
    } else {
        mover = popFromPoint(from);
    }

    Checker* hitCk=nullptr;
    if (!borne && inBoard(to)){
        Side dstSide = pointSide(to);
        unsigned dstCnt = pointCount(to);
        if (dstSide!=NONE && dstSide!=_actor && dstCnt==1){
            hit=true;
            hitCk = popFromPoint(to);
            pushToBar(dstSide, hitCk);
        }
    }

    if (borne){
        pushOff(_actor, mover);
    } else {
        pushToPoint(to, mover);
    }

    st.moved = mover;
    st.hit = hit;
    st.hitChecker = hitCk;
    _steps.push_back(st);

    _diceLeft.erase(it);
    _lastErr.clear();
    return true;
}

bool Board::undoStep() {
    if (_result.over) return false;
    if (_phase!=Phase::Moving) return false;
    if (_steps.empty()) return false;

    Step st = _steps.back();
    _steps.pop_back();

    if (st.borneOff){
        if (_actor==WHITE){ if (_whiteoff>0) --_whiteoff; } else { if (_blackoff>0) --_blackoff; }
        pushToPoint(st.from, st.moved);
    } else {
        if (inBoard(st.to)){
            Checkers& dst = _points[st.to-1];
            auto it = dst.find(st.moved);
            if (it!=dst.end()) dst.erase(it);
        }
        if (st.hit && st.hitChecker){
            if (st.hitChecker->side==WHITE){ if (_whitebar>0) --_whitebar; } else { if (_blackbar>0) --_blackbar; }
            pushToPoint(st.to, st.hitChecker);
        }
        if (st.entered){
            pushToBar(_actor, st.moved);
        } else {
            pushToPoint(st.from, st.moved);
        }
    }

    _diceLeft.push_back(st.pip);
    _lastErr.clear();
    return true;
}


unsigned Board::dfsMax(const SimpleState& st, Side actor,
                       const std::vector<int>& dice, size_t usedMask){
    unsigned best = 0;
    for (size_t i = 0; i < dice.size(); ++i) {
        if (usedMask & (1ULL << i)) continue;
        int pip = dice[i];

        // If bar has checkers, only from==0 is allowed.
        bool barFirst = (actor==WHITE ? st.wbar>0 : st.bbar>0);
        std::vector<int> froms;
        if (barFirst) {
            froms.push_back(0);
        } else {
            if (actor==WHITE) { for (int p=1;p<=24;p++) if (st.w[p]>0) froms.push_back(p); }
            else              { for (int p=1;p<=24;p++) if (st.b[p]>0) froms.push_back(p); }
        }

        auto ownerAt = [&](int p)->Side{
            if (p<1||p>24) return NONE;
            if (st.w[p]>0) return WHITE;
            if (st.b[p]>0) return BLACK;
            return NONE;
        };
        auto countAt = [&](int p)->unsigned{
            if (p<1||p>24) return 0U;
            Side own = ownerAt(p);
            if (own==WHITE) return st.w[p];
            if (own==BLACK) return st.b[p];
            return 0U;
        };
        auto anyFurther = [&](int from)->bool{
            if (actor==WHITE){ for(int p=from+1;p<=24;p++) if (st.w[p]>0) return true; return false; }
            else             { for(int p=1;p<from;p++)    if (st.b[p]>0) return true; return false; }
        };
        auto dec = [&](unsigned &v){ if (v>0) --v; };

        for (int from : froms) {
            int to = (actor==WHITE ? (from==0 ? 25-pip : from-pip)
                                     : (from==0 ? pip : from+pip));

            bool allow=false, hit=false;

            if (to>=1 && to<=24) {
                Side dstSide = ownerAt(to);
                unsigned dstCnt = countAt(to);
                if (dstSide!=NONE && dstSide!=actor && dstCnt>=2) {
                    allow=false; // blocked
                } else {
                    allow=true;
                    hit = (dstSide!=NONE && dstSide!=actor && dstCnt==1);
                }
            } else {
                // Bearing off
                bool allHome=false;
                if (actor==WHITE) {
                    if (st.wbar==0) {
                        unsigned inHome=0; for(int p=1;p<=6;p++) inHome+=st.w[p];
                        allHome = (inHome+st.woff)==15;
                    }
                } else {
                    if (st.bbar==0) {
                        unsigned inHome=0; for(int p=19;p<=24;p++) inHome+=st.b[p];
                        allHome = (inHome+st.boff)==15;
                    }
                }
                if (!allHome) {
                    allow=false;
                } else {
                    if (actor==WHITE)  allow = (from==pip)     || !anyFurther(from);
                    else               allow = (from==(25-pip))|| !anyFurther(from);
                }
            }

            if (!allow) continue;

            // Apply simple move to a copy
            SimpleState s = st;
            if (from==0) {
                if (actor==WHITE) dec(s.wbar); else dec(s.bbar);
            } else {
                if (actor==WHITE) dec(s.w[from]); else dec(s.b[from]);
            }
            if (to>=1 && to<=24) {
                if (hit) {
                    if (actor==WHITE) { dec(s.b[to]); ++s.bbar; }
                    else              { dec(s.w[to]); ++s.wbar; }
                }
                if (actor==WHITE) ++s.w[to]; else ++s.b[to];
            } else {
                if (actor==WHITE) ++s.woff; else ++s.boff;
            }

            unsigned cand = 1 + dfsMax(s, actor, dice, usedMask | (1ULL<<i));
            if (cand>best) best=cand;
        }
    }
    return best;
}

unsigned Board::maxPlayableDice(const SimpleState& st, Side actor, const std::vector<int>& dice){
    if (dice.empty()) return 0;
    return dfsMax(st, actor, dice, 0);
}

bool Board::commitTurn() {
    if (_result.over){ _lastErr="commitTurn: game over"; return false; }
    if (_phase!=Phase::Moving) { _lastErr="commitTurn: not in Moving phase"; return false; }

    if (_steps.empty()){
        unsigned maxUse = maxPlayableDice(_turnStart, _turnStartActor, _turnStartDice);
        if (maxUse>0){ _lastErr="commitTurn: at least one legal move exists"; return false; }
        _diceLeft.clear();
        _phase = Phase::AwaitingRoll;
        _actor = opponent(_actor);
        _lastErr.clear();
        return true;
    }

    unsigned maxUse = maxPlayableDice(_turnStart, _turnStartActor, _turnStartDice);
    unsigned used = (unsigned)_steps.size();
    if (used < maxUse){
        _lastErr="commitTurn: must use maximum number of dice"; return false;
    }
    if (maxUse==1 && _turnStartDice.size()==2 && _turnStartDice[0]!=_turnStartDice[1]){
        int hi = std::max(_turnStartDice[0], _turnStartDice[1]);
        if (_steps[0].pip != hi){
            _lastErr="commitTurn: only one die playable; must use the higher die"; return false;
        }
    }

    _diceLeft.clear();
    _steps.clear();
    _phase = Phase::AwaitingRoll;
    _actor = opponent(_actor);
    _lastErr.clear();
    return true;
}

bool BG::Board::hasAnyLegalStep() const {
    if (_result.over) return false;
    if (_phase != Phase::Moving) return false;
    if (_diceLeft.empty()) return false;

    // Build a SimpleState snapshot of the current live board
    SimpleState s{};
    for (int p = 1; p <= 24; ++p) {
        const Checkers& c = _points[p-1];
        if (c.empty()) continue;
        Side owner = (*c.begin())->side;
        if (owner == WHITE) s.w[p] = (unsigned)c.size();
        else                s.b[p] = (unsigned)c.size();
    }
    s.wbar = _whitebar; s.bbar = _blackbar; s.woff = _whiteoff; s.boff = _blackoff;

    // Use the same search that commitTurn() relies on
    return maxPlayableDice(s, _actor, _diceLeft) > 0;
}

// ===== Convenience counts ====================================================

unsigned Board::countAt(Side s, int point) const {
    if (point<1 || point>24) return 0;
    const Checkers &c(_points[point-1]);
    if (c.empty()) return 0;
    return (*c.begin())->side==s ? (unsigned)c.size() : 0U;
}

unsigned Board::countBar(Side s) const {
    return s==WHITE ? _whitebar : s==BLACK ? _blackbar : 0U;
}

unsigned Board::countOff(Side s) const {
    return s==WHITE ? _whiteoff : s==BLACK ? _blackoff : 0U;
}

// ===== Cube =================================================================

bool Board::offerCube() {
    if (_result.over){ _lastErr="offerCube: game over"; return false; }
    if (_phase!=Phase::AwaitingRoll){ _lastErr="offerCube: only before rolling"; return false; }
    if (_cubePendingFrom!=NONE){ _lastErr="offerCube: offer already pending"; return false; }
    if (!(_cubeholder==NONE || _cubeholder==_actor)){ _lastErr="offerCube: you do not own the cube"; return false; }

    _cubePendingFrom = _actor;
    _phase = Phase::CubeOffered;
    _lastErr.clear();
    return true;
}

bool Board::takeCube() {
    if (_result.over){ _lastErr="takeCube: game over"; return false; }
    if (_phase!=Phase::CubeOffered){ _lastErr="takeCube: no offer pending"; return false; }

    Side taker = opponent(_cubePendingFrom);
    _cubeval <<= 1;
    _cubeholder = taker;
    _cubePendingFrom = NONE;
    _phase = Phase::AwaitingRoll; // offerer is still to roll
    _lastErr.clear();
    return true;
}

bool Board::dropCube() {
    if (_result.over){ _lastErr="dropCube: game over"; return false; }
    if (_phase!=Phase::CubeOffered){ _lastErr="dropCube: no offer pending"; return false; }

    _result.over = true;
    _result.resigned = true;
    _result.finalCube = _cubeval;
    _result.winner = _cubePendingFrom; // offerer wins on drop
    _cubePendingFrom = NONE;
    _lastErr.clear();
    return true;
}

} // namespace BG
