# ABOUT FINE DIRECTORY PROGRAM
author: George Kuegler
created: 06/22/2022

Find a subdirectory path that contains the search pattern. Can use Regex or plain text search patterns.

Click on a result to open a new file explorer window to the directory path.

**Searches are always case insensitive.**
**The full directory path will be matched against by the search pattern, not just folder names.**

## EXAMPLES

This tool can be used to quickly search the KA archive folders.

set the directory to -> \\192.168.1.20\CTroot\Archives
select "recursively search child directories"
set the recursion depth to -> 2

You can search for "hospital" to find all projects with hospital in the name.
You can even leverage regex to catch common spelling mistakes such as "hospital" vs "hospitol".
Use the search pattern of "hospit(a|o)l" to match both versions.
An alternative would be to just search for "hosp" since not many english words contain the phrase "hosp".

Or, one can search for "dunk" to find all dunkin' donuts projects.

Sometimes when searching for project, the job number is roughly known.
For example, if I know the job number started with an "A" and the name contained "school", that I can filter results accordingly by using the following search pattern: A\d+\.*school

This will match all folder names containing an "A" followed by at least 1 number, then an arbitrary number of characters, and then the sequence of characters "school".

## OPTIONS

### text search

Text search searches for the literal sequence of characters in the search pattern.
Use this option if you have no interest in leveraging regex.

No speed advantage is gained by using the text option as this is implemented by excaping all special symbols.

Note for the adventurous: Make sure to check the "text search option" if using the following symbols litterally:
., +, *, ?, ^, $, (, ), [, ], {, }, |, or \
Otherwise, for all letters, numbers, and a "-", regex mode with behave the same as text mode.

### recursively search child directors

Check this option to search subdirectory names as well.
The adjacent text entry limits the recursion depth.

example:
0     = Unlimited Depth
1     = Only The Directory Specified (the same as not checking recursivly searching child directories)
2     = the directory specified, and the next one down
...
10000 = max custom recursion depth accepted

By default this program only searches the directory specified.

## GENERAL

Some settings will need to be modified by editing the configuration file.
Settings are commonly stored in the same directory with the executable as -> "settings.toml".

By default, the search options will be saved on a successful search.
There currently is no option to disable this behavior.

By default, the application will close when a directory path is selected.
This option can be turned off in the configuration file.

A default directory path can also be set in the configuration file.

To clear directory search history, delete the items from the "bookmarks" configuration file parameter.
