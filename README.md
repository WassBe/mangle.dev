
![logo](https://i.imgur.com/xkr6uC3.png)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![npm](https://img.shields.io/npm/v/mangledotdev)](https://www.npmjs.com/package/mangledotdev)
[![PyPI](https://img.shields.io/pypi/v/mangledotdev)](https://pypi.org/project/mangledotdev/)
[![gem](https://img.shields.io/gem/v/mangledotdev)](https://rubygems.org/gems/mangledotdev)
![Languages](https://img.shields.io/badge/languages-9-brightgreen.svg)
![GitHub issues](https://img.shields.io/github/issues/WassBe/mangle.dev)
![GitHub stars](https://img.shields.io/github/stars/WassBe/mangle.dev)

[Website](https://www.mangle.dev) - [Releases](https://release.mangle.dev) - [Documentation](https://wass-b.gitbook.io/mangle.dev)

Mangle.dev is a lightweight, cross-language inter-process communication (IPC) framework that enables seamless data exchange between programs written in different programming languages.


> Note that Mangle.dev is currently in Early Access, which means there might be some bugs and errors in either the package or the documentation.

> In this repository, you will find the latest stable version of Mangle.dev (mangledotdev) for each languages.

## What does Mangle.dev do?

Mangle.dev provides a simple and consistent API for sending data from one program to another, regardless of the programming languages involved. Whether you need to call a Python script from a Java application, or process data in Rust from a JavaScript service, Mangle.dev makes it straightforward.

At its core, Mangle.dev works by:
1. **Sending data** from a caller program to a receiver program
2. **Receiving the data** in the receiver program
3. **Sending a response** back to the caller

# Key Features

## Simple API
Just two classes to learn:
- **InputManager**: For sending requests and receiving responses
- **OutputManager**: For receiving requests and sending responses

## Type Preservation
Data types are preserved across language boundaries. Send an integer from Python, receive an integer in Go. Send an array from JavaScript, receive an array in C#.

## No Dependencies (for most languages)
Python, JavaScript, Ruby, and Go implementations have zero external dependencies. They use only standard library features.

## Language Agnostic Protocol
Uses JSON for data serialization, making it universally compatible and easy to debug.

## Error Handling
Built-in error handling with detailed error messages and warnings to help diagnose issues.

# Use Cases

Mangle.dev is ideal for:

- **Microservices communication** - Connect services written in different languages
- **Plugin systems** - Allow plugins to be written in any supported language
- **Legacy integration** - Bridge modern applications with legacy systems
- **Polyglot architectures** - Use the best language for each task
- **Testing** - Test components in isolation across language boundaries
- **Prototyping** - Quickly prototype with the language you're most comfortable with

# Contributing

Contributions welcome! See [CONTRIBUTING.md](CONTRIBUTING.md)

- 🐛 Report bugs
- 💡 Suggest features  
- 🔧 Submit PRs
- 📖 Improve docs
- 🌍 Add language support
