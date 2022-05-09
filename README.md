# smallsh

## About

smallsh (small shell) is a shell that implements a subset of features of well-known shells, such as bash

## Built-with

C

## Requirements

Linux

## Compiling

Compile using gcc and c99 standard

## Usage 

User will be prompted for input with ":"

Command line syntax is as follows:
  command [arg1 arg2 ...] [< input_file] [> output_file] [&] where bracketed[] items are optional

  command - Build-in commands cd, exit, and status, or non-built in shell command
  argument(s) - arguments seperated by spaces, max 512
  input file - for input redirection
  output file - output redirection
  & - run command in background
  
 CTRL-C will only kill foreground processes
 CTRL-Z switches shell to foregound only mode, making the & useless when entered in command
