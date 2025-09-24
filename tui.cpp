/**
 * @file tui.cpp
 * @brief Minimal ncurses REPL for BG::Board with live UTF-8 rendering.
 *
 * This preserves your CLI semantics:
 *   - Empty line → commit
 *   - Two integers → step FROM PIP (e.g., "12 4")
 *   - Commands: open auto | open set W B | roll | set D1 D2 | undo | double | take | drop | legal | state | help | quit
 *
 * Build:
 *   g++ -std=c++20 board.cpp boardrenderer.cpp ncurses_renderer.cpp tui.cpp -lncurses -o bg_tui
 *   # On some Linux: replace -lncurses with -lncursesw
 */

#include <locale.h>
#include <ncurses.h>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm>

#include "board.hpp"
#include "boardrenderer.hpp"
#include "ncurses_renderer.hpp"

using std::string;
using std::vector;

static string trim(const string& s){
    auto a=s.find_first_not_of(" \t\r\n"); if (a==string::npos) return "";
    auto b=s.find_last_not_of(" \t\r\n");  return s.substr(a,b-a+1);
}
static vector<string> splitws(const string& s){
    std::istringstream is(s); vector<string> out; string t; while(is>>t) out.push_back(t); return out;
}
static string lower(string s){ for(char& c: s) c=(char)std::tolower((unsigned char)c); return s; }
static bool parseInt(const string& tok,int& out){
    try{ size_t idx=0; int v=std::stoi(tok,&idx); if(idx!=tok.size()) return false; out=v; return true; }catch(...){return false;}
}

static const char* sideName(BG::Side s){
    switch(s){ case BG::Side::WHITE: return "WHITE"; case BG::Side::BLACK: return "BLACK"; default: return "NONE"; }
}
static const char* phaseName(BG::Phase p){
    switch(p){ case BG::Phase::OpeningRoll: return "OpeningRoll";
    case BG::Phase::AwaitingRoll: return "AwaitingRoll";
    case BG::Phase::Moving:       return "Moving";
    case BG::Phase::CubeOffered:  return "CubeOffered"; }
    return "Unknown";
}

struct UI {
    WINDOW* boardw; // board drawing region
    int rows, cols;
    int board_h, board_w;
    int status_y;   // status line y
    int prompt_y;   // prompt line y
};

static void layout(UI& ui){
    getmaxyx(stdscr, ui.rows, ui.cols);
    ui.board_h = BG::NcursesRenderer::kHeight;
    ui.board_w = BG::NcursesRenderer::kWidth;
    int by = 1; // leave a top margin
    int bx = (ui.cols - ui.board_w)/2; if (bx<0) bx=0;

    if (ui.boardw){ delwin(ui.boardw); ui.boardw=nullptr; }
    ui.boardw = derwin(stdscr, ui.board_h, ui.board_w, by, bx);

    ui.status_y = by + ui.board_h + 1;
    ui.prompt_y = ui.status_y + 1;
}

static void draw_status(const BG::Board& b){
    move(LINES-3, 0); clrtoeol();
    mvprintw(LINES-3, 0, "phase=%s  side=%s  cube=%u holder=%s",
             phaseName(b.phase()), sideName(b.sideToMove()),
             b.cubeValue(), sideName(b.cubeHolder()));

    auto d = b.diceRemaining();
    std::string ds = "[";
    for(size_t i=0;i<d.size();++i){ if(i) ds+=','; ds+=std::to_string(d[i]); }
    ds += "]";
    mvprintw(LINES-3, 45, "dice=%s", ds.c_str());
}

static void draw_msg(const std::string& m){
    move(LINES-2, 0); clrtoeol();
    attron(COLOR_PAIR(4));
    mvprintw(LINES-2, 0, "%s", m.c_str());
    attroff(COLOR_PAIR(4));
}

static string read_line(){
    move(LINES-1, 0); clrtoeol();
    printw("> ");
    echo(); curs_set(1);
    char buf[256]; wgetnstr(stdscr, buf, 255);
    noecho(); curs_set(0);
    return string(buf);
}

int main(){
    // 1) ncurses init
    setlocale(LC_ALL, ""); // enable UTF-8
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color(); use_default_colors();
        init_pair(1, COLOR_WHITE, -1);  // NcursesRenderer CP_WHITE
        init_pair(2, COLOR_CYAN,  -1);  // NcursesRenderer CP_BLACK
        init_pair(3, COLOR_YELLOW,-1);  // CP_BORDER
        init_pair(4, COLOR_GREEN, -1);  // CP_TEXT / status
    }

    UI ui{nullptr,0,0,0,0,0,0};
    layout(ui);

    // 2) game objects
    using namespace BG;
    Board board;
    Rules rules;
    board.startGame(rules); // OpeningRoll
    NcursesRenderer renderer(ui.boardw);

    auto repaint = [&](){
        // board
        Board::State s; board.getState(s);
        renderer.render(s);
        // status
        draw_status(board);
        // help hint
        mvprintw(0, 0, "bg_tui — Enter=commit · \"FROM PIP\"=step · type 'help' for commands · 'quit' exits");
        refresh();
    };

    repaint();

    bool running=true;
    while (running){
        string line = trim(read_line());
        if (line.empty()){
            if (!board.commitTurn()){
                draw_msg(std::string("Cannot commit: ")+board.lastError());
            } else {
                draw_msg(std::string("Turn committed. Next: ")+sideName(board.sideToMove()));
            }
            repaint();
            continue;
        }

        // two-integer shortcut
        {
            auto toks = splitws(line);
            if (toks.size()==2){
                int a,b;
                if (parseInt(toks[0], a) && parseInt(toks[1], b)){
                    if (!board.applyStep(a,b)){
                        draw_msg(std::string("Illegal: ")+board.lastError());
                    } else {
                        draw_msg("Applied step.");
                    }
                    repaint();
                    continue;
                }
            }
        }

        // full commands
        auto toks = splitws(line);
        auto cmd = lower(toks[0]);

        if (cmd=="quit" || cmd=="exit"){
            running=false; break;
        } else if (cmd=="help"){
            draw_msg("Commands: open auto | open set W B | roll | set D1 D2 | step F P | undo | double | take | drop | legal | state | quit");
        } else if (cmd=="open"){
            if (toks.size()<2){ draw_msg("Usage: open auto | open set W B"); repaint(); continue; }
            auto sub = lower(toks[1]);
            try{
                if (sub=="auto"){
                    auto roll = board.rollOpening();
                    draw_msg("Opening roll: W="+std::to_string(roll.first)+" B="+std::to_string(roll.second));
                } else if (sub=="set"){
                    if (toks.size()<4){ draw_msg("Usage: open set W B"); repaint(); continue; }
                    int W,B;
                    if (!parseInt(toks[2],W)||!parseInt(toks[3],B)){ draw_msg("Dice must be 1..6"); repaint(); continue; }
                    bool resolved = board.setOpeningDice(W,B);
                    draw_msg(resolved? "Opening resolved":"Opening doubles processed; roll again.");
                } else {
                    draw_msg("Usage: open auto | open set W B");
                }
            }catch(const std::exception& e){
                draw_msg(std::string("Error: ")+e.what());
            }
        } else if (cmd=="roll"){
            try{
                auto d = board.rollDice();
                draw_msg("Rolled: "+std::to_string(d.first)+","+std::to_string(d.second));
            }catch(const std::exception& e){
                draw_msg(std::string("Error: ")+e.what());
            }
        } else if (cmd=="set"){
            if (toks.size()<3){ draw_msg("Usage: set D1 D2"); repaint(); continue; }
            int d1,d2;
            if (!parseInt(toks[1],d1)||!parseInt(toks[2],d2)){ draw_msg("Dice must be 1..6"); repaint(); continue; }
            try{ board.setDice(d1,d2); draw_msg("Dice set."); }
            catch(const std::exception& e){ draw_msg(std::string("Error: ")+e.what()); }
        } else if (cmd=="step"){
            if (toks.size()<3){ draw_msg("Usage: step FROM PIP"); repaint(); continue; }
            int from,pip;
            if (!parseInt(toks[1],from)||!parseInt(toks[2],pip)){ draw_msg("FROM/PIP must be ints"); repaint(); continue; }
            if (!board.applyStep(from,pip)) draw_msg(std::string("Illegal: ")+board.lastError());
            else draw_msg("Applied step.");
        } else if (cmd=="undo"){
            if (!board.undoStep()) draw_msg("Nothing to undo (or wrong phase).");
            else draw_msg("Undid last step.");
        } else if (cmd=="commit"){
            if (!board.commitTurn()) draw_msg(std::string("Cannot commit: ")+board.lastError());
            else draw_msg(std::string("Turn committed. Next: ")+sideName(board.sideToMove()));
        } else if (cmd=="double"){
            if (!board.offerCube()) draw_msg(std::string("Cannot offer: ")+board.lastError());
            else draw_msg("Cube offered.");
        } else if (cmd=="take"){
            if (!board.takeCube()) draw_msg(std::string("Cannot take: ")+board.lastError());
            else draw_msg("Cube taken.");
        } else if (cmd=="drop"){
            if (!board.dropCube()) draw_msg(std::string("Cannot drop: ")+board.lastError());
            else draw_msg("*** GAME OVER (resignation) ***");
        } else if (cmd=="legal"){
            draw_msg(board.hasAnyLegalStep()? "A legal step exists." : "No legal step exists.");
        } else if (cmd=="state"){
            BG::Board::State s; board.getState(s);
            draw_msg("bars W="+std::to_string(s.whitebar)+" B="+std::to_string(s.blackbar)+
                     " off W="+std::to_string(s.whiteoff)+" B="+std::to_string(s.blackoff));
        } else {
            draw_msg("Unknown command. Type 'help'.");
        }

        repaint();
    }

    endwin();
    return 0;
}
