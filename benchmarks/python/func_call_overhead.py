def add(a, b): return a + b
s = 0
for i in range(100000):
    s = add(s, i)
print(s)
