This is a clone of the interpreter code base used for the bachelor thesis "Extending Shell Scripting with Cooperative Multitasking".

The code here includes the changes made in chapter 5 for the sake of investigating the implications of automatic management of character streams/file handles.

# Building

The interpreter should be able to be built for and UNIX (like) system. There is currently no support for Windows, though building without the UNIX section of the stdlib may be possible.  
GFLW3 is required to build the interpreter with the default standard library configuration.

To make a release build (which was used for all experiments) run ``make release``. The interpreter executable will then be put into bin/esh.
