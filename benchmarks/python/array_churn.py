arr = []
for i in range(10000):
    row = []
    for j in range(10):
        row.append(j)
    arr.append(row)
for i in range(10000):
    arr.pop(0)
print(len(arr))
