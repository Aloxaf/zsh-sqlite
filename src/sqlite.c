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

/// puts a string, quoted if contains special characters
static int quotedputs(const char *s, FILE *stream)
{
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

static sqlite3* zsqlite_open(char *name, char *database, Options ops)
{
    sqlite3 *pdb;
    int busy_timeout = 10, flags = 0;
    if (OPT_ISSET(ops, 't')) {
        busy_timeout = (int)getposlong(OPT_ARG(ops, 't'), name);
        if (busy_timeout < -1) {
            return NULL;
        }
    }
    if (OPT_ISSET(ops, 'r')) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    }
    if (sqlite3_open_v2(unmeta(database), &pdb, flags, NULL)) {
        zwarnnam(name, "failed to open database at %s", database);
        return NULL;
    }
    sqlite3_busy_timeout(pdb, busy_timeout);

    return pdb;
}

// Usage: zsqlite_open DB ./sqlite.db
static int bin_zsqlite_open(char *name, char **args, Options ops, UNUSED(int func))
{
    sqlite3 *pdb = zsqlite_open(name, args[1], ops);

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
// zsqlite_exec DB 'SELECT ?' 1
// zsqlite_exec DB -v out_var 'SELECT 1'
// zsqlite_exec DB -s ':' -h 'SELECT 1'
static int bin_zsqlite_exec(char *name, char **args, Options ops, int func)
{
    int rc = 0;
    sqlite3 *db = NULL;
    if (func == BIN_ZSQLITE_EXEC) {
        if ((db = gethandle(name, args[0])) == NULL) {
            return 1;
        }
    } else {
        if ((db = zsqlite_open(name, args[0], ops)) == NULL)  {
            return 1;
        }
    }

    char *sql = unmetafy(args[1], NULL);
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        zwarnnam(name, "failed to prepare sql: %s", ztrdup_metafy(sqlite3_errmsg(db)));
        rc = 1;
        goto end;
    }

    for (int i = 2; args[i]; i++) {
        unmetafy(args[i], NULL);
        if (sqlite3_bind_text(stmt, i - 1, args[i], -1, SQLITE_TRANSIENT) != SQLITE_OK) {
            zwarnnam(name, "failed to bind parameter %d: %s", i, ztrdup_metafy(sqlite3_errmsg(db)));
            rc = 1;
            goto end;
        }
    }

    int nCol = 0;
    char **azCols = NULL, **azVars = NULL;
    bool isColsInit = false;

    struct sqlite_result result = { 0, 0, 0, NULL, NULL};

    char *sep = OPT_ISSET(ops, 's') ? OPT_ARG(ops, 's') : "|";
    bool printHeader = OPT_ISSET(ops, 'h'), quoteResult = OPT_ISSET(ops, 'q'), writeVar = OPT_ISSET(ops, 'v');
    unmetafy(sep, NULL);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!isColsInit) {
            nCol = sqlite3_column_count(stmt);
            azCols = zshcalloc((2 * nCol + 1) * sizeof(char *));
            for (int i = 0; i < nCol; i++) {
                azCols[i] = (char *) sqlite3_column_name(stmt, i);
            }
            isColsInit = true;

            if (!writeVar && printHeader) {
                for (int i = 0; i < nCol; i++) {
                    printf("%s%s", i == 0 ? "" : sep, azCols[i]);
                }
                putchar('\n');
            }
        }

        azVars = &azCols[nCol];
        for (int i = 0; i < nCol; i++) {
            azVars[i] = (char *) sqlite3_column_text(stmt, i);
            if (azVars[i] == NULL) {
                // TODO: custom NUL value
                azVars[i] = ztrdup("");
            }
        }

        if (writeVar) {
            sqlite_callback(&result, nCol, azVars, azCols);
        } else {
            for (int i = 0; i < nCol; i++) {
                fputs(i == 0 ? "" : sep, stdout);
                if (quoteResult) {
                    quotedputs(azVars[i], stdout);
                } else {
                    fputs(azVars[i], stdout);
                }
            }
            putchar('\n');
        }
    }
    free(azCols);

    if (OPT_ISSET(ops, 'v')) {
        char *oVar = OPT_ARG(ops, 'v');
        char **colNames = zshcalloc((result.collength + 1) * sizeof(char *));
        for (int i = 0; i < result.collength; i++) {
            colNames[i] = tricat(oVar, "_", result.colname[i]);
            setaparam(colNames[i], result.coldata[i]);
            free(result.colname[i]);
        }
        setaparam(oVar, colNames);
        free(result.coldata);
        free(result.colname);
    }

end:
    if (func == BIN_ZSQLITE) {
        if (db) {
            sqlite3_close(db);
        }
    }
    sqlite3_finalize(stmt);

    return rc;
}


static struct builtin bintab[] = {
    BUILTIN("zsqlite_open", 0, bin_zsqlite_open, 2, 2, 0, "t:r", NULL),
    BUILTIN("zsqlite_exec", 0, bin_zsqlite_exec, 2, -1, BIN_ZSQLITE_EXEC, "hs:v:q", NULL),
    BUILTIN("zsqlite_close", 0, bin_zsqlite_close, 1, 1, 0, NULL, NULL),
    BUILTIN("zsqlite", 0, bin_zsqlite_exec, 2, -1, BIN_ZSQLITE, "hs:v:t:rq", NULL),
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
