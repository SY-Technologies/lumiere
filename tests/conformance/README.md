# Corpus de conformité

Ce dossier, avec `tests/fixtures/interpreter` et `examples`, est exécuté par
`scripts/conformance`. Chaque cas est posé deux fois, aux deux moteurs, et on lui
pose deux questions distinctes :

1. **Fait-il ce qui est écrit ?** C'est le rôle des fichiers d'attente, et c'est
   ce que vérifie n'importe quelle suite de tests.
2. **Les deux moteurs font-ils la même chose ?** Cette question-là se pose même
   sans fichier d'attente, et c'est la seule qu'un test à un seul moteur ne peut
   pas poser. Le tree-walker et la VM détectent les mêmes situations dans du code
   sans rapport l'un avec l'autre ; sans cette comparaison, ils dérivent.

## Forme d'un cas

Un cas est un dossier contenant `main.lum`, ou un simple fichier `.lum`.

| Fichier | Effet |
| --- | --- |
| `main.lum` | le programme exécuté |
| `stdin.txt` | entrée fournie au programme |
| `expected.stdout` | sortie standard attendue, au caractère près |
| `expected.stdout.contains` | fragment devant apparaître dans la sortie |
| `expected.stderr` | sortie d'erreur attendue, au caractère près |
| `expected.stderr.contains` | fragment devant apparaître dans l'erreur |
| `skip.tw`, `skip.vm` | ignorer ce moteur, avec la raison en contenu |
| `divergence.connue` | différence documentée entre les moteurs, avec sa raison |

Sans attente sur la sortie d'erreur, le programme doit réussir ; avec une, il
doit échouer. Les chemins sont normalisés avant comparaison : ni la machine qui a
écrit l'attente ni celle qui exécute le cas n'est une propriété du langage.

## `divergence.connue`

Une différence que l'on connaît et que l'on n'a pas encore corrigée. Le fichier
dit laquelle et pourquoi. Tant qu'elle existe, le cas passe ; **le jour où les
deux moteurs s'accordent, le cas échoue** afin que la note soit retirée plutôt
que de survivre au problème qu'elle décrivait.

Ce n'est pas un endroit où ranger ce qui gêne : une divergence qui y entre doit
aussi apparaître dans `IMPROVEMENT_TASKS.md`.

## Ajouter un cas

Écrire le programme, décider ce qu'il *doit* afficher, puis vérifier que les deux
moteurs le font. Ne jamais enregistrer la sortie observée sans l'avoir lue : un
corpus qui fige le comportement actuel ne teste rien, il le photographie.
