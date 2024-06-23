

# VimTogether (In Development)

A lightweight, terminal based task editor. 

## Table of Contents

- [1. Project Statement](#1-project-problem-and-solution-statement)
- [2. High Level Design Document](#2-high-level-design-overview)
- [3. Usage](#3-usage)
- [4. Documentation](#4-documentation)


## 1. Project Problem and Solution Statement 

Vim is a dated yet still beloved text editor known for its quick editing speeds, convenient (though complicated keyboard shortcuts), and simplicity.

VimTogether aims to replicate Vim's original functionality and improve upon the base model by: 
- simplifying complex keyboard shortcuts 
- adding live collaboration via the cloud, similar to a Google Docs
- providing live spell check and auto complete, fueled by LLM


## 2. High Level Design Overview

- [Requirements](#requirements)
- [Key Components](#key-components-of-the-terminal)
- [Github Flow Diagram](#github-flow-diagram)
- [Testing](#testing)
- [System Architecture](#system-architecture)

### Requirements

Here we have a list of required features for the final product.

1. To be able to open, read, and write text to files of any programming language.
2. To provide convenient and useful keybinds to streamline editing files.
3. To provide a visually appealing frontend complete with language specific syntax highlighting.
4. To allow for live, asynchronous collaborative editing to a single file.

### Key Components of the Terminal

The documentation site can be accessed in `\Documentation\html`. Detailed descriptions of the source code can be found [here](#4-documentation).


### Github Flow Diagram

- No features in progress yet

### Testing

- In Progress


### System Architecture 

- In Progresss


## 3. Usage

### Installation

1. Clone this repository locally `git clone`
2. From the root dir run `make` to compile `kilo.c`
3. Run `./kilo` to create a new file
4. Optionally, move an existing file to the root dir and run `./kilo filename` to open and edit an existing file


### Keyboard Shortcut Reference

- In Progress

## 4. Documentation

Developed and Hosted by Doxygen. [Documentation](Documentation/latex/refman.pdf)

This project has been in Development since June 1, 2024.