# Contributing to crau-nbd

Thank you for your interest in contributing to `crau-nbd`!

## Standards & Style Guidelines

This project strictly adheres to:
1. **IEEE Std 1003.1 (POSIX.1)**: All system interfaces, file descriptor operations, signal handlers, and error handlers must follow POSIX.1 semantics.
2. **GNU Coding Standards**:
   - Error messages must follow: `program: error: message` (lowercase first letter, no period).
   - Options and syntax must follow GNU getopt guidelines.
   - Code formatting adheres to standard GNU/LLVM indentation.

## Commit Guidelines

Format your commit messages using the standard subsystem prefix:
```text
subsystem: brief imperative description

Detailed explanation of why the change was made and any technical
trade-offs involved.
```

## Submitting Patches

Open a Pull Request or format patches using `git format-patch` and send them to the maintainers listed in `AUTHORS`.
