n = 10000
primes = [1] * (n + 1)
primes[0] = primes[1] = 0
p = 2
while p * p <= n:
    if primes[p] == 1:
        for i in range(p * p, n + 1, p):
            primes[i] = 0
    p += 1
count = sum(primes)
print(count)
