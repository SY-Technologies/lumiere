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
}

# The only workload with a compiled reference. A C version of the others would
# be a different program -- there is no dictionary or Unicode string in the C
# standard library -- and a comparison between different programs says nothing.
SCALAR_WORKLOAD = "integer_loop"
