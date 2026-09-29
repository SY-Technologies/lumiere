# Les mêmes programmes en Python

Un programme par mesure, en regard du `.lum` du même nom. Ils sont ici plutôt
que dans le script pour qu'on puisse les lire côte à côte et juger soi-même si
la comparaison est honnête : une mesure entre deux langages ne vaut que si les
deux programmes font le même travail.

Deux règles suivies partout :

- même algorithme, même nombre d'opérations, écrit comme on l'écrirait
  naturellement dans chaque langage. Pas de `range()` là où le programme
  Lumière tient un compteur à la main, pas de compréhension de liste là où il
  appelle `ajouter`.
- même sortie, au caractère près, y compris les mots français (`vrai`), pour
  qu'une exécution qui n'a pas fait le travail soit détectée plutôt que
  chronométrée.
- le programme tourne dans une fonction `principal()`, comme le programme
  Lumière. Écrit au niveau du module, chaque variable Python est une globale,
  cherchée dans un dictionnaire à chaque lecture, alors que les variables d'une
  fonction sont des cases indexées — ce que sont aussi les `soit` d'un
  `principal()` Lumière. Les huit premiers programmes étaient écrits au niveau
  du module, où CPython prenait de 14 % à 75 % de temps en plus : la
  comparaison mesurait ce choix d'écriture, pas les deux interpréteurs.

`scripts/compare-languages.py` apparie chaque `.lum` avec le `.py` de même nom.
