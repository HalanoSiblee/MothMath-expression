/* expr_ncurses — simple expression calculator (ncurses UI).
 *
 * Type an expression, press Enter to evaluate.
 *   + - * / ^ ( )   and commas as thousand separators
 * Keys: Enter = eval, Backspace, Esc/q = quit, c = clear
 *
 * Build:
 *   g++ -std=c++17 -o expr_ncurses expr_ncurses.cpp -lncurses
 */
#include "../MothMath.hpp"

#include <ncurses.h>
#include <string>

static void draw(const std::string &expr, const std::string &result,
                 const std::string &status)
{
    erase();
    mvprintw(0, 0, "expr calculator  (Enter=eval  c=clear  q=quit)");
    mvhline(1, 0, ACS_HLINE, COLS);

    mvprintw(3, 2, "expr: %s", expr.c_str());
    /* caret */
    mvprintw(3, 2 + 6 + (int)expr.size(), "_");

    mvprintw(5, 2, "  =   %s", result.c_str());
    mvprintw(LINES - 2, 0, "%s", status.c_str());
    refresh();
}

int main()
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    std::string expr, result, status = "type an expression";
    bool running = true;

    while (running) {
        draw(expr, result, status);
        int ch = getch();

        if (ch == 'q' || ch == 'Q' || ch == 27) {
            running = false;
        } else if (ch == 'c' || ch == 'C') {
            expr.clear();
            result.clear();
            status = "cleared";
        } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!expr.empty()) expr.pop_back();
        } else if (ch == '\n' || ch == KEY_ENTER) {
            if (expr.empty()) {
                status = "empty expression";
            } else {
                double v = 0;
                std::string err;
                if (expr::eval(expr, v, err)) {
                    result = expr::format_number(v, true);
                    status = "ok";
                } else {
                    result.clear();
                    status = "error: " + err;
                }
            }
        } else if (ch >= 32 && ch < 127) {
            char c = (char)ch;
            /* accept digits, ops, parens, comma, space, caret */
            if ((c >= '0' && c <= '9') || c == '.' || c == ',' ||
                c == '+' || c == '-' || c == '*' || c == '/' || c == '^' ||
                c == '(' || c == ')' || c == ' ') {
                expr.push_back(c);
            }
        }
    }

    endwin();
    return 0;
}
