# General coding style

- Variable naming: _lower_case_underscored_
- Try to aim for lean and elegant code, reuse existing code paths when available.
- Avoid `#defines`, use constant variables instead, allow macros for conditional compilation and repetitive API declarations where they help.
- Put curly-brackets (`{` and `}`) on the next line.
- Try to eliminate all compiler warnings from your code.
- Try to use C++ standard data types whenever it's possible (e.g. _std::string_ instead of _QString_).
- Comment _every_ hack you do and _every_ instance of improvable code.
- Don't use `/**/` for commenting out multiple lines. Use `//` on every line instead.
- Ensure that every source file you modify has the newline at the end of file. Every line ends with "newline" and the end of file must have "newline" too, GitHub usually warns about it.
- Use brackets around multi-term ternary operator conditions especially if they occur with other code on the same line. `(x * y) + ((a > b)? c : d)` is more readable than `x * y + a > b? c : d`.
- Avoid recursive locking, there are always better alternatives.
