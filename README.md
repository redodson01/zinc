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

**Compound type fields** — struct fields can be arrays, hashes, or any other type. Reference-counted fields are automatically retained and released:

```
struct Holder {
    var values: int[]
    var settings: [String: int]
}

var h = Holder(values: [1, 2, 3], settings: ["width": 800])
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

**Compound type fields** — class fields can be arrays, hashes, or other classes. ARC handles all retain/release automatically:

```
class Container {
    var items: int[]
    var data: [String: int]
}

var c = Container(items: [1, 2, 3], data: ["a": 1])
c.items = [4, 5, 6]    # old array released, new array retained
```

**Memory management** is automatic. Objects are freed when their reference count reaches zero. No manual memory management is needed.

> **Note:** ARC does not detect reference cycles. Avoid creating objects that reference each other in a cycle, as they will not be freed.

### Tuples

Tuples are lightweight value types for grouping multiple values. Fields are implicitly `var`.

**Positional tuples** use numeric access (`.0`, `.1`, etc.):

```
var point = (10, 20)
let x = point.0       # 10
let y = point.1       # 20
point.0 = 30          # mutation (var binding)
```

**Named tuples** use named field access:

```
var height = (feet: 5, inches: 11)
let f = height.feet    # 5
height.inches = 10     # mutation
```

Tuples with the same field types share a single type (canonical deduplication):

```
var a = (1, 2)
var b = (3, 4)
a = b                  # same type: both (int, int)
```

Like structs, `let` bindings prevent mutation of tuple fields:

```
let t = (10, 20)
t.0 = 5               # Error: immutable binding
```

**Tuple type annotations** can be used in function parameters — both positional and named:

```
func sum_pair(p: (int, int)) {
    p.0 + p.1
}

func distance(p: (x: int, y: int)) {
    p.x + p.y
}
```

### Object Literals

Object literals create anonymous, heap-allocated instances with named fields. Like classes, they are reference types managed by ARC.

```
let point = { x: 10, y: 20 }
let info = { name: "test", count: 42, active: true }

var px = point.x    # 10
```

**Object types** can be used in function parameter and return positions:

```
func sum(p: { x: int, y: int }) {
    p.x + p.y
}

func make_point() {
    { x: 10, y: 20 }
}

let result = sum({ x: 3, y: 4 })   # 7
```

Objects with the same field names and types share a single anonymous type. Memory management is automatic, just like classes.

### Arrays

Arrays are dynamic, reference-counted collections of values. All elements must have the same type.

```
var nums = [1, 2, 3, 4, 5]
var empty = int[]
```

**Element access** uses bracket notation with zero-based indexing:

```
let first = nums[0]     # 1
let third = nums[2]     # 3
```

**Length** is available as a property:

```
let len = nums.length    # 5
```

**Index assignment** modifies elements in place:

```
nums[0] = 99
```

**Element types** are inferred from the first element. All elements must match. Empty arrays require an explicit element type:

```
var ints = [1, 2, 3]          # int[]
var strs = ["hello", "world"] # String[]
var flags = [true, false]     # bool[]
var empty = int[]             # empty int array
```

**Generic arrays** support any element type — structs, classes, tuples, objects, nested arrays, and hashes:

```
struct Point { var x: int; var y: int }
class Node { var value: int }

var points = [Point(x: 1, y: 2), Point(x: 3, y: 4)]   # struct[]
var nodes = [Node(value: 10), Node(value: 20)]           # class[]
var nested = [[1, 2], [3, 4]]                            # int[][]
var empty_pts = Point[]                                   # empty Point array
```

**Array type annotations** can be used in function parameters:

```
func first(nums: int[]) {
    nums[0]
}
```

**Functions** can return arrays:

```
func make_array() {
    [10, 20, 30]
}

var arr = make_array()
```

**Memory management** is automatic via reference counting. Arrays are freed when their last reference goes away. Bounds checking is performed at runtime — out-of-bounds access terminates the program with an error message.

### Hash Tables

Hash tables are dynamic, reference-counted key-value stores. All keys must have the same type, and all values must have the same type.

```
var ht = ["a": 1, "b": 2, "c": 3]
var empty = [String: int]
```

**Key lookup** uses bracket notation:

```
let a = ht["a"]       # 1
let b = ht["b"]       # 2
```

**Length** is available as a property:

```
let len = ht.length    # 3
```

**Mutation** updates existing keys or adds new ones:

```
ht["a"] = 99           # update existing key
ht["d"] = 4            # add new key
```

**Key types** can be any primitive or string type. All keys must match. Empty hashes require explicit key and value types:

```
var str_ht = ["x": 10, "y": 20]   # String keys
var int_ht = [1: "one", 2: "two"] # int keys
var bool_ht = [true: 1, false: 0] # bool keys
var empty = [String: int]          # empty hash
```

**Generic hash values** support any type — structs, classes, and other compound types:

```
class Node { var value: int }

var map = ["a": Node(value: 100)]            # String -> Node
var pts = [1: Point(x: 5, y: 6)]            # int -> Point
var empty_map = [String: Node]               # empty hash with Node values
```

**Hash type annotations** can be used in function parameters:

```
func lookup(table: [String: int], key: String) {
    table[key]
}
```

**Functions** can return hash tables:

```
func make_hash() {
    ["x": 10, "y": 20]
}

var ht = make_hash()
```

**Memory management** is automatic via reference counting, just like arrays.

### Built-in Functions

#### print

`print` outputs a string to standard output.

```
print("hello world\n")
print("the answer is ${40 + 2}\n")
```

Takes exactly one `String` argument. Use string interpolation and escape sequences for formatting.

### Program Structure

A Zinc program is a series of `func`, `struct`, `class`, tuple, and object literal definitions. Execution starts at `main`.

```
func helper(x: int) {
    x * 2
}

func main() {
    var result = helper(21)
    0
}
```
