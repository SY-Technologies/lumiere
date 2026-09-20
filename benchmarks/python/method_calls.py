class Accumulateur:
    def __init__(self, total):
        self.total = total

    def ajouter(self, valeur):
        self.total = self.total + valeur
        return self.total


accumulateur = Accumulateur(0)
index = 0
while index < 2000000:
    accumulateur.ajouter(index)
    index = index + 1
print(accumulateur.total)
