[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)

---

<!-- geist-render: severity=best-effort route=best-effort reason=typed-lowering-declined records=28-34 detail="prose topic rejected: placeholder run '??' is followed by visible text at record 29 token 177" -->

### PREFACE\.2\.1 How to Read the Syntax Diagrams

<a id="HDRSYNTX"></a>

```text
   FLOW P 3 3
   This book uses two methods to show syntax. One is for commands,
   preprocessor directives, and statements; the other is for compiler
   options.
   FLOW H4 3 3
   Syntax for Commands, Preprocessor Directives, and Statements
   FLOW UL 3 3
   FLOW LI 3 7
   °   Read the syntax diagrams from left to right, from top to bottom,
       following the path of the line.
   FLOW P 7 7
       The >>--- symbol indicates the beginning of a command, directive, or
       statement.
   FLOW P 7 7
       The ---> symbol indicates that the command, directive, or statement
       syntax is continued on the next line.
   FLOW P 7 7
       The >--- symbol indicates that a command, directive, or statement is
       continued from the previous line.
   FLOW P 7 7
       The --->< symbol indicates the end of a command, directive, or
       statement.
   FLOW P 7 7
       Diagrams of syntactical units other than complete commands,
       directives, or statements start with the >--- symbol and end with the
       ---> symbol.
   FLOW P 7 7
       In these diagrams, STATEMENT represents a C or C++ command, directive,
       or statement.
   FLOW LI 3 7
   °   Required items appear on the horizontal line (the main path).
       >>__STATEMENT__required_item________________________________________><
   FLOW LI 3 7
   °   Optional items appear below the main path.
       >>__STATEMENT__ _______________ ____________________________________><
                      |_optional_item_|
   FLOW LI 3 7
   °   If you can choose from two or more items, they appear vertically, in a
       stack.
   FLOW P 7 7
       If you must choose one of the items, one item of the stack appears on
       the main path.
       >>__STATEMENT__ _required_choice1_ _________________________________><
                      |_required_choice2_|
   FLOW P 7 7
       If the items are optional, the entire stack appears below the main
       path.
       >>__STATEMENT__ __________________ _________________________________><
                      |_optional_choice1_|
                      |_optional_choice2_|
   FLOW P 7 7
       The item that is the default appears above the main path.
                       _default_item___
       >>__STATEMENT__|_alternate_item_|___________________________________><
   FLOW LI 3 7
   °   An arrow returning to the left above the main line indicates an item
       that can be repeated.
   OFF SYNTAX
                      <_,_______________
       >>__STATEMENT____repeatable_item_|__________________________________><
   FLOW P 7 7
       A repeat arrow above a stack indicates that you can make more than one
       choice from the stacked items, or repeat a single choice.
   FLOW LI 3 7
   °   Keywords appear in nonitalic letters and should be entered exactly as
       shown (pragma).
   FLOW P 7 7
       Variables appear in italicized lowercase letters (identifier). They
       represent user-supplied names or values.
   FLOW LI 3 7
   °   If punctuation marks, parentheses, arithmetic operators, or other such
       symbols are shown, you must enter them as part of the syntax.
   FLOW NT 3 10
   Note:  The white space is not always required between tokens, but it is
          recommended that you include at least one blank between tokens
          unless specified otherwise.
   FLOW P 3 3
   This diagram shows the syntax for the #pragma comment directive.  (See the
   C++ Language Reference for information on the #pragma directive.)
   >>__#__pragma__comment__(__ _compiler_____________________________ __)__><
                              |_date_________________________________|
                              |_timestamp____________________________|
                              |_ _copyright_ __ ___________________ _|
                                |_user______|  |_,'token_sequence'_|
   FLOW P 3 3
   The syntax diagram is interpreted as:
   FLOW UL 3 3
   FLOW LI 3 7
   °   The >>--- is the start of the syntax diagram
   FLOW LI 3 7
   °   The symbol # must appear first
   FLOW LI 3 7
   °   The keyword pragma must follow the # symbol
   FLOW LI 3 7
   °   The keyword comment must follow the keyword pragma
   FLOW LI 3 7
   °   An opening parenthesis must be present
   FLOW LI 3 7
   °   The comment type must be entered only as one of the types indicated:
       compiler, date, timestamp, copyright, or user
   FLOW LI 3 7
   °   If the comment type is copyright or user, and an optional character
       string follows, a comma must be present after the comment type
   FLOW LI 3 7
   °   A character string must follow the comma
   FLOW LI 3 7
   °   A closing parenthesis is required
   FLOW LI 3 7
   °   The --->< is the end of the syntax diagram
   FLOW P 3 3
   The following #pragma comment directives are syntactically correct
   according to the syntax:
        #pragma comment(date)
        #pragma comment(user)
        #pragma comment(copyright,"This text will appear in the module")
   FLOW H4 3 3
   Syntax for Compiler Options
   FLOW UL 3 3
   FLOW LI 3 7
   °   Optional elements are enclosed in square brackets [ ]
   FLOW LI 3 7
   °   When you have a list of items from which you can choose one, the
       logical OR symbol (|) separates the items.
   FLOW LI 3 7
   °   Variables appear in italicized lowercase letters (num).
   FLOW H4 3 3
   Examples
   FLOW DL 3 3
   Syntax    Possible Choices
   FLOW DT 3 13
   /L[+|-]
             /L
             /L+
             /L-
   FLOW DT 3 13
   /Lt"string" /Lt"Listing File for Program Test"
   FLOW P 3 3
   Note that, for options that use a plus (+) or minus (-) sign, if you do
   not specify a sign, the plus is assumed.  The /L and /L+ options are
   equivalent.
   FLOW H3 3 3
```

---

[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)
