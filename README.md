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

Zinc has four primitive types and a reference-counted string type:

| Type     | Description                | C equivalent |
|----------|----------------------------|--------------|
| `int`    | 64-bit signed integer      | `int64_t`    |
| `float`  | 64-bit floating point      | `double`     |
| `bool`   | Boolean                    | `bool`       |
| `char`   | ASCII character            | `char`       |
| `String` | Reference-counted string   | `ZnString*`  |

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

### Strings

The `String` type is a reference-counted, immutable-content string. String literals use double quotes and are automatically managed.

```
let greeting = "hello, world"
let empty = ""
```

**String operations:**

```
# Concatenation with +
let full = "hello" + " " + "world"

# Length property
let len = full.length    # 11

# Character indexing (returns char)
let ch = full[0]         # 'h'

# Comparison
if greeting == "hello, world" {
    # strings are compared by value
}
```

**String interpolation** embeds expressions inside strings with `${}`:

```
let name = "world"
let msg = "hello ${name}!"          # "hello world!"

let x = 42
let info = "x is ${x}"             # "x is 42"

let a = 10
let b = 20
let sum = "${a} + ${b} = ${a + b}" # "10 + 20 = 30"
```

**Automatic coercion:** When `+` has a `String` on either side, the other operand is automatically converted:

```
let n = 42
let s = "value: " + n      # "value: 42"
let f = "pi: " + 3.14      # "pi: 3.14"
let b = "flag: " + true    # "flag: true"
let c = "char: " + 'A'     # "char: A"
```

**Escape sequences:** `\n` (newline), `\t` (tab), `\r` (carriage return), `\\` (backslash), `\"` (quote), `\$` (dollar sign), `\0` (null).

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

### Optional Types

Optional types represent a value that may or may not have an outcome. There is no `null`, `nil`, or `None` in Zinc — optionals arise naturally from control flow that may not produce a result:

```
# An if-without-else produces an optional — the outcome depends on the condition
let maybe_val = if x > 5 { 42 }     # type: int?

# Conditional loops produce optionals — the break may never execute
var i = 0
let result = while i < 10 {
    i += 1
    if i == 5 { break i * 10 }
}                                     # type: int?

# For loops are always conditional
let found = for var j = 0; j < 100; j += 1 {
    if j == 42 { break j }
}                                     # type: int?
```

Use the `?` operator to check whether an optional has a value. Inside the `if` body, the variable is **type-narrowed** to its non-optional type:

```
let maybe_val = if x > 5 { 42 }

if maybe_val? {
    # maybe_val is narrowed to int here
    let doubled = maybe_val * 2
}
```

Reference types (strings) use `null` under the hood, but this is an implementation detail — the `?` operator works uniformly on all optional types:

```
let maybe_str = if x > 0 { "hello" }   # type: String?

if maybe_str? {
    let len = maybe_str.length
}
```

**Optional type annotations** can be used in function parameters (`int?`, `String?`, etc.):

```
func maybe_double(val: int?) {
    if val? {
        val * 2
    }
}
```

An infinite loop (`while true` or `until false`) always executes its break, so it produces a non-optional value:

```
let val = while true {
    break 42
}                        # type: int (not optional)

let val2 = until false {
    break 99
}                        # type: int (not optional)
```

### Structs

Structs are value types with named fields. Each field can be independently declared as `var` (mutable) or `let` (immutable).

```
struct Point {
    var x: int
    var y: int
}

struct Color {
    let red: int
    let green: int
    let blue: int
}
```

**Creating instances** uses named arguments:

```
var p = Point(x: 10, y: 20)
let c = Color(red: 255, green: 128, blue: 0)
```

**Field access** uses dot notation:

```
let sum = p.x + p.y
```

**Field mutation** respects both field-level and binding-level immutability:

```
p.x = 100        # OK — var field on var binding
# c.red = 50     # Error — let field
# let q = Point(x: 1, y: 2)
# q.x = 10       # Error — var field but let binding (value type)
```

For value types like structs, a `let` binding makes the entire value immutable — even `var` fields cannot be mutated through a `let` binding. This is because structs are copied by value, so mutating a `let` binding would modify the local copy.

**Default values** can be provided for fields:

```
struct Config {
    var width: int
    var height: int
    var debug = false
}

var cfg = Config(width: 800, height: 600)
# cfg.debug is false (the default)
```

Fields without defaults are required when creating instances.

**Nested structs:**

```
struct Rect {
    var origin: Point
    var size: Point
}

var r = Rect(origin: Point(x: 0, y: 0), size: Point(x: 100, y: 50))
r.origin.x = 10
```

**Structs as function parameters and return values:**

```
func point_sum(p: Point) {
    p.x + p.y
}

func make_point(x: int, y: int) {
    Point(x: x, y: y)
}
```

Parameters are passed by value (copied). Struct types are used in type annotations by name.

### Classes

Classes are reference types with automatic reference counting (ARC). Like structs, each field is declared with `let` (immutable) or `var` (mutable).

```
class Point {
    var x: int
    var y: int
}

class Color {
    let red: int
    let green: int
    let blue: int
}
```

Classes are instantiated using named arguments, just like structs:

```
var p = Point(x: 10, y: 20)
let c = Color(red: 255, green: 128, blue: 0)
```

**Reference semantics:** Classes are heap-allocated and reference-counted. Assignment copies the reference, not the value:

```
var a = Point(x: 1, y: 2)
var b = a          # b points to the same object as a
b.x = 99
# a.x is now also 99
```

**Per-field mutability with reference rules:** Unlike structs (value types), a `let` binding on a class variable only prevents reassignment — `var` fields are still mutable through the reference:

```
let p = Point(x: 1, y: 2)
p.x = 10          # OK: var field, reference type
p = Point(x: 3, y: 4)  # Error: let binding prevents reassignment

var c = Color(red: 255, green: 0, blue: 0)
c.red = 128        # Error: let field prevents mutation
```

Fields can have default values:

```
class Config {
    var width: int
    var height: int
    var debug = false
}

var cfg = Config(width: 800, height: 600)  # debug defaults to false
```

**Memory management** is automatic. Objects are freed when their reference count reaches zero. No manual memory management is needed.

> **Note:** ARC does not detect reference cycles. Avoid creating objects that reference each other in a cycle, as they will not be freed.

### Built-in Functions

#### print

`print` outputs a string to standard output.

```
print("hello world\n")
print("the answer is ${40 + 2}\n")
```

Takes exactly one `String` argument. Use string interpolation and escape sequences for formatting.

### Program Structure

A Zinc program is a series of `func`, `struct`, and `class` definitions. Execution starts at `main`.

```
func helper(x: int) {
    x * 2
}

func main() {
    var result = helper(21)
    0
}
```
