[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)

---

<!-- geist-render: severity=best-effort route=best-effort reason=typed-lowering-declined records=28-34 detail="prose topic rejected: cz FLOW DT term '   /L[+|-]' has no definition" -->

### PREFACE\.2\.1 How to Read the Syntax Diagrams

<a id="HDRSYNTX"></a>

<pre>
   This book uses two methods to show syntax. One is for commands,
   preprocessor directives, and statements; the other is for compiler
   options.
   Syntax for Commands, Preprocessor Directives, and Statements
   °   Read the syntax diagrams from left to right, from top to bottom,
       following the path of the line.
       The &gt;&gt;--- symbol indicates the beginning of a command, directive, or
       statement.
       The ---&gt; symbol indicates that the command, directive, or statement
       syntax is continued on the next line.
       The &gt;--- symbol indicates that a command, directive, or statement is
       continued from the previous line.
       The ---&gt;&lt; symbol indicates the end of a command, directive, or
       statement.
       Diagrams of syntactical units other than complete commands,
       directives, or statements start with the &gt;--- symbol and end with the
       ---&gt; symbol.
       In these diagrams, STATEMENT represents a C or C++ command, directive,
       or statement.
   °   Required items appear on the horizontal line (the main path).
       &gt;&gt;__STATEMENT__required_item________________________________________&gt;&lt;
   °   Optional items appear below the main path.
       &gt;&gt;__STATEMENT__ _______________ ____________________________________&gt;&lt;
                      |_optional_item_|
   °   If you can choose from two or more items, they appear vertically, in a
       stack.
       If you must choose one of the items, one item of the stack appears on
       the main path.
       &gt;&gt;__STATEMENT__ _required_choice1_ _________________________________&gt;&lt;
                      |_required_choice2_|
       If the items are optional, the entire stack appears below the main
       path.
       &gt;&gt;__STATEMENT__ __________________ _________________________________&gt;&lt;
                      |_optional_choice1_|
                      |_optional_choice2_|
       The item that is the default appears above the main path.
                       _default_item___
       &gt;&gt;__STATEMENT__|_alternate_item_|___________________________________&gt;&lt;
   °   An arrow returning to the left above the main line indicates an item
       that can be repeated.
                      &lt;_,_______________
       &gt;&gt;__STATEMENT____repeatable_item_|__________________________________&gt;&lt;
       A repeat arrow above a stack indicates that you can make more than one
       choice from the stacked items, or repeat a single choice.
   °   Keywords appear in nonitalic letters and should be entered exactly as
       shown (pragma).
       Variables appear in italicized lowercase letters (identifier). They
       represent user-supplied names or values.
   °   If punctuation marks, parentheses, arithmetic operators, or other such
       symbols are shown, you must enter them as part of the syntax.
   Note:  The white space is not always required between tokens, but it is
          recommended that you include at least one blank between tokens
          unless specified otherwise.
   This diagram shows the syntax for the #pragma comment directive.  (See the
   C++ Language Reference for information on the #pragma directive.)
   &gt;&gt;__#__pragma__comment__(__ _compiler_____________________________ __)__&gt;&lt;
                              |_date_________________________________|
                              |_timestamp____________________________|
                              |_ _copyright_ __ ___________________ _|
                                |_user______|  |_,'token_sequence'_|
   The syntax diagram is interpreted as:
   °   The &gt;&gt;--- is the start of the syntax diagram
   °   The symbol # must appear first
   °   The keyword pragma must follow the # symbol
   °   The keyword comment must follow the keyword pragma
   °   An opening parenthesis must be present
   °   The comment type must be entered only as one of the types indicated:
       compiler, date, timestamp, copyright, or user
   °   If the comment type is copyright or user, and an optional character
       string follows, a comma must be present after the comment type
   °   A character string must follow the comma
   °   A closing parenthesis is required
   °   The ---&gt;&lt; is the end of the syntax diagram
   The following #pragma comment directives are syntactically correct
   according to the syntax:
        #pragma comment(date)
        #pragma comment(user)
        #pragma comment(copyright,"This text will appear in the module")
   Syntax for Compiler Options
   °   Optional elements are enclosed in square brackets [ ]
   °   When you have a list of items from which you can choose one, the
       logical OR symbol (|) separates the items.
   °   Variables appear in italicized lowercase letters (num).
   Examples
   Syntax    Possible Choices
   /L[+|-]
             /L
             /L+
             /L-
   /Lt"string" /Lt"Listing File for Program Test"
   Note that, for options that use a plus (+) or minus (-) sign, if you do
   not specify a sign, the plus is assumed.  The /L and /L+ options are
   equivalent.
</pre>

---

[Previous](preface-2.md) | [Index](README.md) | [Next](preface-2-2.md)
