index = {}
i = 0
while i < 50000:
    index["cle" + str(i)] = i
    i = i + 1

total = 0
j = 0
while j < 50000:
    total = total + index["cle" + str(j)]
    j = j + 1

print(len(index))
print(total)
