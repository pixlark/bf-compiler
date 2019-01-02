# Brainfuck Compiler

bf is a little compiler that turns the esoteric programming langage Brainfuck into x86_64 assembly.

## Basic Usage

```
$ ./bf brainfuck_source.bf output_asm.s
$ gcc output_asm.s -o my_brainfuck_program
$ ./my_brainfuck_program
```
