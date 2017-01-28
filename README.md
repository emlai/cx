# Delta Programming Language

Delta is a general-purpose programming language intended as an alternative to
C++, C, and Rust. The project is currently in very early stages of development.
See the (incomplete) [specification document](doc/spec.md) for a detailed
description of the language.

## Building from source

Building Delta from source requires the following dependencies:
a C++11 compiler, [CMake](https://cmake.org), [Boost](http://www.boost.org),
[Bison](https://www.gnu.org/software/bison/), and
[Flex](https://github.com/westes/flex).

To get started, run the following commands:

    git clone https://github.com/delta-lang/delta.git
    cd delta
    mkdir build
    cd build
    cmake -G "Unix Makefiles" ..

After this, the following commands can be used:

- `make` builds the project.
- `make check` runs the test suite and reports errors in case of failure.