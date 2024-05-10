/************************************************************************/
/*                                                                      */
/*             CP/M Hardware Emulator Card Support Program              */
/*                         CPMEMU.C Ver 1.51                            */
/*                 Copyright (c) By C.J.Chen NTUEE 1988                 */
/*                         All Right Reserved                           */
/*                                                                      */
/************************************************************************/
// # dchen 2014.5
// Porting to 32 bits and 64 bits Unix, and using Posix standard data types
// to avoid porting trouble.
//
// # dchen 2024.2
// Porting back to windows & fix bios console status/fopen for .com file/turbo pascal display
//
//#include <windows.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <memory.h>
#include <signal.h>
#include <unistd.h>

#ifdef GNU_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <synchapi.h>  // for Windows Sleep() function
#include <conio.h> // Windows _kbhit()
#include <direct.h>
#include "cpmemu.h"
#include "cpmglob.h"

#ifdef Z80_YAZE110
#include "simz80_yaze110.h"	// yaze-110 z80 engine
#else
#include "mem_mmu.h"		// yaze-ag z80 engine
#include "simz80.h"		// yaze-ag z80 engine
#include "ytypes.h"		// yaze-ag z80 engine
#endif

static char *hexptr;
UINT8 halt = 0;
static UINT8 doscommand = 1;

FILE *subfile = NULL;
UINT8 xsubflag = FALSE; // support for xsub

static UINT8 checknum;
static jmp_buf cold_start;
static UINT32 timer_start, timer_stop;
static UINT8 timer_flag = 0;

extern WORD pc;
/*----------------------------------------------------------------------*/
void printtitle(void)
{
    printf(PROGRAM_NAME);
    printf(COPYRIGHT);
    printf(COPYRIGHT1);
    printf(COPYRIGHT2);
#ifdef Z80_YAZE110
    printf("Z80 Engine: YAZE 1.10\n");
#else
    printf("Z80 Engine: YAZE-AG 2.51.3\n");
#endif
    printf("TPA Area: 0100H - FDFFH\n\n");
    return;
}

int getc_cpmcmd(void)
{
    int ch;
    //printf("getc_cpmcmd()");
    while (1)          // wait key stroke, pause to reduce CPU utilization
    {
        Sleep(4);     // sleep 4ms
        if (ctrlc_flag)
        {
            printf("^C\n");
            ctrlc_flag = 0;    // clear ctrl-c flag
            longjmp(ctrl_c, 0); // process control-c event
        }
        if (_kbhit()) break;    // exit while when key pressed
    }
    ch = _getch(); // fgetc(fp); // read char
    return (ch);
}

int fgets_cpmcmd(char *buf, int num, FILE *fp)
{
    int i;
    int ch;

    //printf("fgets_cpmcmd()\n");
    for (i = 0; i < num - 1;)
    {
        ch = getc_cpmcmd();
        if (ch == 0x0d) // RETURN
        {
            break;          // exit loop
        }
        else if (ch == 0x08) // backspace
        {
            if (i != 0) i--;
            _putch(ch);
        }
        else
        {
            buf[i++] = ch;
            _putch(ch);
        }
        //printf("ch=0x%x i=%d\n", ch, i);
    }
    buf[i] = 0x00; // add or replace '\n' for end of string char
    //printf("<%s>\n",buf);
    return 1;
}

/*----------------------------------------------------------------------*/
/* use fgets to replace gets, and fix additional '\n' */
/* Since gets is not recommands for current C programming */
/*----------------------------------------------------------------------*/
char *gear_fgets(char *buf, int num, FILE *fp, int ignore)
{
    char *find = 0;

    //printf("gear_fgets()\n");
    //if (!fgets(buf, num, fp))
    if (!fgets_cpmcmd(buf, num, fp))
    {
        return NULL;
    }
    if ((find = strrchr(buf, '\n')))
    {
        *find = '\0';
    }
    //else if(ignore)
    //{
    //    char ch;
    //    while (((ch=fgetc(fp)!=EOF)&&( ch!='\n')));
    //}
    return buf;
}
/*----------------------------------------------------------------------*/
/* This part is related to decode HEX and load to simulated Z80's memory space */
/*----------------------------------------------------------------------*/
char hexfile[1400];
char getchfromcpmhex(void)
{
    char ch;
    ch = *hexptr++;
    //putchar(ch); //#debug
    return(ch);
}
/*----------------------------------------------------------------------*/
char readnibble(void)
{
    char tmp;

    tmp = getchfromcpmhex();
    if (tmp > '9') tmp -= 55;
    else tmp -= 48;
    return(tmp);
}
/*----------------------------------------------------------------------*/
unsigned char readbyte(void)
{
    UINT8 result;

    result = (UINT8)readnibble() * 16;
    result += (UINT8)readnibble();
    checknum += result;
    return(result);
}
/*----------------------------------------------------------------------*/
int hex_readline(void)
{
    char ch;
    UINT16 addr = 0;
    unsigned char data_type;
    unsigned char data_number;
    unsigned char i, tmp;
    unsigned char *ptr;

    checknum = 0;
    if ((ch = getchfromcpmhex()) == EOF)
    {
        //printf("\ninvalid EOF \n"); quit();
        return(EOF);
    }
    if (ch != ':')
    {
        printf("\nInvalid HEX file!\n");
        quit();
    }
    if (( data_number = readbyte() ) == 0) return(EOF);
    addr = readbyte();
    tmp = readbyte() ;
    addr = addr * 256 + tmp;
    if (( data_type = readbyte() ) == 1) return(EOF);
    else if (data_type != 0)
    {
        printf("\nInvalid data type char.\n");
        quit();
    }
    ptr = (unsigned char *)(&ram[addr]);
    for (i = 1 ; i <= data_number ; i++) *ptr++ = readbyte();
    readbyte();             /* read checknumber */
    getchfromcpmhex();      /* read Line Feed char */
    if (checknum != 0)
    {
        printf("\nCheck Sum error!\n");
        quit();
    }
    return(0);
}
/*----------------------------------------------------------------------*/
void loadcpmhex(void)
{
    int result;
    static char hexfile_loaded = 0;
    static char *hexptr_backup;
	FILE *hexfp = NULL;

    if (!hexfile_loaded) // first time call
    {
        hexfile_loaded = 1;
        if ((hexfp = fopen ("cpmemuz8.hex", "r")) != NULL)
        {
            // only read file once. (otherwise it can not load when CD to other directories!)
            printf("Loading CPMEMUZ8.HEX\n");

            fread(hexfile, 1400, 1, hexfp);
            fclose(hexfp);
            hexptr_backup = hexptr = hexfile;
        }
        else
        {
            printf("Loading internal HEX\n");
            hexptr_backup = hexptr = (char *)cpmhex;
        }
    }
    else // 2nd, 3rd,... call
    {
        //printf("Loading HEX\n");
        hexptr = hexptr_backup;
    }
    // Start loading
    do
    {
        result = hex_readline();
    }
    while (result != EOF);
}
/*----------------------------------------------------------------------*/
UINT8 loadcom(char *filename)
{
    char *addr = (char *)(&ram[0x0100]);
    unsigned filelen = 0;
    FILE *fp;

    if (strstr(filename, ".COM") == NULL)
    {
        printf("%s is not a Z80 Execution File\n", filename);
        return(1);
    }
    if ((fp = fopen(filename, "rb")) == NULL)
    {
        printf("Z80 Execution File %s not found\n", filename);
        return(1);
    }
    fseek(fp, 0L, SEEK_END);
    filelen = (unsigned)ftell(fp);
    fseek(fp, 0L, SEEK_SET);
    fread(addr, (size_t)1, (size_t)filelen, fp);
    fclose(fp);
    return(0);
}
/*----------------------------------------------------------------------*/
/* Clear all simulated Z80's memory space to 0 */
/*----------------------------------------------------------------------*/
void clearmem(void)
{
    GotoPC;
    memset(ram, 0, 65536);
    return;
}
/*----------------------------------------------------------------------*/
#if 0 // old implementation -- not easy to understand now!
void fillfcb(char *fcb, char *ptr)
{
    char count = 0;
    char *ptr3;
    char i;

    //printf("fillfcb(0x%p, 0x%p)\n", fcb, ptr);
    ptr3 = fcb;
    *ptr3++ = 0x00;
    for (i = 0 ; i < 11 ; i++) *ptr3++ = 0x20;

    while (*ptr == ' ') ptr++;
    if (*(ptr + 1) == ':')
    {
        *fcb = *ptr - 'A' + 1;
        ptr += 2;
    }
    fcb++;
    while (*ptr != '.' && *ptr != 0x00 && count < 8)
    {
        *fcb++ = *ptr++;
        count++;
    }
    if (*ptr != 0x00)
    {
        while (count < 8)
        {
            fcb++;
            count++;
        }
        if (*ptr == '.')
        {
            ptr++;
            count = 0;
            while(*ptr != 0x00 && count < 3)
            {
                *fcb++ = *ptr++;
                count++;
            }
        }
    }
    return;
}
#else
//extern void dumpmem(UINT8 *ram, UINT16 len);
// New implementation (easier to understand, hopefully)
void fillfcb(char *fcb, char *filename)
{
    char *ptr;
    char i;

    //printf("fillfcb(0x%p, 0x%p)\n", fcb, filename);
    //dumpmem(filename, strlen(filename)); // #debug
    // init fcb's filename
    fcb[0] = 0x00;
    memset(&fcb[1], ' ', 11);

    ptr = filename;
    // process head "x:" of drive (fcb[0])
    if (ptr[1] == ':')
    {
        if ((toupper(ptr[0]) >= 'A') && (toupper(ptr[0]) <= 'Z'))
            fcb[0] = toupper(ptr[0]) - 'A' + 1;
        ptr += 2; // shift ptr to head of filename
    }

    // fill file name (w/o ext). (fcb[1]..fcb[8])
    for (i = 0; i < 8; i++)
    {
        if (*ptr == '.' || *ptr == 0x00) break;
        fcb[i + 1] = *ptr++;
    }
    if (i == 8) while(*ptr != '.' && *ptr != 0x00) ptr++; // filename too long, skip till next 0x00 or '.'

    // fill file ext (fcb[9]..fcb[11])
    switch (*ptr)
    {
    case 0x00:
        break; // no ext
    case '.':
        ptr++;	// skip '.'
        for (i = 0; i < 3; i++)
            if (*ptr != 0x00) fcb[i + 9] = *ptr++;
        break;
    }
    //dumpmem(fcb,0x40); //#debug
    return;
}
#endif
/*----------------------------------------------------------------------*/
void submit(char *ptr1)
{
    char *ptr2;
    UINT8 ch, ch2;
    char filename[20];
    UINT8 submitcount = 1;
    char *submitptr[10];
    char submitstring[CPMCMDBUF];
    int tmp;
    void upcase(char *);
	FILE *subfileout = NULL;

    for (tmp = 1 ; tmp < 10 ; submitptr[tmp++] = NULL);
    *submitstring = 0x00;
    upcase(ptr1);	// cmd string to uppercase
    // fill filename xxxx.sub
    ptr1 += 6; // skip 'submit'
    while (*ptr1 == ' ') ptr1++; // skip spaces
    ptr2 = filename;
    while (*ptr1 != ' ' && *ptr1 != '.' && *ptr1 != 0x00) *ptr2++ = *ptr1++; // copy file name(till '.', ' ' or 0x00)
    switch (*ptr1)
    {
    case '.':
        while (*ptr1 != ' ' && *ptr1 != 0x00) // copy file extension
            *ptr2++ = *ptr1++;
        *ptr2 = 0x00;
        break;
    case ' ':
        strcpy(ptr2, ".SUB");
        break;			// no extension, add ".SUB"
    case  0 :
        strcpy(ptr2, ".SUB");
        break;			// no extension, add ".SUB"
    }
    // Open xxxx.sub and fill submit $1...$9
    if ((subfile = fopen(filename, "r")) == NULL)
    {
        printf("Submit File %s not found\n", filename);
        return;
    }
    else if (*ptr1 != 0x00)
    {
        ptr1++;
        strncpy(submitstring, ptr1, sizeof(submitstring) - 1);
        ptr1 = submitstring;
        while (submitcount < 10)
        {
            if (*ptr1 == 0x00) break;
            submitptr[submitcount++] = ptr1;
            while (*ptr1 != ' ' && *ptr1 != 0x00) ptr1++;
            if (*ptr1 == ' ') *ptr1++ = 0x00;
        }
    }
    // debug
    //for (tmp = 1; tmp < 10; tmp++)
    //{
    //	printf("$%d = '%s'\n", tmp, submitptr[tmp]);
    //}
    // open $$$.SUB and convert xxxx.sub to $$$.sub
    remove("$$$.SUB");

    if ((subfileout = fopen("$$$.SUB", "w")) == NULL) printf("Can't create $$$.SUB\n");
    else
    {
        //printf("$$$.SUB:\n"); // #debug
        while (!feof(subfile))
        {
            //ch = toupper(getc(subfile));
            ch = getc(subfile);
            if (ch == '$')
            {
                ch2 = getc(subfile);
                if (!isdigit(ch2))
                {
                    putc(ch, subfileout);
                    putc(ch2, subfileout);
                    //putchar(ch); putchar(ch2); // #debug
                }
                else
                {
                    ptr2 = submitptr[ch2 - '0'];
                    if (ptr2 != NULL)
                    {
                        fputs(ptr2, subfileout);
                        //printf("%s",ptr2); // #debug
                    }
                }
            }
            else if (ch != 0xFF)
            {
                putc(ch, subfileout);
                //putchar(ch); // #debug
            }
        }
    }
    //putc('\r',subfileout);
    //putc('\n',subfileout); // add additional CRLF to $$$.SUB
    //printf("---- end of $$$.SUB ---\n"); //#debug
    fclose(subfile);
    fclose(subfileout);
    subfileout = NULL;
    // Open $$$.SUB for submit
    if ((subfile = fopen("$$$.SUB", "r")) == NULL) printf("Can't open $$$.SUB\n");
    return;
}
void chkclosesubfile(void)
{
    if (feof(subfile) != 0)
    {
        fclose(subfile);
        subfile = NULL;
        remove("$$$.SUB");
        xsubflag = FALSE; // xsub support
    }
    return;
}
/*----------------------------------------------------------------------*/
/* BDOS and BIOS call Trace                                             */
/*----------------------------------------------------------------------*/
void debug(char *ptr1)
{
    if (strcasecmp(ptr1, "DEBUG ON") == 0)
    {
        printf("Debug is set on\n");
        DebugFlag = 1;
        lastcall = 0xff;
        if (lpt == NULL)
        {
            lpt = fopen("btrace.out", "w");
            //lpt = fopen("stdout","w"); // debug print
            fputs("Times  Z80PC    Z80DE    Z80DMA   FUNCTION\n"
                  , lpt);
            fputs("-----  -----    -----    ------   --------\n"
                  , lpt);
        }
        // add screen output stream debug file
        if (stream == NULL)
        {
            stream = fopen("stream.out", "wb");
        }
    }
    else if (strcasecmp(ptr1, "DEBUG OFF") == 0)
    {
        printf("Debug is set off\n");
        DebugFlag = 0;
        if (lpt != NULL)
        {
            if (lastcall != 0xFF)
            {
                fprintf(lpt, "% 5d  %s%s\n", repeats, debugmess1
                        , debugmess2);
            }
            fputs("--------- END OF BDOS TRACE TABLE -------- "
                  , lpt);
            fclose(lpt);
            lpt = NULL;
        }
        // add screen output stream debug file
        if (stream != NULL)
        {
            fclose(stream);
            stream = NULL;
        }
    }
    else
    {
        if (DebugFlag) printf("Debug is on\n");
        else printf("Debug is off\n");
    }
    return;
}
/*----------------------------------------------------------------------*/
void z80debug(char *ptr1)
{
    if (strcasecmp(ptr1, "Z80DBG ON") == 0)
    {
        printf("Z80 Debug is set on\n");
        Z80DebugFlag = 1;
        if (Z80Trace == NULL)
        {
            Z80Trace = fopen("z80trace.dat", "w");
            fputs("--------- Start OF Z80 TRACE TABLE --------\n", Z80Trace);

        }
    }
    else if (strcasecmp(ptr1, "Z80DBG OFF") == 0)
    {
        printf("Z80 Debug is set off\n");
        Z80DebugFlag = 0;
        if (Z80Trace != NULL)
        {
            fputs("--------- END OF Z80 TRACE TABLE --------\n", Z80Trace);
            fclose(Z80Trace);
            Z80Trace = NULL;
        }
    }
    else
    {
        if (Z80DebugFlag) printf("Z80 Debug is on\n");
        else printf("Z80 Debug is off\n");
    }
    return;
}
/*----------------------------------------------------------------------*/
/* Check input string from prompt is a command or a Z80 program         */
/* Command will use system() to run                                     */
/*----------------------------------------------------------------------*/
#include <sysinfoapi.h> // windows API timer GetTickCount()
static DWORD base_time;
void CheckDosCommand(char *ptr)
{
    int cnt = 0, i;
    int argc = 0;
    char *argv[kMaxArgs];
    char output[CPMCMDBUF], buffer[CPMCMDBUF], *p2, *cmd, *strend;
    char *ptr1;

    strncpy(buffer, ptr, sizeof(buffer) - 1);
    ptr1 = buffer;

    // cmd string into argc and argv[]
    strend = ptr1 + strlen(ptr1);
    p2 = strtok(ptr1, " ");
    while (cnt < kMaxArgs)
    {
        if (p2 != NULL)
        {
            argv[cnt] = p2;
            argc++;
        }
        else
        {
            argv[cnt] = strend;
        }
        //     printf("argv[%d] = %s\n", cnt, argv[cnt]);
        cnt++;
        p2 = strtok(0, " ");
    }
    //   printf("argc = %d\n",argc);

    doscommand = 1;
    //printf("CheckDosCommand[%s,len=%d]\n",ptr,strlen(ptr)); // debug
    //for (i = 0; i < strlen(ptr); i++) printf("<0x%02x>",ptr[i]);
    //printf("\n");
    if (strcasecmp(argv[0], "EXIT") == 0)         quit();
    else if (strcasecmp(argv[0], "QUIT") == 0)    quit();
    else if (ptr[0] == '!')
    {
        system(ptr + 1);
        return;
    }
    else if (ptr[0] == ';')
    {
        return;    //comment, no action
    }
    else if (strcasecmp(argv[0], "COLD!") == 0)
    {
        clearmem();
        loadcpmhex();
        longjmp(cold_start, 0);
        return;
    }
    else if (strcasecmp(argv[0], "VER") == 0 )
    {
        printtitle();
        return;
    }
    else if (strcasecmp(argv[0], "DEBUG") == 0)
    {
        debug(ptr);
        return;
    }
    else if (strcasecmp(argv[0], "SUBMIT") == 0)
    {
        submit(ptr);
        return;
    }
    else if (strcasecmp(argv[0], "XSUB") == 0)
    {
        if (subfile != NULL)
        {
            xsubflag = TRUE;    // XSUB support
            printf("XSUB enabled\n");
            return;
        }
    }
    else if (strcasecmp(argv[0], "Z80DBG") == 0)
    {
        z80debug(ptr);
        return;
    }
    else if (strlen(ptr) == 0) return;
    else if ((strlen(ptr) == 2) && (isalpha(ptr[0])) && (ptr[1] == ':'))
    {
        int drive;
        // change drive (This is work on windows CMD shell, not work on msys2 command shell!
        drive = toupper(ptr[0]) - 'A' + 1;
        if(_chdrive( drive ))
        {
            printf("Drive %s is not available!\n", ptr1);
        }
        return;
    }
    else if (ptr[0] == 0xFF) return; // workaround for $$$.SUB end with 0xFF
#ifdef GNU_READLINE
    else if (strcasecmp(argv[0], "LIST") == 0) // list readline buffer
    {
        HIST_ENTRY **list;
        int i;

        list = history_list ();
        if (list)
        {
            for (i = 0; list[i]; i++)
                fprintf (stderr, "%d: %s\r\n", i, list[i]->line);
        }
        return;
    }
#endif
    if (strcasecmp(argv[0], "bdosdbg") == 0) // set/clear bdos debug flag
    {
        // format: bdosdbg [on/off] [n1 n2 n3 n4....]
        UINT8 flag;
        switch (argc)
        {
        case 1: // no other arguments, show bdosdbg flags (print current flags)
            printf("BDOS DBG Flag ON:");
            for (i = 0; i < BDOSDBGFLAGS; i++)
                if (bdosdbgflag[i] != 0) printf("%d ", i);
            printf("\n");
            break;
        case 2: // all on or all off
            if (strcasecmp(argv[1], "on") == 0)
                for (i = 0; i < BDOSDBGFLAGS; i++) bdosdbgflag[i] = 1;
            else if (strcasecmp(argv[1], "off") == 0)
                for (i = 0; i < BDOSDBGFLAGS; i++) bdosdbgflag[i] = 0;
            break;
        default:
            if (strcasecmp(argv[1], "on") == 0)
                flag = 1;
            else if (strcasecmp(argv[1], "off") == 0)
                flag = 0;
            else
                break;
            for (i = 2; i < argc; i++)
                if (atoi(argv[i]) < BDOSDBGFLAGS)
                    bdosdbgflag[atoi(argv[i])] = flag;
            break;
        }
        return;
    }

    DWORD curr_time, timer_ms;
    if (strcasecmp(argv[0], "TIMER") == 0)
    {
        if (strcasecmp(argv[1], "RESET") == 0)
        {
            base_time = GetTickCount();
            printf("Timer Reset, Base Time:%ld ms\n", base_time);
        }
        if (strcasecmp(argv[1], "SHOW") == 0)
        {
            curr_time = GetTickCount();
            timer_ms = curr_time - base_time;
            printf("Current Time:%ld ms, Duration %.3f seconds\n", curr_time, (float)timer_ms / 1000.0f);
        }
        return;
    }
    if (strcasecmp(argv[0], "DIR") == 0) cmd = "dir";
    else if (strcasecmp(argv[0], "LS") == 0) cmd = "dir";
    else if (strcasecmp(argv[0], "COPY") == 0)   cmd = "copy";
    else if (strcasecmp(argv[0], "REN") == 0)    cmd = "rename";
    else if (strcasecmp(argv[0], "RENAME") == 0) cmd = "rename";
    else if (strcasecmp(argv[0], "MV") == 0)     cmd = "move";
    else if (strcasecmp(argv[0], "MOVE") == 0)    cmd = "move";
    else if (strcasecmp(argv[0], "PWD") == 0)     cmd = "cd";
    else if (strcasecmp(argv[0], "CD") == 0)
    {
        if (argc == 1) cmd = "cd";
        else
        {
            if(chdir(argv[1]) != 0) printf("Path %s not exist!\n", argv[1]);
            return;
        }
    }
    else if (strcasecmp(argv[0], "DEL") == 0)  cmd = "del";
    else if (strcasecmp(argv[0], "RM") == 0)   cmd = "del";
    else if (strcasecmp(argv[0], "ERA") == 0)  cmd = "del";
    else if (strcasecmp(argv[0], "TYPE") == 0) cmd = "type";
    else if (strcasecmp(argv[0], "CAT") == 0)  cmd = "type";
    else
    {
        doscommand = 0;
        return;
    }

    strncpy(output, cmd, sizeof(output) - 1);
    for (i = 1; i < argc; i++)
    {
        strncat(output, " ", sizeof(output) - 1);
        strncat(output, argv[i], sizeof(output) - 1);
    }
    system(output);
    printf("\n");
    return;
}
/*----------------------------------------------------------------------*/
void upcase(char *ptr1)
{
#if 0
    while (*ptr1 != 0x00)
    {
        if ( *ptr1 > 0x60 && *ptr1 < 0x7b) *ptr1 -= 32;
        ptr1++;
    }
#else
    while (*ptr1 != 0x00)
    {
        *ptr1 = toupper(*ptr1);
        ptr1++;
    }
#endif
    return;
}
/*----------------------------------------------------------------------*/
void getstring(char *filename, int size)
{
    char tempname[CPMCMDBUF];
    int name_len;

    //printf("getstring()\n");
    filename[0] = 0x00;	// clear filename first
    name_len = min(size - 1, CPMCMDBUF - 1);
    if (subfile != NULL)
    {
        /* Submit Batch command File Processing */
        fgets(filename, name_len, subfile);
        printf("%s\n", filename);
        chkclosesubfile();
    }
    else
    {
        /* Normal Command Input */
        /* gets(filename); */
        tempname[0] = 0x00;
        if (gear_fgets(tempname, name_len, stdin, 1) != NULL)
        {
            strncpy(filename, tempname, name_len);
        }
        printf("\n");
    }
    return;
}
/*----------------------------------------------------------------------*/
void getcommand(void)
{
    char filename[CPMCMDBUF];
    char first_file[20];
    char *ptr1, *ptr2;
    UINT8 i;
    char dir_now[256];

    int cnt;
    int argc;
    char *argv[kMaxArgs];
    char *p2, *strend;

    if (bdosdbgflag[0])printf("getcommand()\n"); //#debug
    do
    {
        do
        {
            if (getcwd(dir_now, 250) == NULL) strcpy(dir_now, "Unknown");

#ifdef GNU_READLINE
            /* Use GNU_Readline Library, support command line history */
            char prompt[256];
            filename[0] = 0x00;
            rl_getc_function = (Function *)getc_cpmcmd; // change getc function to handle ctrl-c;
            //rl_getc_function = getc_cpmcmd;
            rl_catch_signals = 0;
            rl_catch_sigwinch = 0;
            if (subfile == NULL)
            {
                sprintf(prompt, "Z80 %s>", dir_now);
                ptr1 = readline(prompt);
                if (ptr1 != NULL)
                {
                    if (*ptr1) add_history(ptr1);
                    strncpy(filename, ptr1, sizeof(filename) - 1);
                    free(ptr1);
                }
                //else filename[0]=0x00;
            }
            else
            {
                printf("Z80 %s>", dir_now);
                getstring(filename, sizeof(filename));
            }
#else
            /* Do not use GNU_Readline Library. No history support */
            printf("Z80 %s>", dir_now);
            getstring(filename, sizeof(filename));
#endif
            if (bdosdbgflag[0]) dumpmem((UINT8*)filename, strlen(filename)); //printf("getcommand(%s)\n",filename); //#debug
            if (stream != NULL)
            {
                fprintf(stream, "Z80 %s>", dir_now);
                fputs(filename, stream);   // log command to stream.out
                fputs("\n", stream);
            }
            ptr1 = filename;
            //while (*ptr1 == ' ') ptr1++; // skip head spaces

            // remove 0x0d/0x0a
            for (i = 0; ptr1[i] != 0x00; i++)
            {
                if ((ptr1[i] == 0x0d) || (ptr1[i] == 0x0a)) ptr1[i] = 0x00;
            }
            CheckDosCommand(ptr1);
            upcase(ptr1);
        }
        while (doscommand);

        // measure single application elapsed time feature
        if (strncasecmp(ptr1, "TIME ", 5) == 0)
        {
            // Time xxxx .... (to measure the elapsed time of this application)
            ptr1 += 5; // skip "TIME "
            timer_start = GetTickCount();
            timer_flag = 1;
            printf("\nTimer Start, base:%u ms\n", timer_start);
        }
        // #dchen 20240407
        // Rewrite this part by strtok(). I think these original hard-coded ptr code is not bug free
        // use strchr/strtok can make program much simpler and bug free.

        // fill ram[0x81] ~ ram[0xFF] with rest arguments and ram[0x80] with length
        memset(&ram[0x80], 0x00, 128); 	// Clear RAM 0x80-0xFF
        if ((ptr2 = strchr(ptr1, ' ')) != NULL)
        {
            // with space in argument --> argc >= 1 -> copy remaining arguments to ram[0x80]
            //while (*ptr2 == ' ') ptr2++; // skip these spaces (may not be one space char)
            // #dchen20240410 need to keep at least one space char to make small-c-floats runs correctly.
            strncpy((char*)&ram[0x81], ptr2, 126); // max length 126 bytes (0x80-0x7E), 0x7F for end string 00.
            ram[0x80] = (UINT8)strlen((char*)&ram[0x81]);
        }
        if (bdosdbgflag[0]) dumpram(0x80, 128); // #debug

        // fill argc and argvs
        cnt = 0;
        argc = 0;
        strend = ptr1 + strlen(ptr1);
        p2 = strtok(ptr1, " ");
        while (cnt < kMaxArgs)
        {
            if (p2 != NULL)
            {
                argv[cnt] = p2;
                argc++;
            }
            else
            {
                argv[cnt] = strend;
            }
            if (bdosdbgflag[0])printf("argv[%d] = %s\n", cnt, argv[cnt]); // #debug
            cnt++;
            p2 = strtok(0, " ");
        }
        if (bdosdbgflag[0])printf("argc = %d\n", argc); // #debug

        strcpy(first_file, argv[0]);
        if(strchr(first_file, '.') == NULL)
        {
            strcat(first_file, ".COM");
        }

    }
    while (loadcom(first_file) != 0);   //load 'first_file'.COM file
    fillfcb((char *)(&ram[0x005c]), argv[1]);
    fillfcb((char *)(&ram[0x006c]), argv[2]);
    if (bdosdbgflag[0])dumpram(0x5c, 0x30); //#debug

    //	while ((i = _bios_keybrd(_KEYBRD_READY)) != 0)
    //	        _bios_keybrd(_KEYBRD_READ);    /* clear keybrd buffer */

    ResetZ80;        /* pull reset line to low */
    ReturnZ80;        /* set z80 to raad RAM */
    ResetZ80;   /* pull reset line to high ,z80 begin running from PC=0x0000 */
    simz80(pc); //waitz80(); skip 1st fake bdos/bios call in WARMBOOT->CALLPC (CPMEMUZ80.Z80)
    ReturnZ80;
    simz80(pc); //waitz80(); skip 2nd fake bdos/bios call in MAIN (CPMEMUZ80.Z80)
    halt = *eop = 0; // Now, program will really start (enter 0x0100) in next simz80() call
    return;
}
/*----------------------------------------------------------------------*/
/* Program Initialize Part                                              */
/*----------------------------------------------------------------------*/
void initpointer(void)               /* initial pointers value */
{
    rega = (UINT8 *)REGA;  /* initial 8 bits register pointers */
    regb = (UINT8 *)REGB;
    regc = (UINT8 *)REGC;
    regd = (UINT8 *)REGD;
    rege = (UINT8 *)REGE;
    regh = (UINT8 *)REGH;
    regl = (UINT8 *)REGL;

    regaf = (UINT16 *)REGAF; /* initial 16 bits register pointers */
    regbc = (UINT16 *)REGBC;
    regde = (UINT16 *)REGDE;
    reghl = (UINT16 *)REGHL;
    regix = (UINT16 *)REGIX;
    regiy = (UINT16 *)REGIY;
    regip = (UINT16 *)REGIP;
    regsp = (UINT16 *)REGSP;

    eop = (UINT8 *)EOP;
    bioscode = (UINT8 *)BIOSCODE;
    bdoscall = (UINT8 *)BDOSCALL;

    fcbdn = (UINT8 *)FCBDN; /* initial FCB block pointers */
    fcbfn = (UINT8 *)FCBFN;
    fcbft = (UINT8 *)FCBFT;
    fcbrl = (UINT8 *)FCBRL;
    fcbrc = (UINT8 *)FCBRC;
    fcbcr = (UINT8 *)FCBCR;
    fcbln = (UINT8 *)FCBLN;
    return;
}
/*----------------------------------------------------------------------*/
void initial(void)
{
    initpointer();
    initialbdos();
    initialbios();
    printtitle();

    subfile = fopen("$$$.SUB", "r");
    base_time = GetTickCount();
    return;
}
#if 0
/*----------------------------------------------------------------------*/
/* Z80 Software CPU execution loop                                      */
/* It will execute until a HALT command (BDOS and BIOS call in Z80 HEX  */
/* loaded by program)                                                   */
/*----------------------------------------------------------------------*/
void waitz80(void)
{
    simz80(GetPC());		/* call z80 engine, loop till BDOS call, BIOS call, end of program or CTRL-C*/
#if 0 //ctrl-c debug
    if (ctrlc_flag)
    {
        ctrlc_flag = 0;
        longjmp(ctrl_c, 0);
    }
#endif
    return;
}
#endif
/*----------------------------------------------------------------------*/
/* Reset Simulated Z80 CPU (move PC to 0x0000)                          */
/*----------------------------------------------------------------------*/
void resetz80(void)
{
    SetPC(0x0000);	/* initialize pc */
    /* There should be some other registers to be initialized */
    return;
}
/*----------------------------------------------------------------------*/

void quit(void)
{
    if (DebugFlag ==  1 && lpt !=  NULL)
    {
        if (lastcall  !=  0xFF)
        {
            fprintf(lpt, "% 5d  %s%s\n", repeats, debugmess1
                    , debugmess2);
        }
        fputs("----- END OF BDOS TRACE TABLE ----- ", lpt);
        fclose(lpt);
    }

    exit(0);
    return;
}
/*----------------------------------------------------------------------*/
void closeall(void)
{
    UINT8 fcbptr;

    for (fcbptr = 0 ; fcbptr < MAXFILE ; fcbptr++)
    {
        if (fcbfile[fcbptr] != NULL) fclose(fcbfile[fcbptr]);
        fcbfile[fcbptr] = NULL;
        fcbused[fcbptr][0] = 0x00;
        fcbfilelen[fcbptr] = 0x0000;
    }
    return;
}
/*----------------------------------------------------------------------*/
/* Main Loop                                                            */
/*----------------------------------------------------------------------*/
extern UINT8 filesearchingflag;// flag for bdos17(search for 1st) and bdos18(search for next)
int main(void)
{

    initial();
    setjmp(cold_start);

#ifdef UNIX
    signal (SIGINT, handle_ctrl_c);
#endif // UNIX

    // Windows
    ctrlc_flag = 0;
    BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);

    if (SetConsoleCtrlHandler(CtrlHandler, TRUE))
    {
        //printf("\nThe Ctrl-C Control Handler is installed.\n");
        ctrlc_flag = 0; // Clear Ctrl-C flag
        //while (ctrlc_flag == 0)
        //{
        //    Sleep(1000); // delay 10ms
        //    printf("ctrlc_flag=%d\n", ctrlc_flag);
        //}
    }
    // Windows

    while(1)
    {
        clearmem();
        setjmp(ctrl_c);
        ctrlc_flag = 0;
        chkclosesubfile();

        while (1)
        {
            GotoPC;
            loadcpmhex();       /* Load .HEX to RAM everytime befor running a program */
            closeall();         /* Clear opened file in last Z80 program (for turbo pascal!) */
            filesearchingflag = 0; // Not searching file.
            getcommand();       /* Read a Command and Loading to RAM. */
            dmaaddr = 0x0080;
            //while (*eop == 0 && halt == 0) {
            while(1)
            {
                simz80(pc); //waitz80();
                //if (*eop != 0 || halt != 0) break;
                if (halt) break;
                if (*bdoscall) cpmbdos();
                else cpmbios();
                if (*eop) break;
#if 1 // try ctrl-c	break (per bdos/bios API call)
                if (ctrlc_flag)
                {
                    ctrlc_flag = 0;
                    printf("^C\n");
                    longjmp(ctrl_c, 0);
                    break;
                }
#endif
            }
            halt = 0;
            printf("\n");

            // Time xxxx .... (to measure the elapsed time of this application)
            if (timer_flag)
            {
                timer_stop = GetTickCount();
                timer_flag = 0;
                printf("\nTimer Stop, base:%u ms, Elapsed Time:%.3f Seconds\n", timer_stop, (timer_stop - timer_start) / 1000.0f);
            }
        }
    }
    return 0;
}
