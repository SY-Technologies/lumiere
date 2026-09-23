# Les échecs d'analyse sont des exceptions ici, là où le programme Lumière rend
# un Résultat et le fait remonter par `ou propager` : chaque langage avec son
# mécanisme d'erreur, puisque c'est lui qu'un programme ordinaire emploie.

MODULE = 1000003


class Hasard:
    def __init__(self, etat):
        self.etat = etat

    def suivant(self, borne):
        self.etat = (self.etat * 1103515245 + 12345) % 2147483648
        return self.etat // 65536 % borne


class ErreurSyntaxe(Exception):
    def __init__(self, position):
        self.position = position


class Nombre:
    def __init__(self, valeur):
        self.valeur = valeur

    def evaluer(self):
        return self.valeur


class Operation:
    def __init__(self, operateur, gauche, droite):
        self.operateur = operateur
        self.gauche = gauche
        self.droite = droite

    def evaluer(self):
        a = self.gauche.evaluer()
        b = self.droite.evaluer()
        if self.operateur == "+":
            return (a + b) % MODULE
        if self.operateur == "-":
            return ((a - b) % MODULE + MODULE) % MODULE
        return a * b % MODULE


class Lexeme:
    def __init__(self, genre, valeur, position):
        self.genre = genre
        self.valeur = valeur
        self.position = position


def decouper(texte):
    lexemes = []
    nombre = -1
    position = 0
    for caractere in texte:
        code = ord(caractere)
        if code >= 48 and code <= 57:
            if nombre < 0:
                nombre = 0
            nombre = nombre * 10 + code - 48
        else:
            if nombre >= 0:
                lexemes.append(Lexeme("n", nombre, position))
                nombre = -1
            if code != 32:
                lexemes.append(Lexeme(caractere, 0, position))
        position = position + 1
    if nombre >= 0:
        lexemes.append(Lexeme("n", nombre, position))
    lexemes.append(Lexeme("fin", 0, position))
    return lexemes


class Analyseur:
    def __init__(self, lexemes, position):
        self.lexemes = lexemes
        self.position = position

    def courant(self):
        return self.lexemes[self.position]

    def avancer(self):
        self.position = self.position + 1


def expression(analyseur):
    gauche = terme(analyseur)
    while analyseur.courant().genre == "+" or analyseur.courant().genre == "-":
        operateur = analyseur.courant().genre
        analyseur.avancer()
        droite = terme(analyseur)
        gauche = Operation(operateur, gauche, droite)
    return gauche


def terme(analyseur):
    gauche = facteur(analyseur)
    while analyseur.courant().genre == "*":
        analyseur.avancer()
        droite = facteur(analyseur)
        gauche = Operation("*", gauche, droite)
    return gauche


def facteur(analyseur):
    lexeme = analyseur.courant()
    if lexeme.genre == "n":
        analyseur.avancer()
        return Nombre(lexeme.valeur)
    if lexeme.genre == "(":
        analyseur.avancer()
        interieur = expression(analyseur)
        if analyseur.courant().genre != ")":
            raise ErreurSyntaxe(analyseur.courant().position)
        analyseur.avancer()
        return interieur
    raise ErreurSyntaxe(lexeme.position)


def analyser(texte):
    analyseur = Analyseur(decouper(texte), 0)
    arbre = expression(analyseur)
    if analyseur.courant().genre != "fin":
        raise ErreurSyntaxe(analyseur.courant().position)
    return arbre


def generer(profondeur, hasard):
    if profondeur == 0 or hasard.suivant(4) == 0:
        return str(hasard.suivant(100))
    operateurs = ["+", "-", "*"]
    gauche = generer(profondeur - 1, hasard)
    operateur = operateurs[hasard.suivant(3)]
    droite = generer(profondeur - 1, hasard)
    texte = gauche + " " + operateur + " " + droite
    if hasard.suivant(3) == 0:
        return "(" + texte + ")"
    return texte


def principal():
    hasard = Hasard(7)
    total = 0
    erreurs = 0
    positions = 0
    index = 0
    while index < 4000:
        texte = generer(6, hasard)
        if index % 25 == 0:
            texte = texte + " *"
        try:
            arbre = analyser(texte)
            total = (total + arbre.evaluer()) % MODULE
        except ErreurSyntaxe as erreur:
            erreurs = erreurs + 1
            positions = positions + erreur.position
        index = index + 1
    print(total)
    print(erreurs)
    print(positions)


principal()
