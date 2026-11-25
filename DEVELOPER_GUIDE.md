# ntksh Developer Guide

The complete and definitive Architecture & Developer Guide for **ntksh** is available in:

👉 **[Architecture & Developer Guide](architecture_and_developer_guide.md)**

It covers:
- Executive Summary & Architectural Philosophy
- High-Level Architecture & Subsystem Decomposition
- Memory Management (`KshArena` and heap lifecycle)
- Lexing, Parsing & Abstract Syntax Tree (`CommandAST`)
- Process Execution & Win32 Interoperability Layer
- In-Process Windows Batch (`.bat` / `.cmd`) Runtime Engine
- Embedded Coreutils & Built-in Dispatch Engine
- Dynamic Module System & C Plugin ABI (`ksh/module.h`)
- Step-by-Step Developer Guides (Adding built-ins, help, completion, tests)
- Build Pipeline, Amalgamation, Hardened Exploit Mitigations & Signing
- Testing, Internal Self-Test Harness & Adversarial Fuzzing
- Coding Standards & Contribution Guidelines
