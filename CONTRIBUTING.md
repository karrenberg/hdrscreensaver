# Contributing to HDRScreenSaver

Thank you for your interest in contributing to HDRScreenSaver! This document provides guidelines for contributing to the project.

## Development Setup

1. **Prerequisites**: Follow the build instructions in the main README.md.
2. **Fork the repository** and clone your fork locally.
3. **Set up the XMP Toolkit SDK** as described in the README.md.
4. **Build the project** using CMake.

## Code Style Guidelines

- Keep the code simple. Prefer easy to understand code over fancy or elegant but hard to understand constructs.
- Follow the existing code style and formatting.
- Use meaningful variable and function names.
- Add comments for complex logic.
- Keep functions focused and reasonably sized.

## Debugging and Logging

- Use the `LOG_MSG()` macro for logging.

## Testing

- Test all relevant command line modes (`/c`, `/p`, `/s`, `/x`) and options (e.g. `/preload`).
- The provided test batch files are AI-generated and have never been used ;).

## Submitting Changes

1. **Create a feature branch** from the main branch.
2. **Make your changes** following the guidelines above.
3. **Test thoroughly** on Windows 11 with HDR display.
4. **Commit with descriptive messages**.
5. **Submit a pull request** with a clear description of changes.

## Areas for Improvement

See the TODO section in README.md for current development priorities:

- HDR color accuracy
- Support for more HDR formats, color spaces, and displays
- Preview mode stability
- Dependency reduction

## Questions?

If you have questions about contributing, please open an issue on GitHub. 