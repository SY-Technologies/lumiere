texte = "é中😀" * 10000
total = 0
for caractere in texte:
    total = total + ord(caractere)
print(total)
