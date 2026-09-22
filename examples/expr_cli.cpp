/* expr_cli — evaluate expressions from the command line or stdin.
 *
 *   ./expr_cli "1,500 + 1,500"     → 3,000
 *   ./expr_cli -7+5*2^3            → 33
 *   ./expr_cli                     → interactive
 *
 * Input may use commas or not; the header ignores them when parsing.
 * Output always uses thousand separators via format_number(v).
 *
 * Build:  g++ -std=c++17 -o expr_cli expr_cli.cpp
 */
#include "../MothMath.hpp"

#include <iostream>
#include <string>

static int run_one(const std::string &line)
{
    double v = 0;
    std::string err;
    if (!expr::eval(line, v, err)) {
        std::cerr << "error: " << err << "\n";
        return 1;
    }
    std::cout << expr::format_number(v) << "\n";
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        std::cout << "usage: expr_cli [expression]\n"
                     "  no args → read lines from stdin (q to quit)\n";
        return 0;
    }

    if (argc > 1) {
        std::string e;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) e += ' ';
            e += argv[i];
        }
        return run_one(e);
    }

    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "q" || line == "quit" || line == "exit") break;
        run_one(line);
    }
    return 0;
}
