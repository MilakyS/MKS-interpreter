arr = []
for i in range(10000):
    arr.append(i)

sum_val = 0
for j in range(len(arr)):
    sum_val += arr[j]
print(f"Array sum: {sum_val}")
