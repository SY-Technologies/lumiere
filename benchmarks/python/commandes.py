# Les refus sont des exceptions ici, là où le programme Lumière rend un
# Résultat : chaque langage avec son mécanisme d'erreur, puisque c'est lui
# qu'un programme ordinaire emploie.


class Hasard:
    def __init__(self, etat):
        self.etat = etat

    def suivant(self, borne):
        self.etat = (self.etat * 1103515245 + 12345) % 2147483648
        return self.etat // 65536 % borne


class Produit:
    def __init__(self, reference, prix, stock):
        self.reference = reference
        self.prix = prix
        self.stock = stock


class Ligne:
    def __init__(self, reference, quantite):
        self.reference = reference
        self.quantite = quantite


class Client:
    def __init__(self, nom, categorie):
        self.nom = nom
        self.categorie = categorie


class Commande:
    def __init__(self, numero, client, lignes):
        self.numero = numero
        self.client = client
        self.lignes = lignes


class ErreurProduitInconnu(Exception):
    def __init__(self, reference):
        self.reference = reference


class ErreurStockInsuffisant(Exception):
    def __init__(self, reference, manque):
        self.reference = reference
        self.manque = manque


class ErreurCommandeVide(Exception):
    def __init__(self, numero):
        self.numero = numero


def remise(client, montant):
    if client.categorie == "pro":
        return montant * 15 // 100
    if client.categorie == "fidele":
        return montant * 5 // 100
    return 0


def traiter(commande, catalogue):
    if len(commande.lignes) == 0:
        raise ErreurCommandeVide(commande.numero)

    demande = {}
    for ligne in commande.lignes:
        if ligne.reference not in catalogue:
            raise ErreurProduitInconnu(ligne.reference)
        if ligne.reference in demande:
            demande[ligne.reference] = demande[ligne.reference] + ligne.quantite
        else:
            demande[ligne.reference] = ligne.quantite
    for reference in demande:
        disponible = catalogue[reference].stock
        if disponible < demande[reference]:
            raise ErreurStockInsuffisant(reference, demande[reference] - disponible)

    montant = 0
    for reference in demande:
        produit = catalogue[reference]
        produit.stock = produit.stock - demande[reference]
        montant = montant + produit.prix * demande[reference]
    return montant - remise(commande.client, montant)


def principal():
    hasard = Hasard(2026)
    catalogue = {}
    index = 0
    while index < 300:
        reference = "REF-" + str(index)
        catalogue[reference] = Produit(reference, 100 + hasard.suivant(9900), 5 + hasard.suivant(20))
        index = index + 1

    categories = ["standard", "standard", "fidele", "pro"]
    chiffre = 0
    acceptees = 0
    manque_total = 0
    refus = {"inconnu": 0, "stock": 0, "vide": 0}
    par_categorie = {"standard": 0, "fidele": 0, "pro": 0}

    numero = 0
    while numero < 40000:
        if numero % 500 == 0:
            for reference in catalogue:
                produit = catalogue[reference]
                produit.stock = produit.stock + 9

        lignes = []
        nombre_lignes = hasard.suivant(6)
        l = 0
        while l < nombre_lignes:
            lignes.append(Ligne("REF-" + str(hasard.suivant(306)), 1 + hasard.suivant(4)))
            l = l + 1
        identifiant = hasard.suivant(500)
        client = Client("client" + str(identifiant), categories[identifiant % 4])
        commande = Commande(numero, client, lignes)

        try:
            montant = traiter(commande, catalogue)
            chiffre = chiffre + montant
            acceptees = acceptees + 1
            par_categorie[client.categorie] = par_categorie[client.categorie] + montant
        except ErreurProduitInconnu:
            refus["inconnu"] = refus["inconnu"] + 1
        except ErreurStockInsuffisant as e:
            refus["stock"] = refus["stock"] + 1
            manque_total = manque_total + e.manque
        except ErreurCommandeVide:
            refus["vide"] = refus["vide"] + 1
        numero = numero + 1

    stock_restant = 0
    for reference in catalogue:
        stock_restant = stock_restant + catalogue[reference].stock
    print("acceptées " + str(acceptees) + " pour " + str(chiffre))
    for raison in refus:
        print("refus " + raison + " " + str(refus[raison]))
    for categorie in par_categorie:
        print(categorie + " " + str(par_categorie[categorie]))
    print("manque " + str(manque_total))
    print("stock " + str(stock_restant))


principal()
