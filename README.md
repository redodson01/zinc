# Zinc

A statically-typed, expression-oriented language that transpiles to C.

## Quick Start

```bash
make            # Build the compiler
./build/zinc -c program.zn -o program
./program
```

## Building

Requires GCC, Flex, and Bison.

```bash
make            # Build the compiler (build/zinc)
make test-all   # Run all tests
make clean      # Clean build artifacts
```

## Language Guide

### Comments

Comments start with `#` and extend to end of line.

```
# This is a comment
var x = 42  # Inline comment
```

### Types

Zinc has four primitive types:

| Type    | Description             | C equivalent |
|---------|-------------------------|--------------|
| `int`   | 64-bit signed integer   | `int64_t`    |
| `float` | 64-bit floating point   | `double`     |
| `bool`  | Boolean                 | `bool`       |
| `char`  | ASCII character         | `char`       |

Types are inferred from context — you never write type annotations on variables.

### Literals

```
# Integers
42
0xFF          # Hexadecimal
1e10          # Scientific notation

# Floats
3.14
2.5e-3        # Scientific notation

# Booleans
true
false

# Characters
'A'
'\n'          # Escape sequences: \n \t \r \0 \\ \'
```

### Variables and Constants

`var` declares a mutable variable, `let` declares an immutable constant.

```
var counter = 0      # Mutable
let MAX = 100        # Immutable — cannot be reassigned
```

### Operators

#### Arithmetic
`+`, `-`, `*`, `/`, `%` (modulo)

#### Comparison
`==`, `!=`, `<`, `>`, `<=`, `>=`

#### Logical
`&&` (and), `||` (or), `!` (not)

#### Assignment
`=`, `+=`, `-=`, `*=`, `/=`, `%=`

#### Increment/Decrement
`++`, `--` (both prefix and postfix)

### Functions

Functions are declared with `func`. The last expression in the body is the implicit return value.

```
func add(a: int, b: int) {
    a + b
}

func factorial(n: int) {
    if n <= 1 {
        return 1
    }
    n * factorial(n - 1)
}
```

Parameters are immutable by default and require type annotations. Return types are inferred.

Every Zinc program needs a `main` function. Return `0` for success.

```
func main() {
    0
}
```

### Control Flow

All control flow constructs are expressions — they can return values.

#### if / else

```
if condition {
    # ...
} else if other {
    # ...
} else {
    # ...
}
```

When both branches return the same type, `if`/`else` can be used as an expression:

```
var x = if score >= 90 { 4 } else { 3 }
```

#### unless

`unless` is syntactic sugar for `if !condition`:

```
unless logged_in {
    redirect = true
}
```

`unless`/`else` works as an expression too:

```
var x = unless failed { 100 } else { 0 }
```

#### while

```
while condition {
    # ...
}
```

#### until

`until` is syntactic sugar for `while !condition`:

```
until done {
    process_next()
}
```

#### for

C-style for loops with `var`/`let` declarations in the initializer:

```
for var i = 0; i < 10; i++ {
    # ...
}
```

#### break and continue

`break` and `continue` can carry values for loop-as-expression:

```
break 42       # Exit loop with value
continue 0     # Skip to next iteration with value
```

### Loops as Expressions

`while true` (and `until false`) loops with `break value` produce expression results:

```
var result = while true {
    if found {
        break answer
    }
}
```

Since `while true` always executes at least once and can only exit via `break`, the result type is non-optional.

`until false` works the same way (it desugars to `while true` internally):

```
var x = until false {
    break 42
}
```

### Program Structure

A Zinc program is a series of `func` definitions. Execution starts at `main`.

```
func helper(x: int) {
    x * 2
}

func main() {
    var result = helper(21)
    0
}
```
