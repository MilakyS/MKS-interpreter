#!/usr/bin/env bash

set -e

mkdir -p tests/cases
mkdir -p tests/expected

echo "Generating MKS test suite..."

# ---------------- arithmetic ----------------
cat > tests/cases/arithmetic_01.mks << 'EOF'
Writeln(1 + 2);
Writeln(10 - 3);
Writeln(4 * 5);
Writeln(20 / 4);
Writeln(10 % 3);
EOF

cat > tests/expected/arithmetic_01.txt << 'EOF'
3
7
20
5
1
EOF

# ---------------- logic ----------------
cat > tests/cases/logic_01.mks << 'EOF'
Writeln(1 && 1);
Writeln(1 && 0);
Writeln(0 || 1);
Writeln(0 || 0);
EOF

cat > tests/expected/logic_01.txt << 'EOF'
1
0
1
0
EOF

# ---------------- comparison ----------------
cat > tests/cases/comparison_01.mks << 'EOF'
Writeln(1 ?= 1);
Writeln(1 !? 2);
Writeln(5 > 3);
Writeln(2 < 1);
Writeln(3 >= 3);
Writeln(2 <= 5);
EOF

cat > tests/expected/comparison_01.txt << 'EOF'
1
1
1
0
1
1
EOF

# ---------------- strings ----------------
cat > tests/cases/strings_01.mks << 'EOF'
var s =: "Hello";
Writeln(s);
Writeln(s[1]);
Writeln(s.upper());
Writeln(s.lower());
Writeln(s.len());
EOF

cat > tests/expected/strings_01.txt << 'EOF'
Hello
e
HELLO
hello
5
EOF

# ---------------- empty string ----------------
cat > tests/cases/string_empty_01.mks << 'EOF'
var s =: "";
Writeln(s.empty());
Writeln(s.len());
EOF

cat > tests/expected/string_empty_01.txt << 'EOF'
1
0
EOF

# ---------------- arrays ----------------
cat > tests/cases/arrays_01.mks << 'EOF'
var arr =: [1, 2, 3];
Writeln(arr[0]);
arr[1] =: 99;
Writeln(arr[1]);
Writeln(arr.len());
EOF

cat > tests/expected/arrays_01.txt << 'EOF'
1
99
3
EOF

# ---------------- empty array ----------------
cat > tests/cases/array_empty_01.mks << 'EOF'
var arr =: [];
Writeln(arr.len());
arr.inject(10);
Writeln(arr[0]);
Writeln(arr.len());
EOF

cat > tests/expected/array_empty_01.txt << 'EOF'
0
10
1
EOF

# ---------------- array methods ----------------
cat > tests/cases/array_methods_01.mks << 'EOF'
var arr =: [1, 2, 3];
Writeln(arr.offset(1));
arr.exclude(1);
Writeln(arr);
arr.purge();
Writeln(arr);
EOF

cat > tests/expected/array_methods_01.txt << 'EOF'
2
[1, 3]
[]
EOF

# ---------------- nested arrays ----------------
cat > tests/cases/nested_arrays_01.mks << 'EOF'
var arr =: [[1, 2], [3, 4]];
Writeln(arr[0][1]);
Writeln(arr[1][0]);
EOF

cat > tests/expected/nested_arrays_01.txt << 'EOF'
2
3
EOF

# ---------------- if / else ----------------
cat > tests/cases/if_01.mks << 'EOF'
var x =: 10;

if(x > 5) ->
    Writeln("big");
<- else ->
    Writeln("small");
<-
EOF

cat > tests/expected/if_01.txt << 'EOF'
big
EOF

# ---------------- while ----------------
cat > tests/cases/while_01.mks << 'EOF'
var i =: 0;

while(i < 3) ->
    Writeln(i);
    i =: i + 1;
<-
EOF

cat > tests/expected/while_01.txt << 'EOF'
0
1
2
EOF

# ---------------- for ----------------
cat > tests/cases/for_01.mks << 'EOF'
for(var i =: 0; i < 3; i =: i + 1) ->
    Writeln(i);
<-
EOF

cat > tests/expected/for_01.txt << 'EOF'
0
1
2
EOF

# ---------------- functions ----------------
cat > tests/cases/functions_01.mks << 'EOF'
fnc add(a, b) ->
    return a + b;
<-

Writeln(add(2, 3));
EOF

cat > tests/expected/functions_01.txt << 'EOF'
5
EOF

# ---------------- recursion ----------------
cat > tests/cases/recursion_01.mks << 'EOF'
fnc fact(n) ->
    if(n <= 1) ->
        return 1;
    <-
    return n * fact(n - 1);
<-

Writeln(fact(5));
EOF

cat > tests/expected/recursion_01.txt << 'EOF'
120
EOF

# ---------------- return in if ----------------
cat > tests/cases/return_if_01.mks << 'EOF'
fnc test(x) ->
    if(x > 0) ->
        return 111;
    <-
    return 222;
<-

Writeln(test(1));
Writeln(test(0));
EOF

cat > tests/expected/return_if_01.txt << 'EOF'
111
222
EOF

# ---------------- nested expressions ----------------
cat > tests/cases/nested_01.mks << 'EOF'
Writeln(2 + 3 * 4);
Writeln((2 + 3) * 4);
EOF

cat > tests/expected/nested_01.txt << 'EOF'
14
20
EOF

echo "Done! Tests created in ./tests/"
