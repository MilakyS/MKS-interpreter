n = 500
arr = [n - i for i in range(n)]
for i in range(n):
    for j in range(n - i - 1):
        if arr[j] > arr[j + 1]:
            arr[j], arr[j + 1] = arr[j + 1], arr[j]
print(f"{arr[0]} {arr[n-1]}")
