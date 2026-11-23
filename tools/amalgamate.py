#!/usr/bin/env python3
"""
tools/amalgamate.py: Generates single-file amalgamation src/ksh.c (SQLite-style).
"""

import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUTPUT_KSH = os.path.join(ROOT, "src", "ksh.c")

HEADERS_IN_ORDER = [
    "include/ksh/common.h",
    "include/ksh/ast.h",
    "include/ksh/ksh.h",
    "include/ksh/builtins.h",
    "include/ksh/help.h",
    "include/ksh/module.h",
]

SOURCES_IN_ORDER = [
    "src/core/common.c",
    "src/core/env.c",
    "src/core/parser.c",
    "src/core/executor.c",
    "src/core/jobs.c",
    "src/core/repl.c",
    "src/core/module.c",
    "src/batch/batch_expand.c",
    "src/batch/batch_runtime.c",
    "src/batch/batch_pipeline.c",
    "src/core/shell.c",
    "src/builtins/builtins_core.c",
    "src/builtins/builtins_test.c",
    "src/builtins/builtins_cmd.c",
    "src/builtins/builtins_text.c",
    "src/builtins/builtins_regex.c",
    "src/builtins/builtins_grep.c",
    "src/builtins/builtins_sed.c",
    "src/builtins/builtins_awk.c",
    "src/builtins/builtins_diff.c",
    "src/builtins/builtins_file.c",
    "src/builtins/builtins_crypto.c",
    "src/builtins/builtins_system.c",
    "src/builtins/builtins_net.c",
    "src/builtins/builtins_bsd.c",
    "src/builtins/builtins_bsd2.c",
    "src/builtins/builtins_bsd3.c",
    "src/builtins/builtins_bsd4.c",
    "src/builtins/builtins_bsd5.c",
    "src/builtins/builtins_bsd6.c",
    "src/builtins/dispatch.c",
    "src/help/help.c",
    "src/main.c",
]

def main():
    print(f"[AMALGAMATE] Generating {OUTPUT_KSH}...")
    with open(OUTPUT_KSH, "w", encoding="utf-8") as out:
        out.write("""/**
 * ntksh
 * Copyright (C) 2023-2026 PC/OpenSystems. All rights reserved.
 * Licensed under the terms of the BSD-3 Clause License
 *
 * Maintainer: Roberto J. Dohnert, Kelly Wells, Adam Wells, Mary Staats, John Albright
 * Inspired by the AT&T KornShell
 * A minimal Korn Shell implementation for Windows NT systems
 *
 * Single-file amalgamation distribution (SQLite-style target).
 * Generated automatically by tools/amalgamate.py
 */

""")
        # Include headers
        for h_rel in HEADERS_IN_ORDER:
            h_path = os.path.join(ROOT, h_rel)
            out.write(f"\n/* ========================================================================= */\n")
            out.write(f"/* Header: {h_rel:<65} */\n")
            out.write(f"/* ========================================================================= */\n\n")
            with open(h_path, "r", encoding="utf-8") as f:
                for line in f:
                    if line.strip().startswith('#include <ksh/') or line.strip().startswith('#include "ksh/'):
                        continue
                    out.write(line)

        # Include sources
        for s_rel in SOURCES_IN_ORDER:
            s_path = os.path.join(ROOT, s_rel)
            out.write(f"\n/* ========================================================================= */\n")
            out.write(f"/* Source Module: {s_rel:<59} */\n")
            out.write(f"/* ========================================================================= */\n\n")
            with open(s_path, "r", encoding="utf-8") as f:
                for line in f:
                    if line.strip().startswith('#include <ksh/') or line.strip().startswith('#include "ksh/'):
                        continue
                    out.write(line)

    size = os.path.getsize(OUTPUT_KSH)
    print(f"[AMALGAMATE] Successfully generated {OUTPUT_KSH} ({size:,} bytes)")

if __name__ == "__main__":
    main()
