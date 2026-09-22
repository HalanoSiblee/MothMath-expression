# MothMath

Tiny header-only expression evaluator for calculators. C++17. Zero dependencies.

```cpp
#include "MothMath.hpp"

auto r = expr::evaluate("1.5k + 50% * 100");  // 1550
expr::format_number(*r);                       // "1,550"
expr::format_number(*r, false, true);          // "1.55k"
```

## Evaluate

```cpp
std::optional<double> evaluate(std::string_view);
bool eval(std::string_view, double &out, std::string &err);
```

Returns `nullopt` / `false` on syntax error, div-by-zero, or non-finite result.

## Operators

| Op | Meaning | Assoc / notes |
|----|---------|---------------|
| `+` `-` | add, sub | left |
| `*` `/` | mul, div | left |
| `^` | power | **right** |
| unary `+` `-` | sign | |
| `( )` | group | |
| `%` | percent | postfix — `50%` → `0.5` |

**Precedence:** `%` > `^` > `*` `/` > `+` `-`

```
9*9+9*2        →  99
(9*9+9)*2      →  180
2^3^2          →  512      (right-assoc)
50% * 100      →  50
(10+10)%       →  0.2
```

## Metric suffixes

Case-insensitive. Work with decimals.

| Suffix | Multiplier |
|--------|------------|
| `k` | 10³ |
| `m` | 10⁶ |
| `b` | 10⁹ |
| `t` | 10¹² |
| `q` | 10¹⁵ |

```
1.5k           →  1500
2m + 500k      →  2500000
1.2t           →  1.2e12
```

Commas in input are ignored: `1,500 + 1,500` → `3000`.

## Format

```cpp
std::string format_number(double v,
                          bool separators = true,
                          bool metric     = false);
```

| Call | Result |
|------|--------|
| `format_number(1500)` | `"1,500"` |
| `format_number(1500, false)` | `"1500"` |
| `format_number(1500, false, true)` | `"1.5k"` |
| `format_number(2.5e6, false, true)` | `"2.5m"` |
| `format_number(1.2e12, false, true)` | `"1.2t"` |

When `metric` is on, values below 1000 fall back to plain / separators.

## Design

- Single header. No alloc beyond what `std::string` needs for format.
- Recursive-descent parser. No AST.
- Fast enough for interactive use.

## License

MIT
