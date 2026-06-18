[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)

---

## PREFACE.2.1 How to Read the Syntax Diagrams

<a id="HDRSYNTX"></a>

This book uses two methods to show syntax. One is for commands, preprocessor directives, and statements; the other is for compiler options. Syntax for Commands, Preprocessor Directives, and Statements

- Read the syntax diagrams from left to right, from top to bottom, following the path of the line.

The >>--- symbol indicates the beginning of a command, directive, or statement.

The ---> symbol indicates that the command, directive, or statement syntax is continued on the next line.

The >--- symbol indicates that a command, directive, or statement is continued from the previous line.

The --->< symbol indicates the end of a command, directive, or statement.

Diagrams of syntactical units other than complete commands, directives, or statements start with the >--- symbol and end with the ---> symbol.

In these diagrams, `STATEMENT` represents a C or C++ command, directive, or statement.

- Required items appear on the horizontal line (the main path). < >> STATEMENT required_item ><
- Optional items appear below the main path. << >> STATEMENT >< optional_item
- < If you can choose from two or more items, they appear vertically, in a stack.

[ If you must choose one of the items, one item of the stack appears on the main path. >> STATEMENT required_choice1 >< required_choice2

If the items are optional, the entire stack appears below the main path. << >> STATEMENT >< optional_choice1 optional_choice2

The item that is the default appears above the main path. default_item >> STATEMENT alternate_item ><

- An arrow returning to the left above the main line indicates an item that can be repeated. ++ >> STATEMENT repeatable_item ><

A repeat arrow above a stack indicates that you can make more than one choice from the stacked items, or repeat a single choice.

- Keywords appear in nonitalic letters and should be entered exactly as shown (pragma).

Variables appear in italicized lowercase letters (identifier). They represent user-supplied names or values.

- If punctuation marks, parentheses, arithmetic operators, or other such symbols are shown, you must enter them as part of the syntax. **<<** Note: The white space is not always required between tokens, but it is recommended that you include at least one blank between tokens unless specified otherwise.

[ This diagram shows the syntax for the #pragma comment directive. (See the C++ Language Reference for information on the #pragma directive.) and >> # pragma comment ( compiler ) >< date timestamp copyright user ,'token_sequence'

The syntax diagram is interpreted as:

- The >>--- is the start of the syntax diagram
- The symbol `#` must appear first
- The keyword pragma must follow the `#` symbol
- The keyword `comment` must follow the keyword pragma
- An opening parenthesis must be present
- [ The comment type must be entered only as one of the types indicated: `compiler,` `date,` `timestamp,` `copyright,` or `user`
- If the comment type is `copyright` or `user`, and an optional character string follows, a comma must be present after the comment type
- A character string must follow the comma
- A closing parenthesis is required
- The --->< is the end of the syntax diagram

The following #pragma comment directives are syntactically correct according to the syntax:

```text
#pragma comment(date)
#pragma comment(user) <<
&         #pragma comment(copyright,"This text will appear in the module")
```

Syntax for Compiler Options

- Optional elements are enclosed in square brackets [ ]
- [ When you have a list of items from which you can choose one, the logical `OR` symbol (|) separates the items.
- Variables appear in italicized lowercase letters (*num*). Examples

**Syntax** **Possible** **Choices** `/L[+|-]` `/L` `/L+` `/L-`

[

``&`` `/Lt"stri`n`g" /`L`t"L`i`sting F`i`le fo`r Program Test"

Note that, for options that use a plus (`+`) or minus (`-`) sign, if you do not specify a sign, the plus is assumed. The `/L` and `/L+` options are equivalent.

---

[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)
