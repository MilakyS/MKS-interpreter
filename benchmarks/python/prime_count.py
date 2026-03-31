count = 0
for i in range(2, 2000):
    is_prime = 1
    j = 2
    while j * j <= i:
        if i % j == 0:
            is_prime = 0
            break
        j += 1
    if is_prime == 1:
        count += 1
print(count)
