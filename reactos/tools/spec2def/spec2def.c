#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

typedef struct
{
    char *pcName;
    int nNameLength;
    char *pcRedirection;
    int nRedirectionLength;
    int nCallingConvention;
    int nOrdinal;
    int nStackBytes;
    int nArgCount;
    int anArgs[30];
    unsigned int uFlags;
} EXPORT;

typedef int (*PFNOUTLINE)(FILE *, EXPORT *);
int gbKillAt = 0;
int gbUseDeco = 0;
int gbMSComp = 0;
int no_redirections = 0;
char *pszArchString = "i386";
char *pszArchString2;
char *pszDllName = 0;

enum
{
    FL_PRIVATE = 1,
    FL_STUB = 2,
    FL_NONAME = 4,
};

enum
{
    CC_STDCALL,
    CC_CDECL,
    CC_FASTCALL,
    CC_EXTERN,
    CC_STUB,
};

enum
{
    ARG_LONG,
    ARG_PTR,
    ARG_STR,
    ARG_WSTR,
    ARG_DBL,
    ARG_INT64
};

char* astrCallingConventions[] =
{
    "STDCALL",
    "CDECL",
    "FASTCALL",
    "EXTERN"
};

static
int
IsSeparator(char chr)
{
    return ((chr <= ',' && chr != '$') ||
            (chr >= ':' && chr < '?') );
}

int
CompareToken(const char *token, const char *comparand)
{
    while (*comparand)
    {
        if (*token != *comparand) return 0;
        token++;
        comparand++;
    }
    if (!IsSeparator(*token)) return 0;
    return 1;
}

int
ScanToken(const char *token, char chr)
{
    while (!IsSeparator(*token))
    {
        if (*token++ == chr) return 1;
    }
    return 0;
}

char *
NextLine(char *pc)
{
    while (*pc != 0)
    {
        if (pc[0] == '\n' && pc[1] == '\r') return pc + 2;
        else if (pc[0] == '\n') return pc + 1;
        pc++;
    }
    return pc;
}

int
TokenLength(char *pc)
{
    int length = 0;

    while (!IsSeparator(*pc++)) length++;

    return length;
}

char *
NextToken(char *pc)
{
    /* Skip token */
    while (!IsSeparator(*pc)) pc++;

    /* Skip white spaces */
    while (*pc == ' ' || *pc == '\t') pc++;

    /* Check for end of line */
    if (*pc == '\n' || *pc == '\r' || *pc == 0) return 0;

    /* Check for comment */
    if (*pc == '#' || *pc == ';') return 0;

    return pc;
}

void
OutputHeader_stub(FILE *file)
{
    fprintf(file, "/* This file is autogenerated, do not edit. */\n\n"
            "#include <stubs.h>\n\n");
}

int
OutputLine_stub(FILE *file, EXPORT *pexp)
{
    int i;

    if (pexp->nCallingConvention != CC_STUB &&
        (pexp->uFlags & FL_STUB) == 0) return 0;

    fprintf(file, "int ");
    if (strcmp(pszArchString, "i386") == 0 &&
        pexp->nCallingConvention == CC_STDCALL)
    {
        fprintf(file, "__stdcall ");
    }

    fprintf(file, "%.*s(", pexp->nNameLength, pexp->pcName);

    for (i = 0; i < pexp->nArgCount; i++)
    {
        if (i != 0) fprintf(file, ", ");
        switch (pexp->anArgs[i])
        {
            case ARG_LONG: fprintf(file, "long"); break;
            case ARG_PTR:  fprintf(file, "void*"); break;
            case ARG_STR:  fprintf(file, "char*"); break;
            case ARG_WSTR: fprintf(file, "wchar_t*"); break;
            case ARG_DBL: case ARG_INT64 :  fprintf(file, "__int64"); break;
        }
        fprintf(file, " a%d", i);
    }
    fprintf(file, ")\n{\n\tDPRINT1(\"WARNING: calling stub %.*s(",
            pexp->nNameLength, pexp->pcName);

    for (i = 0; i < pexp->nArgCount; i++)
    {
        if (i != 0) fprintf(file, ",");
        switch (pexp->anArgs[i])
        {
            case ARG_LONG: fprintf(file, "0x%%lx"); break;
            case ARG_PTR:  fprintf(file, "0x%%p"); break;
            case ARG_STR:  fprintf(file, "'%%s'"); break;
            case ARG_WSTR: fprintf(file, "'%%ws'"); break;
            case ARG_DBL:  fprintf(file, "%%f"); break;
            case ARG_INT64: fprintf(file, "%%\"PRix64\""); break;
        }
    }
    fprintf(file, ")\\n\"");

    for (i = 0; i < pexp->nArgCount; i++)
    {
        fprintf(file, ", ");
        switch (pexp->anArgs[i])
        {
            case ARG_LONG: fprintf(file, "(long)a%d", i); break;
            case ARG_PTR:  fprintf(file, "(void*)a%d", i); break;
            case ARG_STR:  fprintf(file, "(char*)a%d", i); break;
            case ARG_WSTR: fprintf(file, "(wchar_t*)a%d", i); break;
            case ARG_DBL:  fprintf(file, "(double)a%d", i); break;
            case ARG_INT64: fprintf(file, "(__int64)a%d", i); break;
        }
    }
    fprintf(file, ");\n");

    if (pexp->nCallingConvention == CC_STUB)
    {
        fprintf(file, "\t__wine_spec_unimplemented_stub(\"%s\", __FUNCTION__);\n", pszDllName);
    }

    fprintf(file, "\treturn 0;\n}\n\n");

    return 1;
}

void
OutputHeader_asmstub(FILE *file, char *libname)
{
    fprintf(file, "; File generated automatically, do not edit! \n\n"
            ".586\n.model flat\n.code\n");
}

int
OutputLine_asmstub(FILE *fileDest, EXPORT *pexp)
{
    /* Handle autoname */
    if (pexp->nNameLength == 1 && pexp->pcName[0] == '@')
    {
        fprintf(fileDest, "PUBLIC ordinal%d\nordinal%d: nop\n",
                pexp->nOrdinal, pexp->nOrdinal);
    }
    else if (pexp->nCallingConvention == CC_STDCALL)
    {
        fprintf(fileDest, "PUBLIC _%.*s@%d\n_%.*s@%d: nop\n",
                pexp->nNameLength, pexp->pcName, pexp->nStackBytes,
                pexp->nNameLength, pexp->pcName, pexp->nStackBytes);
    }
    else if (pexp->nCallingConvention == CC_FASTCALL)
    {
        fprintf(fileDest, "PUBLIC @%.*s@%d\n@%.*s@%d: nop\n",
                pexp->nNameLength, pexp->pcName, pexp->nStackBytes,
                pexp->nNameLength, pexp->pcName, pexp->nStackBytes);
    }
    else if (pexp->nCallingConvention == CC_CDECL ||
             pexp->nCallingConvention == CC_STUB)
    {
        fprintf(fileDest, "PUBLIC _%.*s\n_%.*s: nop\n",
                pexp->nNameLength, pexp->pcName,
                pexp->nNameLength, pexp->pcName);
    }
    else if (pexp->nCallingConvention == CC_EXTERN)
    {
        fprintf(fileDest, "PUBLIC _%.*s\n_%.*s:\n",
                pexp->nNameLength, pexp->pcName,
                pexp->nNameLength, pexp->pcName);
    }

    return 1;
}

void
OutputHeader_def(FILE *file, char *libname)
{
    fprintf(file,
            "; File generated automatically, do not edit!\n\n"
            "LIBRARY %s\n\n"
            "EXPORTS\n",
            libname);
}

void
PrintName(FILE *fileDest, EXPORT *pexp, int fRedir, int fDeco)
{
    char *pcName = fRedir ? pexp->pcRedirection : pexp->pcName;
    int nNameLength = fRedir ? pexp->nRedirectionLength : pexp->nNameLength;

    if (fDeco && pexp->nCallingConvention == CC_FASTCALL)
         fprintf(fileDest, "@");
    fprintf(fileDest, "%.*s", nNameLength, pcName);
    if ((pexp->nCallingConvention == CC_STDCALL ||
        pexp->nCallingConvention == CC_FASTCALL) && fDeco)
    {
        fprintf(fileDest, "@%d", pexp->nStackBytes);
    }
}

int
OutputLine_def(FILE *fileDest, EXPORT *pexp)
{
    fprintf(fileDest, " ");

    /* Handle autoname */
    if (pexp->nNameLength == 1 && pexp->pcName[0] == '@')
    {
        fprintf(fileDest, "ordinal%d", pexp->nOrdinal);
    }
    else
    {
        PrintName(fileDest, pexp, 0, gbUseDeco && !gbKillAt);
    }

    if (pexp->pcRedirection && !no_redirections)
    {
        int fDeco = (gbUseDeco && !ScanToken(pexp->pcRedirection, '.'));

        fprintf(fileDest, "=");
        PrintName(fileDest, pexp, 1, fDeco && !gbMSComp);
    }
    else if (gbUseDeco && gbKillAt && !gbMSComp &&
             (pexp->nCallingConvention == CC_STDCALL ||
              pexp->nCallingConvention == CC_FASTCALL))
    {
        fprintf(fileDest, "=");
        PrintName(fileDest, pexp, 0, 1);
    }

    if (pexp->nOrdinal != -1)
    {
        fprintf(fileDest, " @%d", pexp->nOrdinal);
    }

    if (pexp->nCallingConvention == CC_EXTERN)
    {
        fprintf(fileDest, " DATA");
    }

    if (pexp->uFlags & FL_PRIVATE)
    {
        fprintf(fileDest, " PRIVATE");
    }

    if (pexp->uFlags & FL_NONAME)
    {
        fprintf(fileDest, " NONAME");
    }

    fprintf(fileDest, "\n");

    return 1;
}

int
ParseFile(char* pcStart, FILE *fileDest, PFNOUTLINE OutputLine)
{
    char *pc, *pcLine;
    int nLine;
    EXPORT exp;
    int included;

    //fprintf(stderr, "info: line %d, pcStart:'%.30s'\n", nLine, pcStart);

    /* Loop all lines */
    nLine = 1;
    for (pcLine = pcStart; *pcLine; pcLine = NextLine(pcLine), nLine++)
    {
        pc = pcLine;

        exp.nArgCount = 0;
        exp.uFlags = 0;

        //fprintf(stderr, "info: line %d, token:'%d, %.20s'\n",
        //        nLine, TokenLength(pcLine), pcLine);

        /* Skip white spaces */
        while (*pc == ' ' || *pc == '\t') pc++;

        /* Skip empty lines, stop at EOF */
        if (*pc == ';' || *pc <= '#') continue;
        if (*pc == 0) return 0;

        //fprintf(stderr, "info: line %d, token:'%.*s'\n",
        //        nLine, TokenLength(pc), pc);

        /* Now we should get either an ordinal or @ */
        if (*pc == '@') exp.nOrdinal = -1;
        else exp.nOrdinal = atol(pc);

        /* Go to next token (type) */
        if (!(pc = NextToken(pc)))
        {
            fprintf(stderr, "error: line %d, unexpected end of line\n", nLine);
            return -10;
        }

        //fprintf(stderr, "info: Token:'%.10s'\n", pc);

        /* Now we should get the type */
        if (CompareToken(pc, "stdcall"))
        {
            exp.nCallingConvention = CC_STDCALL;
        }
        else if (CompareToken(pc, "cdecl") ||
                 CompareToken(pc, "varargs"))
        {
            exp.nCallingConvention = CC_CDECL;
        }
        else if (CompareToken(pc, "fastcall"))
        {
            exp.nCallingConvention = CC_FASTCALL;
        }
        else if (CompareToken(pc, "extern"))
        {
            exp.nCallingConvention = CC_EXTERN;
        }
        else if (CompareToken(pc, "stub"))
        {
            exp.nCallingConvention = CC_STUB;
        }
        else
        {
            fprintf(stderr, "error: line %d, expected type, got '%.*s' %d\n",
                    nLine, TokenLength(pc), pc, *pc);
            return -11;
        }

        //fprintf(stderr, "info: nCallingConvention: %d\n", exp.nCallingConvention);

        /* Go to next token (options or name) */
        if (!(pc = NextToken(pc)))
        {
            fprintf(stderr, "fail2\n");
            return -12;
        }

        /* Handle options */
        included = 1;
        while (*pc == '-')
        {
            if (CompareToken(pc, "-arch"))
            {
                /* Default to not included */
                included = 0;
                pc += 5;

                /* Look if we are included */
                while (*pc == '=' || *pc == ',')
                {
                    pc++;
                    if (CompareToken(pc, pszArchString) ||
                        CompareToken(pc, pszArchString2))
                    {
                        included = 1;
                    }

                    /* Skip to next arch or end */
                    while (*pc > ',') pc++;
                }
            }
            else if (CompareToken(pc, "-i386"))
            {
                if (strcasecmp(pszArchString, "i386") != 0) included = 0;
            }
            else if (CompareToken(pc, "-private"))
            {
                exp.uFlags |= FL_PRIVATE;
            }
            else if (CompareToken(pc, "-noname") ||
                     CompareToken(pc, "-ordinal"))
            {
                exp.uFlags |= FL_NONAME;
            }
            else if (CompareToken(pc, "-stub"))
            {
                exp.uFlags |= FL_STUB;
            }
            else if (CompareToken(pc, "-norelay") ||
                     CompareToken(pc, "-register") ||
                     CompareToken(pc, "-ret64"))
            {
                /* silently ignore these */
            }
            else
            {
                fprintf(stderr, "info: ignored option: '%.*s'\n",
                        TokenLength(pc), pc);
            }

            /* Go to next token */
            pc = NextToken(pc);
        }

        //fprintf(stderr, "info: Name:'%.10s'\n", pc);

        /* If arch didn't match ours, skip this entry */
        if (!included) continue;

        /* Get name */
        exp.pcName = pc;
        exp.nNameLength = TokenLength(pc);

        /* Handle parameters */
        exp.nStackBytes = 0;
        if (exp.nCallingConvention != CC_EXTERN &&
            exp.nCallingConvention != CC_STUB)
        {
            //fprintf(stderr, "info: options:'%.10s'\n", pc);
            /* Go to next token */
            if (!(pc = NextToken(pc)))
            {
                fprintf(stderr, "fail4\n");
                return -13;
            }

            /* Verify syntax */
            if (*pc++ != '(')
            {
                fprintf(stderr, "error: line %d, expected '('\n", nLine);
                return -14;
            }

            /* Skip whitespaces */
            while (*pc == ' ' || *pc == '\t') pc++;

            exp.nStackBytes = 0;
            while (*pc >= '0')
            {
                if (CompareToken(pc, "long"))
                {
                    exp.nStackBytes += 4;
                    exp.anArgs[exp.nArgCount] = ARG_LONG;
                }
                else if (CompareToken(pc, "double"))
                {
                    exp.nStackBytes += 8;
                    exp.anArgs[exp.nArgCount] = ARG_DBL;
                }
                else if (CompareToken(pc, "ptr") ||
                         CompareToken(pc, "str") ||
                         CompareToken(pc, "wstr"))
                {
                    exp.nStackBytes += 4; // sizeof(void*) on x86
                    exp.anArgs[exp.nArgCount] = ARG_PTR; // FIXME: handle strings
                }
                else if (CompareToken(pc, "int64"))
                {
                    exp.nStackBytes += 8;
                    exp.anArgs[exp.nArgCount] = ARG_INT64;
                }
                else
                    fprintf(stderr, "error: line %d, expected type, got: %.10s\n", nLine, pc);

                exp.nArgCount++;

                /* Go to next parameter */
                if (!(pc = NextToken(pc)))
                {
                    fprintf(stderr, "fail5\n");
                    return -15;
                }
            }

            /* Check syntax */
            if (*pc++ != ')')
            {
                fprintf(stderr, "error: line %d, expected ')'\n", nLine);
                return -16;
            }
        }

        /* Handle special stub cases */
        if (exp.nCallingConvention == CC_STUB)
        {
            /* Check for c++ mangled name */
            if (pc[0] == '?')
            {
                printf("Found c++ mangled name...\n");
                //
            }
            else
            {
                /* Check for stdcall name */
                char *p = strchr(pc, '@');
                if (p && (p - pc < exp.nNameLength))
                {
                    int i;
                    exp.nNameLength = p - pc;
                    if (exp.nNameLength < 1)
                    {
                        fprintf(stderr, "error, @ in line %d\n", nLine);
                        return -1;
                    }
                    exp.nStackBytes = atoi(p + 1);
                    exp.nArgCount =  exp.nStackBytes / 4;
                    exp.nCallingConvention = CC_STDCALL;
                    exp.uFlags |= FL_STUB;
                    for (i = 0; i < exp.nArgCount; i++)
                        exp.anArgs[i] = ARG_LONG;
                }
            }
        }

        /* Get optional redirection */
        if ((pc = NextToken(pc)))
        {
            exp.pcRedirection = pc;
            exp.nRedirectionLength = TokenLength(pc);

            /* Check syntax (end of line) */
            if (NextToken(pc))
            {
                 fprintf(stderr, "error: line %d, additional tokens after ')'\n", nLine);
                 return -17;
            }
        }
        else
        {
            exp.pcRedirection = 0;
            exp.nRedirectionLength = 0;
        }

        OutputLine(fileDest, &exp);
    }

    return 0;
}


void usage(void)
{
    printf("syntax: spec2pdef [<options> ...] <spec file>\n"
           "Possible options:\n"
           "  -h --help   prints this screen\n"
           "  -l=<file>   generates an asm lib stub\n"
           "  -d=<file>   generates a def file\n"
           "  -s=<file>   generates a stub file\n"
           "  --ms        msvc compatibility\n"
           "  -n=<name>   name of the dll\n"
           "  --kill-at   removes @xx decorations from exports\n"
           "  -r          removes redirections from def file\n"
           "  -a=<arch>   Set architecture to <arch>. (i386, x86_64, arm)\n");
}

int main(int argc, char *argv[])
{
    size_t nFileSize;
    char *pszSource, *pszDefFileName = 0, *pszStubFileName = 0, *pszLibStubName = 0;
    char achDllName[40];
    FILE *file;
    int result, i;

    if (argc < 2)
    {
        usage();
        return -1;
    }

    /* Read options */
    for (i = 1; i < argc && *argv[i] == '-'; i++)
    {
        if ((strcasecmp(argv[i], "--help") == 0) ||
            (strcasecmp(argv[i], "-h") == 0))
        {
            usage();
            return 0;
        }
        else if (argv[i][1] == 'd' && argv[i][2] == '=')
        {
            pszDefFileName = argv[i] + 3;
        }
        else if (argv[i][1] == 'l' && argv[i][2] == '=')
        {
            pszLibStubName = argv[i] + 3;
        }
        else if (argv[i][1] == 's' && argv[i][2] == '=')
        {
            pszStubFileName = argv[i] + 3;
        }
        else if (argv[i][1] == 'n' && argv[i][2] == '=')
        {
            pszDllName = argv[i] + 3;
        }
        else if ((strcasecmp(argv[i], "--kill-at") == 0))
        {
            gbKillAt = 1;
        }
        else if ((strcasecmp(argv[i], "--ms") == 0))
        {
            gbMSComp = 1;
        }
        else if ((strcasecmp(argv[i], "-r") == 0))
        {
            no_redirections = 1;
        }
        else if (argv[i][1] == 'a' && argv[i][2] == '=')
        {
            pszArchString = argv[i] + 3;
        }
        else
        {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            return -1;
        }
    }

    if ((strcasecmp(pszArchString, "x86_64") == 0) ||
        (strcasecmp(pszArchString, "ia64") == 0))
    {
        pszArchString2 = "win64";
    }
    else
        pszArchString2 = "win32";

    if (strcasecmp(pszArchString, "i386") == 0)
    {
        gbUseDeco = 1;
    }

    /* Set a default dll name */
    if (!pszDllName)
    {
        char *p1, *p2;
        int len;

        p1 = strrchr(argv[i], '\\');
        if (!p1) p1 = strrchr(argv[i], '/');
        p2 = p1 = p1 ? p1 + 1 : argv[i];

        /* walk up to '.' */
        while (*p2 != '.' && *p2 != 0) p2++;
        len = p2 - p1;
        if (len >= sizeof(achDllName) - 5)
        {
            fprintf(stderr, "name too long: %s\n", p1);
            return -2;
        }

        strncpy(achDllName, p1, len);
        strncpy(achDllName + len, ".dll", sizeof(achDllName) - len);
        pszDllName = achDllName;
    }

    /* Open input file argv[1] */
    file = fopen(argv[i], "r");
    if (!file)
    {
        fprintf(stderr, "error: could not open file %s ", argv[i]);
        return -3;
    }

    /* Get file size */
    fseek(file, 0, SEEK_END);
    nFileSize = ftell(file);
    rewind(file);

    /* Allocate memory buffer */
    pszSource = malloc(nFileSize + 1);
    if (!pszSource) return -4;

    /* Load input file into memory */
    nFileSize = fread(pszSource, 1, nFileSize, file);
    fclose(file);

    /* Zero terminate the source */
    pszSource[nFileSize] = '\0';

    if (pszDefFileName)
    {
        /* Open output file */
        file = fopen(pszDefFileName, "w");
        if (!file)
        {
            fprintf(stderr, "error: could not open output file %s ", argv[i + 1]);
            return -5;
        }

        OutputHeader_def(file, pszDllName);
        result = ParseFile(pszSource, file, OutputLine_def);
        fclose(file);
    }

    if (pszStubFileName)
    {
        /* Open output file */
        file = fopen(pszStubFileName, "w");
        if (!file)
        {
            fprintf(stderr, "error: could not open output file %s ", argv[i + 1]);
            return -5;
        }

        OutputHeader_stub(file);
        result = ParseFile(pszSource, file, OutputLine_stub);
        fclose(file);
    }

    if (pszLibStubName)
    {
        /* Open output file */
        file = fopen(pszLibStubName, "w");
        if (!file)
        {
            fprintf(stderr, "error: could not open output file %s ", argv[i + 1]);
            return -5;
        }

        OutputHeader_asmstub(file, pszDllName);
        result = ParseFile(pszSource, file, OutputLine_asmstub);
        fprintf(file, "\nEND\n");
        fclose(file);
    }


    return result;
}
