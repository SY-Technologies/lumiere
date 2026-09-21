# Emplacement des diagnostics d’exécution

Un diagnostic d’exécution doit désigner le token qui nomme la chose décrite par
son message. La colonne est celle du premier caractère de ce token, comme pour
les diagnostics de l’analyseur. Elle ne dépend ni du moteur utilisé, ni de
l’instruction interne qui a découvert l’erreur.

| Famille | Token désigné | Raison |
| --- | --- | --- |
| Symbole absent, affectation ou contrat d’une variable | Nom du symbole | Le message parle de cette liaison. |
| Champ ou méthode absent, privé ou invalide | Nom du membre après `.` | Le receveur peut être valide ; c’est ce membre précis qui échoue. |
| Opération unaire ou binaire | Opérateur | L’échec vient de l’opération appliquée aux opérandes. Cela comprend les divisions par zéro et les débordements. |
| Conversion ou annotation de type | Nom du type cible, ou nom de la valeur annotée quand son contrat est violé | Le message nomme la conversion ou la liaison dont le type est refusé. |
| Argument invalide | Expression de l’argument ; pour un argument nommé, son nom | L’autre argument et la fonction peuvent être valides. |
| Appel impossible, mauvais nombre d’arguments ou argument requis absent | Nom du callable, ou début de l’expression appelée si elle n’a pas de nom | L’erreur porte sur le contrat de cet appel dans son ensemble. |
| Construction d’objet | Nom du champ fourni lorsqu’il est inconnu, dupliqué ou mal typé ; nom de la classe pour un champ requis absent | Le curseur suit l’élément du contrat que le message nomme. |
| Accès ou affectation par indice | Expression de l’indice si sa valeur est invalide ; `[` si le type du receveur ne permet pas l’opération | Une mauvaise clé ou position appartient à l’indice ; un accès impossible appartient à l’opération. |
| Itération | Expression itérée | C’est sa valeur qui ne fournit pas une séquence itérable. |
| Import | Nom du module ou du membre importé | Le message nomme l’import qui n’a pas pu être résolu. |
| Instruction de contrôle ou de résultat | Mot-clé (`retourne`, `propager`, `ignorer`, `agir selon`, etc.) | Le message décrit l’usage de cette instruction. |

Les erreurs internes qui indiquent un bytecode invalide ne sont pas des
diagnostics du programme source. Lorsqu’une erreur d’exécution ne correspond à
aucune ligne du tableau, elle désigne le token qui introduit l’opération, jamais
une parenthèse ou un délimiteur de fin choisi seulement parce qu’il était facile
à conserver dans l’AST.

Les appels natifs reçoivent donc l’emplacement sémantique approprié, pas un
unique « emplacement d’appel » pour toutes leurs erreurs. Si une fonction native
rejette son deuxième argument, les deux moteurs doivent lui transmettre le
début de ce deuxième argument. Si elle échoue dans l’opération elle-même, ils lui
transmettent le callable ou le nom du membre.

Cette règle s’applique aussi aux images de pile : l’image d’un appel désigne le
callable invoqué. La ligne où l’erreur a été détectée et les images qui montrent
comment elle a été atteinte restent deux emplacements distincts.
