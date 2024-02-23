#include "sqlite.mdh" 
#include "sqlite.pro"
#include <sqlite3.h>

enum {
    BIN_ZSQLITE_EXEC,
    BIN_ZSQLITE,
};

static char *sqlite_module_version;

struct sqlite_result {
    // the count of result
    int length;
    // the total capacity of coldata
    int capacity;
    // the number of column
    int collength;
    // an array of column names, ends in NULL
    char **colname;
    // A 2d array of column data, ends in NULL
    char ***coldata;
};

static void sqlite_result_push(struct sqlite_result *p, int argc, char **argv)
{
    if (p->coldata == NULL) {
        p->coldata = zshcalloc(argc * sizeof(char **));
        for (int i = 0; i < argc; i++) {
            p->coldata[i] = zshcalloc(4 * sizeof(char *));
        }
        p->capacity = 4;
    }
    // Why +1? Because the last element should be NULL
    if (p->capacity == p->length + 1) {
        p->capacity *= 1.5;
        for (int i = 0; i < argc; i++) {
            p->coldata[i] = zrealloc(p->coldata[i], p->capacity * sizeof(char *));
        }
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) {
            p->coldata[i][p->length] = ztrdup("");
        } else {
            p->coldata[i][p->length] = ztrdup_metafy(argv[i]);
        }
        // ensure last element is NULL
        p->coldata[i][p->length + 1] = NULL;
    }
    p->length += 1;
}

static int sqlite_callback(void *output, int argc, char **argv, char **colname)
{
    struct sqlite_result *p = output;

    if (p->colname == NULL) {
        p->colname = zshcalloc((argc + 1) * sizeof(char *));
        for (int i = 0; i < argc; i++) {
            p->colname[i] = ztrdup_metafy(colname[i]);
        }
        p->collength = argc;
    }

    sqlite_result_push(p, argc, argv);

    return 0;
}

static long getposlong(char *instr, char *nam)
{
    char *eptr;
    long ret;

    ret = zstrtol(instr, &eptr, 10);
    if (*eptr || ret < 0) {
        zwarnnam(nam, "integer expected: %s", instr);
        return -1;
    }

    return ret;
}

static sqlite3* gethandle(char *name, char *varname)
{
    char *s_handle = getsparam(varname);
    if (s_handle == NULL) {
        zwarnnam(name, "failed to get handle from variable %s", varname);
        return NULL;
    }

    long ptr = getposlong(s_handle, name);
    if (ptr < 0) {
        return NULL;
    }
    sqlite3 *pdb = (sqlite3 *)ptr;

    return pdb;
}

/// Unmetafy and output a string, quoted if contains special characters
static int quotedputs(char *s, FILE *stream)
{
    unmetafy(s, NULL);
    for (; *s; s++) {
        switch (*s) {
            case '\n':
                putc('\\', stream);
                putc('n', stream);
                break;
            case '\\':
                putc('\\', stream);
                putc('\\', stream);
                break;
            default:
                putc(*s, stream);
        }
    }
    return 0;
}

// Usage: zsqlite_open DB ./sqlite.db
static int bin_zsqlite_open(char *name, char **args, Options ops, UNUSED(int func))
{
    sqlite3 *pdb;
    int flags = 0;
    int busy_timeout = 10;

    if (OPT_ISSET(ops, 't')) {
        busy_timeout = (int)getposlong(OPT_ARG(ops, 't'), name);
        if (busy_timeout < -1) {
            return 1;
        }
    }
    if (OPT_ISSET(ops, 'r')) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    }

    if (sqlite3_open_v2(unmeta(args[1]), &pdb, flags, NULL)) {
        zwarnnam(name, "failed to open database at %s", args[1]);
        return 1;
    }

    sqlite3_busy_timeout(pdb, busy_timeout);

    char buf[21];
    sprintf(buf, "%ld", (long)pdb);

    setsparam(args[0], ztrdup(buf));

    return 0;
}

// Usage: zsqlite_close DB
static int bin_zsqlite_close(char *name, char **args, Options ops, UNUSED(int func))
{
    char *varname = args[0];

    sqlite3 *pdb = gethandle(name, varname);
    if (pdb == NULL) {
        return 1;
    }

    sqlite3_close(pdb);

    unsetparam(varname);

    return 0;
}

// Usage:
// zsqlite_exec DB 'SELECT 1'
// zsqlite_exec DB -v out_var 'SELECT 1'
// zsqlite_exec DB -s ':' -h 'SELECT 1'
static int bin_zsqlite_exec(char *name, char **args, Options ops, int func)
{
    sqlite3 *pdb = NULL;
    if (func == BIN_ZSQLITE_EXEC) {
        if ((pdb = gethandle(name, args[0])) == NULL) {
            return 1;
        }
    } else {
        // TODO: duplicate code
        int busy_timeout = 10, flags = 0;
        if (OPT_ISSET(ops, 't')) {
            busy_timeout = (int)getposlong(OPT_ARG(ops, 't'), name);
            if (busy_timeout < -1) {
                return 1;
            }
        }
        if (OPT_ISSET(ops, 'r')) {
            flags |= SQLITE_OPEN_READONLY;
        } else {
            flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        }
        if (sqlite3_open_v2(unmeta(args[0]), &pdb, flags, NULL)) {
            zwarnnam(name, "failed to open database at %s", args[0]);
            return 1;
        }
        sqlite3_busy_timeout(pdb, busy_timeout);
    }

    char *sql = unmetafy(args[1], NULL);

    struct sqlite_result result = { 0, 0, 0, NULL, NULL};
    char *errmsg;
    if (sqlite3_exec(pdb, sql, sqlite_callback, &result, &errmsg) != SQLITE_OK) {
        char *merrmsg = ztrdup_metafy(errmsg);
        zwarnnam(name, "failed to execute sql: %s", merrmsg);
        sqlite3_free(errmsg);
        free(merrmsg);
        if (func == BIN_ZSQLITE) {
            sqlite3_close(pdb);
        }
        return 1;
    }

    if (OPT_ISSET(ops, 'v')) {
        char *outvar = OPT_ARG(ops, 'v');
        char **colnames = zshcalloc((result.collength + 1) * sizeof(char *));
        for (int i = 0; i < result.collength; i++) {
            colnames[i] = zshcalloc(512 * sizeof(char));
            sprintf(colnames[i], "%s_%s", outvar, result.colname[i]);
            setaparam(colnames[i], result.coldata[i]);
            free(result.colname[i]);
        }
        setaparam(outvar, colnames);
    } else {
        char *sep = OPT_ISSET(ops, 's') ? OPT_ARG(ops, 's') : "|";
        bool header = OPT_ISSET(ops, 'h'), quote = OPT_ISSET(ops, 'q');

        unmetafy(sep, NULL);

        for (int i = 0; i < result.collength; i++) {
            if (header) {
                unmetafy(result.colname[i], NULL);
                printf("%s%s", i == 0 ? "" : sep, result.colname[i]);
            }
            free(result.colname[i]);
        }
        if (header) {
            putchar('\n');
        }

        for (int i = 0; i < result.length; i++) {
            for (int j = 0; j < result.collength; j++) {
                fputs(j == 0 ? "" : sep, stdout);
                if (quote) {
                    quotedputs(result.coldata[j][i], stdout);
                }
            }
            putchar('\n');
        }
        for (int i = 0; i < result.collength; i++) {
            if (result.coldata[i]) {
                freearray(result.coldata[i]);
            }
        }
    }

    if (func == BIN_ZSQLITE) {
        sqlite3_close(pdb);
    }

    free(result.coldata);
    free(result.colname);

    return 0;
}


static struct builtin bintab[] = {
    BUILTIN("zsqlite_open", 0, bin_zsqlite_open, 2, 2, 0, "t:r", NULL),
    BUILTIN("zsqlite_exec", 0, bin_zsqlite_exec, 2, 2, BIN_ZSQLITE_EXEC, "hs:v:", NULL),
    BUILTIN("zsqlite_close", 0, bin_zsqlite_close, 1, 1, 0, NULL, NULL),
    BUILTIN("zsqlite", 0, bin_zsqlite_exec, 2, 2, BIN_ZSQLITE, "hs:v:t:rq", NULL),
};

static struct paramdef patab[] = {
    STRPARAMDEF("SQLITE_MODULE_VERSION", &sqlite_module_version),
};

// clang-format off
static struct features module_features = {
    bintab, sizeof(bintab) / sizeof(*bintab),
    NULL, 0,
    NULL, 0,
    patab, sizeof(patab) / sizeof(*patab),
    0,
};
// clang-format on

int setup_(UNUSED(Module m))
{
    return 0;
}

int features_(Module m, char*** features)
{
    *features = featuresarray(m, &module_features);
    return 0;
}


int enables_(Module m, int** enables)
{
    return handlefeatures(m, &module_features, enables);
}

int boot_(UNUSED(Module m))
{
    if (sqlite_module_version == NULL) {
        sqlite_module_version = ztrdup("0.3.0");
    }
    return 0;
}

int cleanup_(UNUSED(Module m))
{
    return setfeatureenables(m, &module_features, NULL);
}

int finish_(UNUSED(Module m))
{
    printf("aloxaf/sqlite module unloaded.\n");
    fflush(stdout);
    return 0;
}
