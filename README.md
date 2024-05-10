# David's CPMEMU CP/M 2.2 Emulator 

There are dozens of CP/M emulators or Z80 emulators that can run CP/M already.
It is not necessary to create one now.
However, back to 30+ years ago, we do have CP/M emulator for DOS but not fast.
That's why I start this project.
This is an old project, but not open to public till now.

## Version 1 (1988,1989) - CP/M emulator for my Z80 ISA CARD on IBM PC XT/AT (clone)

This program is start at 1988. In that time my PC is a IBM PC XT clone (4.77MHz/8MHz).
I decide to build an ISA CARD for Z80 and implement CP/M emulation in PC.
So, the CP/M program can be executed in my PC.

The design of that card is quite simple. It has a Z80 CPU (up to 20MHz), 64KB SRAM.
then some latches and glue logic for this card, that's all.
The RAM can be accessed by both Z80 and 8088. But one at a time. 8088 is master, Z80 
is slave. 8088 can write a special IO address to halt Z80 CPU and gain the RAM access,
and release it to Z80, and polling for Z80's state (in halt or not in halt).
Z80 can output a special IO port to half itself.

The emulation is simple. PC load .COM program and environment into RAM on this card (Z80 is halted now),
then release it. So, Z80 start to execute program. When Z80 call BDOS/BIOS, my
BDOS/BIOS emulation layer will store all registers in dedicated address, then halt ifself.
And, when PC see Z80 is in halt state by polling, CPMEMU will access RAM. find out what BDOS/BIOS
call is needed. Then, PC program will do these calls and put the result back to RAM, and
resume Z80's program execution till next BDOS/BIOS call. That's all. 

This program emulate almost all CP/M 2.2 BDOS calls and partial BIOS call. So, it can not run
some programs that need these unemulated calls. (like STAT, PUN support, LST support, DISK support in BIOS...)

This program is implemented by pure C code using Microsoft Quick C 2.0/2.5 or Microsoft C 6.0 ~ 8.0.
And, lots of BDOS call is implemented by MSDOS's INT21h.

The schematics file of this card is done by orcad 1.0 for DOS. And, I can't open these SCH files.
But I have paper copy on hand. So, I put scanned version as well.

After I make everything works, I management to get one 20MHz Z80 sample (I want to buy it from Zilog Taiwan,
but they just sent it to me!) So I'm happy that I have the fastest CP/M machine ever!

## Version 2 (2001) - CP/M emulator and Z80 simulator for DOS
As time go to 2001. My PC-XT is retired and I have a 386/SX machine. But my Z80 card can't work in 
that machine (reason is still unknown. Maybe from wait state generation, or memory mapping). I choose not to 
debug this card. (ISA bus is dying for PC at that age) I try to port Z80 simulator from YAZE-1.10. 
Then CPMEMU can run on 386/486 PC again. 

## Version 3 (2014) - CP/M emulator and Z80 simulator for LINUX
At 2014, I try to porting it to Linux, use standard C library to emulate these BDOS calls.
I have to rewrite all BDOS/BIOS call that use INT10h/INT21h. Using standard C library and console I/O instead.
Then my program can be executed in Linux. However, I do not spent enough time on testing and improve it's 
compatability, just make it able to run some program. 

## Version 4 (2023) - CP/M emulator and Z80 simulator for Win32/64
In 2023, after my retirement, I have time to do whatever I want. So, I restart this project and add these:
* Port it to Win32/64 (this version is Win32/64 only)
* XSUB
* Time measurement
* Z80CPU simulator is upgraded to YAZE-AG 2.51.3's engine

For Win32/64, I use msys2 as development environment. And, I also try Code::blocks with embedded GCC.
But this embedded GCC is lack of GNU Readline library. So, need to remove -DGNU_READLINE to disable 
command line history feature, or configure Code::blocks to use msys2's gcc compiler and libraries.
