def identite(texte):
    return texte


source = "abcdefgh" * 16384
resultat = ""
index = 0
while index < 3000:
    resultat = identite(source)
    index = index + 1
# Le programme Lumière affiche un Logique, dont le nom est français.
print("vrai" if resultat == source else "faux")
print(len(resultat))
