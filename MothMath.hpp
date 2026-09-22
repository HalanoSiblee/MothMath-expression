#pragma once
/* MothMath.hpp — tiny header-only expression evaluator (C++17)
 *
 *   expr::evaluate("1.5k + 50%");     // 1500.5
 *   expr::format_number(1500, true);            // "1,500"
 *   expr::format_number(1500, false);           // "1500"
 *   expr::format_number(1500, false, true);     // "1.5k"
 *
 * Operators: + - * / ^  unary +/-  ( )  postfix %
 * Suffixes:  k=1e3  m=1e6  b=1e9  t=1e12  q=1e15  (case-insensitive)
 * Precedence: % > ^ > * / > + -   (^ right-assoc)
 * Commas in input are ignored.
 */
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace expr {
namespace detail {

inline double metric_mul(char c) {
    switch (c | 32) { // tolower
        case 'k': return 1e3;
        case 'm': return 1e6;
        case 'b': return 1e9;
        case 't': return 1e12;
        case 'q': return 1e15;
        default:  return 0;
    }
}

struct P {
    std::string_view s;
    size_t i = 0;
    const char *err = nullptr;

    explicit P(std::string_view src) : s(src) {}
    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i; }
    bool fail(const char *m) { if (!err) err = m; return false; }

    bool num(double &o) {
        skip();
        if (i >= s.size() || (!isdigit((unsigned char)s[i]) && s[i] != '.'))
            return fail("Expected a number");
        std::string t;
        while (i < s.size()) {
            char c = s[i];
            if (isdigit((unsigned char)c) || c == '.' || c == 'e' || c == 'E') {
                t += c; ++i;
            } else if ((c == '+' || c == '-') && !t.empty() &&
                       (t.back() == 'e' || t.back() == 'E')) {
                t += c; ++i;
            } else if (c == ',') {
                ++i;
            } else break;
        }
        if (t.empty()) return fail("Invalid number");
        char *e = nullptr;
        o = strtod(t.c_str(), &e);
        if (e == t.c_str()) return fail("Invalid number");

        // metric suffix: 1.5k  2M  3b ...
        skip();
        if (i < s.size()) {
            double mul = metric_mul(s[i]);
            if (mul > 0) { o *= mul; ++i; }
        }
        return true;
    }

    bool primary(double &o) {
        skip();
        if (i < s.size() && s[i] == '(') {
            ++i;
            if (!expr(o)) return false;
            skip();
            if (i >= s.size() || s[i] != ')') return fail("Missing ')'");
            ++i;
        } else {
            if (!num(o)) return false;
        }
        // postfix %  (50% → 0.5, (10+10)% → 0.2)
        skip();
        if (i < s.size() && s[i] == '%') {
            ++i;
            o *= 0.01;
        }
        return true;
    }

    bool unary(double &o) {
        skip();
        if (i < s.size() && s[i] == '-') { ++i; if (!unary(o)) return false; o = -o; return true; }
        if (i < s.size() && s[i] == '+') { ++i; return unary(o); }
        return primary(o);
    }

    bool power(double &o) {
        if (!unary(o)) return false;
        skip();
        if (i < s.size() && s[i] == '^') {
            ++i;
            double r = 0;
            if (!power(r)) return false;
            o = pow(o, r);
        }
        return true;
    }

    bool term(double &o) {
        if (!power(o)) return false;
        for (;;) {
            skip();
            if (i >= s.size() || (s[i] != '*' && s[i] != '/')) break;
            char op = s[i++];
            double r = 0;
            if (!power(r)) return false;
            if (op == '*') o *= r;
            else { if (r == 0.0) return fail("Division by zero"); o /= r; }
        }
        return true;
    }

    bool expr(double &o) {
        if (!term(o)) return false;
        for (;;) {
            skip();
            if (i >= s.size() || (s[i] != '+' && s[i] != '-')) break;
            char op = s[i++];
            double r = 0;
            if (!term(r)) return false;
            if (op == '+') o += r; else o -= r;
        }
        return true;
    }
};

} /* detail */

inline std::optional<double> evaluate(std::string_view in)
{
    detail::P p(in);
    p.skip();
    if (p.i >= p.s.size()) return 0.0;
    double v = 0;
    if (!p.expr(v)) return std::nullopt;
    p.skip();
    if (p.i < p.s.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

inline bool eval(std::string_view in, double &out, std::string &err)
{
    detail::P p(in);
    p.skip();
    if (p.i >= p.s.size()) { out = 0; err.clear(); return true; }
    if (!p.expr(out)) { err = p.err ? p.err : "Syntax error"; return false; }
    p.skip();
    if (p.i < p.s.size()) { err = "Unexpected characters"; return false; }
    if (!std::isfinite(out)) { err = "Result is not a number"; return false; }
    err.clear();
    return true;
}

/**
 * Format a number.
 *   separators: thousand commas
 *   metric:     k m b t q suffixes (1.5k, 2m, …)
 * metric wins over separators when both true.
 */
inline std::string format_number(double v, bool separators = true, bool metric = false)
{
    if (!std::isfinite(v)) return "Error";
    if (std::fabs(v) < 1e-15) v = 0;

    if (metric) {
        const struct { double lim; const char *suf; } tab[] = {
            {1e15, "q"}, {1e12, "t"}, {1e9, "b"}, {1e6, "m"}, {1e3, "k"},
        };
        double av = std::fabs(v);
        for (auto &e : tab) {
            if (av >= e.lim) {
                double s = v / e.lim;
                char b[64];
                // up to 6 significant digits, strip trailing zeros
                std::snprintf(b, sizeof b, "%.6g", s);
                std::string o(b);
                o += e.suf;
                return o;
            }
        }
        // below 1k — fall through to plain/separators
    }

    if (std::fabs(v) >= 1e15 || (std::fabs(v) > 0 && std::fabs(v) < 1e-6)) {
        char b[64];
        std::snprintf(b, sizeof b, "%.15g", v);
        return b;
    }

    char raw[96];
    std::snprintf(raw, sizeof raw, "%.15g", v);
    if (std::strchr(raw, '.') && !std::strchr(raw, 'e') && !std::strchr(raw, 'E')) {
        char *e = raw + std::strlen(raw) - 1;
        while (e > raw && *e == '0') *e-- = 0;
        if (*e == '.') *e = 0;
    }
    if (!separators) return raw;

    bool neg = raw[0] == '-';
    const char *ip = raw + neg;
    const char *dot = std::strchr(ip, '.');
    int n = dot ? (int)(dot - ip) : (int)std::strlen(ip);
    const char *frac = dot ? dot : "";

    std::string o;
    if (neg) o += '-';
    for (int i = 0; i < n; ++i) {
        if (i && (n - i) % 3 == 0) o += ',';
        o += ip[i];
    }
    o += frac;
    return o;
}

} /* expr */
