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

void sqlite_result_push(struct sqlite_result *p, int argc, char **argv)
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

sqlite3* gethandle(char *name, char *varname)
{
    const char *s_handle = getsparam(varname);
    if (s_handle == NULL) {
        zwarnnam(name, "failed to get handle from variable %s", varname);
        return NULL;
    }

    char *endptr;
    sqlite3 *pdb = (sqlite3 *)strtol(s_handle, &endptr, 10);
    if (*endptr != '\0') {
        zwarnnam(name, "failed to parse handle from variable %s", varname);
        return NULL;
    }

    return pdb;
}


// Usage: zsqlite_open DB ./sqlite.db
static int bin_zsqlite_open(char *name, char **args, Options ops, UNUSED(int func))
{
    if (args[0] == NULL || args[1] == NULL) {
        zwarnnam(name, "too few arguments");
        return 1;
    }

    const char *varname = args[0];
    const char *filename = unmeta(args[1]);

    sqlite3 *pdb;
    if (sqlite3_open(filename, &pdb)) {
        zwarnnam(name, "failed to open database at %s", filename);
        return 1;
    }

    char buf[21];
    sprintf(buf, "%ld", (long)pdb);

    setsparam(varname, ztrdup(buf));

    return 0;
}

// Usage: zsqlite_close DB
static int bin_zsqlite_close(char *name, char **args, Options ops, UNUSED(int func))
{
    if (args[0] == NULL) {
        zwarnnam(name, "too few arguments");
        return 1;
    }

    char *varname = args[0];

    sqlite3 *pdb = gethandle(name, varname);
    if (pdb == NULL) {
        return 1;
    }

    sqlite3_close(pdb);

    unsetparam(varname);
}

// Usage:
// zsqlite_exec DB 'SELECT 1'
// zsqlite_exec DB -v out_var 'SELECT 1'
// zsqlite_exec DB -s ':' -h 'SELECT 1'
static int bin_zsqlite_exec(char *name, char **args, Options ops, int func)
{
    if (args[0] == NULL || args[1] == NULL) {
        zwarnnam(name, "too few arguments");
        return 1;
    }

    sqlite3 *pdb = NULL;
    if (func == BIN_ZSQLITE_EXEC) {
        if ((pdb = gethandle(name, args[0])) == NULL) {
            return 1;
        }
    } else {
        char *filename = unmeta(args[0]);
        if (sqlite3_open(filename, &pdb)) {
            zwarnnam(name, "failed to open database at %s", filename);
            return 1;
        }
    }

    char *sql = unmetafy(args[1], NULL);

    struct sqlite_result result = { 0, 0, 0, NULL, NULL};
    char *errmsg;
    if (sqlite3_exec(pdb, sql, sqlite_callback, &result, &errmsg) != SQLITE_OK) {
        // TODO: should I metafy errmsg here?
        zwarnnam(name, "failed to execute sql: %s", errmsg);
        sqlite3_free(errmsg);
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
        // TODO: unmeta sep
        char *sep = OPT_ISSET(ops, 's') ? OPT_ARG(ops, 's') : "|";
        bool header = OPT_ISSET(ops, 'h');

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
                unmetafy(result.coldata[j][i], NULL);
                printf("%s%s", j == 0 ? "" : sep, result.coldata[j][i]);
            }
            putchar('\n');
        }
        for (int i = 0; i < result.collength; i++) {
            if (result.coldata[i]) {
                freearray(result.coldata[i]);
            }
        }
    }

    free(result.coldata);
    free(result.colname);

    return 0;
}


static struct builtin bintab[] = {
    BUILTIN("zsqlite_open", 0, bin_zsqlite_open, 2, -1, 0, NULL, NULL),
    BUILTIN("zsqlite_exec", 0, bin_zsqlite_exec, 2, -1, BIN_ZSQLITE_EXEC, "hs:v:", NULL),
    BUILTIN("zsqlite_close", 0, bin_zsqlite_close, 1, -1, 0, NULL, NULL),
    BUILTIN("zsqlite", 0, bin_zsqlite_exec, 2, -1, BIN_ZSQLITE, NULL, NULL),
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
        sqlite_module_version = ztrdup("0.2.1");
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
