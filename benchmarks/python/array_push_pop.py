arr = []
for i in range(100000):
    arr.append(i)
for i in range(100000):
    arr.pop()
print(len(arr))
