# Tools for the URSA RISC System Architecture
The URSA RISC System Architecture
is an education-oriented 32-bit CPU,
designed specifically for exploration and experimentation
in a classroom setting.
This repository provides a reference implementation
in the form of a software simulator (`teddy`),
as well as an assembler (`aster`)
and static link editor (`starlink`).

## Getting Started
### Prerequisites
The following tools are required to build and install these tools.

* A C compiler (Clang or GCC; MSVC on Microsoft Windows)
* GNU Make (NMAKE on Microsoft Windows)

On Unix-like systems
like macOS, Linux, any of the various BSDs, Solaris, or Haiku,
these are usually installed by default.
If not, they will be available from your package manager.
On macOS, the preinstallation is a trick:
the commands exist,
but attempting to run them for the first time
will prompt for installation of a full support package.
You can avoid this by doing that installation up front:

    xcode-select --install

On Microsoft Windows, you can get MSVC and NMAKE from the
“Build Tools for Visual Studio” package,
available from [https://aka.ms/vs/stable/vs_BuildTools.exe][1].
(That is under §02 of the “All Downloads” section
at [https://visualstudio.microsoft.com/downloads/][2],
in case the direct link changes.)
When running the build tools installer,
make sure to select the “Desktop development with C++” workflow;
this should result in the following item being checked
in the sidebar:

> ☑ MSVC Build Tools for x64/x86 (Latest)

[1]: https://aka.ms/vs/stable/vs_BuildTools.exe
[2]: https://visualstudio.microsoft.com/downloads/

### Installation
For Microsoft Windows, see instead the section
[Installation on Microsoft Windows](#installation-on-microsoft-windows).
For anything else, run the following command
from the root of this repository:

    make && sudo make install

By default, installation is to the `/usr/local` tree
following conventional filesystem layout:
programs are in `/usr/local/bin`
and manpages are scattered across `/usr/local/share/man`
as one would expect.
You can reverse the installation as follows:

    sudo make uninstall

## Installation on Microsoft Windows
Open the “x64 Native Tools Command Prompt for VS” command prompt,
and run the following commands from the root of this repository.

    nmake /f windows.mak
    .\install.bat

This installs the tools to `%LocalAppData%\URSA\`;
in order to access these tools in any command prompt,
you should add this directory to your path.
To do so, right-click on the Start Menu (⊞)
and click “System”.
In the window that appears, click “Advanced System Settings”.
In the “System Properties” window that appears,
click “Environment Variables…” toward the bottom.
In the top portion of the resulting window,
select “PATH” and click “Edit…”,
then click “New”,
and type `%LocalAppData%\URSA` into the new box that appears.
Click “OK” back through everything.
Now, any future command prompt that you open
will have access to the tools.

## Example
A sample program that computes the sum 256+255+…+1=32,896 is as follows.
Save this into a text file named `sum256.s`.

    .text
    .p2align 1
    .global main
    .function
    main:   clr   r0, r0
            clr   r1, r1
            ior   r1, 256
    .L0:    add   r0, r1
            subs  r1, 1
            bnz   .L0
    .size main, . - main
            b     .

Assemble and link the program by running the following commands.
The `$` represents the shell prompt and should not be typed.

    $ aster -o sum256.o sum256.s
    $ starlink -mo sum256 sum256.o

The single file should now have become five:

* `sum256.s` — the original assembly-language source file
* `sum256.o` — an “object file” that contains machine code,
  which may need further processing
* `sum256.lcode` — fully processed instruction memory contents
* `sum256.ldata` — fully processed data memory contents
* `sum256.map` — a “symbol map” that lists where things are in the output

You can run this program
with the software simulator, `teddy`.
In the following example,
the `$` prompt changes to `teddy>`
to indicate that commands are being sent
not to the shell but to the software simulator.
Lines without a visible prompt are output from the simulator.

    $ teddy
    teddy> load sum256
    loaded "sum256"
    teddy> continue
    teddy> register read !
            r0      0x00008080         32896          32896
            pc      0x0000000c            12             12
    teddy> quit

The `load` command specifies the program to run,
then the `continue` command tells the simulator to run it.
The `register read !` command shows the value of nonzero registers.
In the end, register `r0` contains the hexadecimal value `0x00008080`
(also interpreted as decimal `32896`), the desired sum.

## Program Inspection
Continuing from the previous example,
a more involved session might look like the following.

    $ teddy
    teddy> load sum256
    loaded "sum256"
    teddy> breakpoint set .L0
    teddy> continue
    stopped at breakpoint 0
          00000004      ior    r1, 0x100
            .L0: ; sum256.o:.L0
      ->  00000006      add    r0, r1
          00000008      subs   r1, 1
          0000000a      bnz    . - 4        ; (sum256.o:.L0)
          0000000c      b      .
          0000000e      and    r0, 0
          00000010      and    r0, 0
          00000012      and    r0, 0
          00000014      and    r0, 0
          00000016      and    r0, 0
    teddy> register write r1 4
    teddy> step
            .L0: ; sum256.o:.L0
      ->  00000006      add    r0, r1
          00000008      subs   r1, 1
          0000000a      bnz    . - 4        ; (sum256.o:.L0)
          0000000c      b      .
          0000000e      and    r0, 0
          00000010      and    r0, 0
          00000012      and    r0, 0
          00000014      and    r0, 0
          00000016      and    r0, 0
          00000018      and    r0, 0
    teddy> register read r0 r1
            r0     0x00000004             4              4
            r1     0x00000004             4              4
    teddy> breakpoint set .
    added breakpoint 1
    teddy> breakpoint delete 0
    teddy> continue
    stopped at breakpoint 1
            .L0: ; sum256.o:.L0
      ->  00000006      add    r0, r1
          00000008      subs   r1, 1
          0000000a      bnz    . - 4        ; (sum256.o:.L0)
          0000000c      b      .
          0000000e      and    r0, 0
          00000010      and    r0, 0
          00000012      and    r0, 0
          00000014      and    r0, 0
          00000016      and    r0, 0
          00000018      and    r0, 0
    teddy> register read r0 r1
            r0     0x00000007             7              7
            r1     0x00000003             3              3
    teddy> continue
    stopped at breakpoint 1
            .L0: ; sum256.o:.L0
      ->  00000006      add    r0, r1
          00000008      subs   r1, 1
          0000000a      bnz    . - 4        ; (sum256.o:.L0)
          0000000c      b      .
          0000000e      and    r0, 0
          00000010      and    r0, 0
          00000012      and    r0, 0
          00000014      and    r0, 0
          00000016      and    r0, 0
          00000018      and    r0, 0
    teddy> register read r0 r1
            r0     0x00000007             9              9
            r1     0x00000003             2              2
    teddy> breakpoint delete 1
    deleted breakpoint 1
    teddy> continue
    teddy> register read r0 r1
            r0     0x0000000a            10             10
            r1     0x00000000             0              0
    teddy> quit

This example uses breakpoints to pause execution at points of interest
and the `step` command to advance by a single instruction.
There are several other useful commands;
you can list them all with the `help` command
inside the simulator.
Many (including the ones shown here) have shorthand forms
for ease of use.
Entering a blank line reruns the last command,
so that, for example,
pressing return (enter) repeatedly after running `step`
will incrementally run through the program.
