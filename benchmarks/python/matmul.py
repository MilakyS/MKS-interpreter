n = 50
a = [[i + j for j in range(n)] for i in range(n)]
b = [[i * j for j in range(n)] for i in range(n)]
res = [[0 for _ in range(n)] for _ in range(n)]
for i in range(n):
    for j in range(n):
        s = 0
        for k in range(n):
            s += a[i][k] * b[k][j]
        res[i][j] = s
print(res[n-1][n-1])
