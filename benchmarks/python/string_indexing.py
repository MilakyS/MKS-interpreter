s = "abcdefghij"
res = ""
for i in range(100000):
    c = s[i % 10]
    res = c
print(res)
