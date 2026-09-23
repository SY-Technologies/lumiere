class Hasard:
    def __init__(self, etat):
        self.etat = etat

    def suivant(self, borne):
        self.etat = (self.etat * 1103515245 + 12345) % 2147483648
        return self.etat // 65536 % borne


class Statistique:
    def __init__(self, appels, duree_totale, duree_max):
        self.appels = appels
        self.duree_totale = duree_totale
        self.duree_max = duree_max

    def enregistrer(self, duree):
        self.appels = self.appels + 1
        self.duree_totale = self.duree_totale + duree
        if duree > self.duree_max:
            self.duree_max = duree


def produire(nombre, hasard):
    niveaux = ["INFO", "INFO", "INFO", "INFO", "WARN", "ERREUR"]
    methodes = ["GET", "GET", "GET", "POST", "PUT", "DELETE"]
    chemins = ["/api/commandes", "/api/clients", "/api/produits", "/sante",
               "/api/factures", "/connexion", "/api/stocks"]
    statuts = [200, 200, 200, 200, 201, 204, 304, 400, 404, 500]
    lignes = []
    index = 0
    while index < nombre:
        seconde = index % 86400
        ligne = "2026-09-23T" + str(seconde // 3600) + ":" + str(seconde // 60 % 60) + ":" + str(seconde % 60)
        ligne = ligne + " " + niveaux[hasard.suivant(6)] + " " + methodes[hasard.suivant(6)]
        ligne = ligne + " " + chemins[hasard.suivant(7)] + " " + str(statuts[hasard.suivant(10)])
        ligne = ligne + " " + str(3 + hasard.suivant(250)) + " utilisateur" + str(hasard.suivant(2000))
        lignes.append(ligne)
        index = index + 1
    return lignes


def analyser(lignes):
    par_niveau = {}
    par_chemin = {}
    utilisateurs = set()
    erreurs_serveur = 0

    for ligne in lignes:
        champs = ligne.split(" ")
        niveau = champs[1]
        if niveau in par_niveau:
            par_niveau[niveau] = par_niveau[niveau] + 1
        else:
            par_niveau[niveau] = 1

        chemin = champs[3]
        if chemin not in par_chemin:
            par_chemin[chemin] = Statistique(0, 0, 0)
        par_chemin[chemin].enregistrer(int(champs[5]))

        utilisateurs.add(champs[6])
        statut = int(champs[4])
        if statut >= 500:
            erreurs_serveur = erreurs_serveur + 1

    for niveau in par_niveau:
        print(niveau + " " + str(par_niveau[niveau]))
    for chemin in par_chemin:
        statistique = par_chemin[chemin]
        print(chemin + " " + str(statistique.appels) + " " + str(statistique.duree_totale // statistique.appels)
              + " " + str(statistique.duree_max))
    print("utilisateurs " + str(len(utilisateurs)))
    print("erreurs " + str(erreurs_serveur))


def principal():
    analyser(produire(40000, Hasard(42)))


principal()
