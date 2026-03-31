arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
s = 0
for i in range(100000):
    s += arr[i % 10]
print(s)
