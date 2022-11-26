#include "sqlite.mdh" 
#include "sqlite.pro"
#include <sqlite3.h>

struct sqlite_result {
    int length;
    int capacity;
    int collength;
    char **colname;
    char ***coldata;
};

void sqlite_result_push(struct sqlite_result *p, int argc, char **argv)
{
    if (p->coldata == NULL) {
        p->coldata = calloc(argc, sizeof(char **));
        for (int i = 0; i < argc; i++) {
            p->coldata[i] = calloc(4, sizeof(char *));
        }
        p->capacity = 4;
    }
    if (p->capacity == p->length) {
        p->capacity *= 1.5;
        for (int i = 0; i < argc; i++) {
            p->coldata[i] = reallocarray(p->coldata[i], p->capacity, sizeof(char *));
        }
    }

    for (int i = 0; i < argc; i++) {
        p->coldata[i][p->length] = metafy(argv[i], -1, META_DUP);
    }
    p->length += 1;
}

static int sqlite_callback(void *output, int argc, char **argv, char **colname)
{
    struct sqlite_result *p = output;

    if (p->colname == NULL) {
        p->colname = calloc(argc, sizeof(char *));
        for (int i = 0; i < argc; i++) {
            p->colname[i] = metafy(colname[i], -1, META_DUP);
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


// Usage: zsqlite_open HANDLE ./sqlite.db
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

// Usage: zsqlite_close HANDLE
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

// Usage: zsqlite_exec HANDLE OUTVAR 'SELECT 1;'
static int bin_zsqlite_exec(char *name, char **args, Options ops, UNUSED(int func))
{
    if (args[0] == NULL || args[1] == NULL || args[2] == NULL) {
        zwarnnam(name, "too few arguments");
        return 1;
    }

    char *varname = args[0];
    sqlite3 *pdb = gethandle(name, varname);
    if (pdb == NULL) {
        return 1;
    }

    char *outvar = args[1];
    char *sql = args[2];

    struct sqlite_result result = { 0, 0, 0, NULL, NULL};
    char *errmsg;
    if (sqlite3_exec(pdb, sql, sqlite_callback, &result, &errmsg) != SQLITE_OK) {
        // TODO: should I metafy errmsg here?
        zwarnnam(name, "failed to execute sql: %s", errmsg);
        sqlite3_free(errmsg);
        return 1;
    }

    for (int i = 0; i < result.collength; i++) {
        char var_column[512];
        sprintf(var_column, "%s_%s", outvar, result.colname[i]);
        setaparam(var_column, result.coldata[i]);
    }

    return 0;
}


static struct builtin bintab[] = {
    BUILTIN("zsqlite_open", 0, bin_zsqlite_open, 0, -1, 0, "f", NULL),
    BUILTIN("zsqlite_exec", 0, bin_zsqlite_exec, 0, -1, 0, "fv", NULL),
    BUILTIN("zsqlite_close", 0, bin_zsqlite_close, 0, -1, 0, "fv", NULL),
};

static struct paramdef patab[] = {};

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

