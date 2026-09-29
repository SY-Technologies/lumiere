"""The benchmark workloads, and what each one must print.

One table, imported by scripts/benchmark.py and scripts/compare-languages.py.
Both used to carry their own copy, which is one copy too many: a workload whose
expected output drifts in one file and not the other stops being a check and
becomes decoration.

Each entry names a program under benchmarks/ and the exact text it prints. The
Python translation of the same workload lives in benchmarks/python/<name>.py and
prints the same text, so a run that did not do the work is caught rather than
timed.
"""

WORKLOADS = {
    "integer_loop": "499999500000\n",
    "text_iteration": "1487580000\n",
    "function_calls": "14999950000\n",
    "method_calls": "1999999000000\n",
    "text_calls": "vrai\n131072\n",
    "typed_list": "200000\n199999\n",
    "dictionary_lookup": "50000\n1249975000\n",
    "wide_object": "15500000\n",

    # Programs rather than probes. Each of the eight above isolates one
    # operation, which is how a cost is found and how a fix is shown to
    # work; none of them says what an ordinary program spends its time on.
    # These three are written the way such a program would be -- a log
    # analysis, an expression parser, an order-processing service -- and
    # read the target off something closer to use.
    "journal": ("INFO 26634\n"
                "ERREUR 6618\n"
                "WARN 6748\n"
                "/api/produits 5670 126 252\n"
                "/sante 5652 127 252\n"
                "/connexion 5803 127 252\n"
                "/api/clients 5753 128 252\n"
                "/api/commandes 5797 127 252\n"
                "/api/stocks 5680 127 252\n"
                "/api/factures 5645 126 252\n"
                "utilisateurs 2000\n"
                "erreurs 4034\n"),
    "expressions": ("16730\n"
                    "160\n"
                    "15039\n"),
    "commandes": ("acceptées 28943 pour 974552805\n"
                  "refus inconnu 1888\n"
                  "refus stock 2370\n"
                  "refus vide 6799\n"
                  "standard 510679206\n"
                  "fidele 246926255\n"
                  "pro 216947344\n"
                  "manque 4776\n"
                  "stock 10961\n"),
}

# The only workload with a compiled reference. A C version of the others would
# be a different program -- there is no dictionary or Unicode string in the C
# standard library -- and a comparison between different programs says nothing.
SCALAR_WORKLOAD = "integer_loop"
