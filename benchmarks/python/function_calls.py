def calculer(n):
    double = n * 2
    suivant = n + 1
    return double + suivant


index = 0
total = 0
while index < 100000:
    total = total + calculer(index)
    index = index + 1
print(total)
