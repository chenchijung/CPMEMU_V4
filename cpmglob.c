/************************************************************************/
/*                                                                      */
/*             CP/M Hardware Emulator Card Support Program              */
/*                         CPMGLOB.C Ver 1.51                           */
/*                 Copyright (c) By C.J.Chen NTUEE 1988                 */
/*                         All Right Reserved                           */
/*                                                                      */
/************************************************************************/
#include <stdio.h>
#include <ctype.h>
//#include <bios.h>
//#include <dos.h>
#include <stdlib.h>
//#include <direct.h>
//#include <process.h>
#include <setjmp.h>
#include <string.h>
#include <memory.h>
//#include <conio.h>
#include <signal.h>

#include "cpmemu.h"

/* Z80 Registers backup for BDOS/BIOS Call */
UINT8 *bioscode;
UINT8 *bdoscall;
UINT8 *eop;

UINT8 *rega;
UINT8 *regb;
UINT8 *regc;
UINT8 *regd;
UINT8 *rege;
UINT8 *regh;
UINT8 *regl;

UINT16 *regaf;
UINT16 *regbc;
UINT16 *regde;
UINT16 *reghl;
UINT16 *regix;
UINT16 *regiy;
UINT16 *regsp;
UINT16 *regip;

/* FCB Pointers for BDOS call */
UINT8 *fcbdn;
UINT8 *fcbfn;
UINT8 *fcbft;
UINT8 *fcbrl;
UINT8 *fcbrc;
UINT8 *fcbcr;
UINT8 *fcbln;

UINT16 dmaaddr = 0x80;

//#ifdef MSC
//  UINT16 ctrl_c_seg, ctrl_c_offset;
//  UINT16 stack_segment;

//#endif
//#ifdef DJGPP
void *ctrl_c_offset;
//#endif

jmp_buf ctrl_c;


FILE *fcbfile[MAXFILE];
UINT32 fcbfilelen[MAXFILE];
UINT8 fcbused[MAXFILE][15];
UINT8 unused[128]; // debug add buffer to prevent overflow
FILE *lpt = NULL;
FILE *stream = NULL; // BDOS & BIOS output stream
UINT8 bdosdbgflag[BDOSDBGFLAGS]; // bdos debug print flag

char DebugFlag = 0;
UINT8 lastcall = 0xFF;
UINT16 repeats = 1;
char debugmess1[50];
char debugmess2[50];
UINT8 in_bios = 0;

char Z80DebugFlag = 0;
FILE *Z80Trace = NULL;

#if 0 // old one, bios JMP table not in 0xXX00
char cpmhex[] =
    ":08000000C37BFE0000C300FEFB\n"
    ":10FE0000CD13FE3E013281FFD3F03E003281FFCDA3\n"
    ":10FE10004CFEC9ED4384FFED5386FFDD228AFFFDD2\n"
    ":10FE2000228CFFED738EFF2288FFE1D1D5E5ED53E3\n"
    ":10FE300090FFF5E12282FF08D9F5E12293FFED431F\n"
    ":10FE400095FFED5397FF2299FF08D9C92A82FF7CBD\n"
    ":10FE5000ED4B84FFED5B86FF2A88FFDD2A8AFFFDDC\n"
    ":10FE60002A8CFFED7B8EFF08D92A93FF7CED4B9502\n"
    ":10FE7000FFED5B97FF2A99FF08D9C9C3A8FEC3BA53\n"
    ":10FE8000FEC3C3FEC3CCFEC3D8FEC3E4FEC3F0FE74\n"
    ":10FE9000C3F9FEC301FFC310FFC31CFFC328FFC388\n"
    ":10FEA00037FFC340FFC349FF21FFFFF93E023A0479\n"
    ":10FEB00000E60F4FCD52FFC358FF3E03CD52FF3A2D\n"
    ":10FEC00083FFC93E04CD52FF3A83FFC9F5793283DF\n"
    ":10FED000FF3E05CD52FFF1C9F5793283FF3E06CDD5\n"
    ":10FEE00052FFF1C9F5793283FF3E07CD52FFF1C9C8\n"
    ":10FEF0003E08CD52FF3A83FFC9F53E09CD52FFF1CE\n"
    ":10FF0000C9F5793283FF3E0ACD52FF2A88FFF1C935\n"
    ":10FF1000F5793283FF3E0BC352FFF1C9F579328385\n"
    ":10FF2000FF3E0CC352FFF1C9E5F560692284FF3E34\n"
    ":10FF30000DCD52FFF1E1C93E0ECD52FF3A83FFC90C\n"
    ":10FF40003E0FCD52FF3A83FFC93E10CD52FF3A8398\n"
    ":10FF5000FFC93292FFD3F0C93E013280FFD3F0CD0A\n"
    ":09FF6000000121FFFFF9C358FF65\n"
    ":10FF80000000000000000000000000000000000071\n"
    ":0BFF9000000000000000000000000066\n"
    ":0000000000\n";
#else // new one. BIOS JMP table in 0xFE00
char cpmhex[] =
    ":08000000C303FF0000C300FE72\n"
    ":20FE0000CD13FE3E013281FFD3F03E003281FFCD4CFEC9ED4384FFED5386FFDD228AFFFD83\n"
    ":20FE2000228CFFED738EFF2288FFE1D1D5E5ED5390FFF5E12282FF08D9F5E12293FFED4330\n"
    ":20FE400095FFED5397FF2299FF08D9C92A82FF7CED4B84FFED5B86FF2A88FFDD2A8AFFFDE7\n"
    ":20FE60002A8CFFED7B8EFF08D92A93FF7CED4B95FFED5B97FF2A99FF08D9C921FFFFF93E53\n"
    ":20FE8000023A0400E60F4FCD5AFFC360FF3E03CD5AFF3A83FFC93E04CD5AFF3A83FFC9F5C8\n"
    ":20FEA000793283FF3E05CD5AFFF1C9F5793283FF3E06CD5AFFF1C9F5793283FF3E07CD5A1E\n"
    ":20FEC000FFF1C93E08CD5AFF3A83FFC9F53E09CD5AFFF1C9F5793283FF3E0ACD5AFF2A881A\n"
    ":1BFEE000FFF1C9F5793283FF3E0BC35AFFF1C9F5793283FF3E0CC35AFFF1C9CB\n"
    ":20FF0000C37BFEC37BFEC38DFEC396FEC39FFEC3ABFEC3B7FEC3C3FEC3CCFEC3D4FEC3E334\n"
    ":20FF2000FEC3EFFEC330FFC33FFFC348FFC351FFE5F560692284FF3E0DCD5AFFF1E1C93E71\n"
    ":20FF40000ECD5AFF3A83FFC93E0FCD5AFF3A83FFC93E10CD5AFF3A83FFC93292FFD3F0C9A8\n"
    ":11FF60003E013280FFD3F0CD000121FFFFF9C360FFD5\n"
    ":1BFF800000000000000000000000000000000000000000000000000000000066\n"
    ":00000001FF\n";

#endif