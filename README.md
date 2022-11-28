# zsh-sqlite

Bring sqlite to zsh

## Install

First, install it with your plugin manager

**zinit**

```zsh
zinit ice nocompletions
zinit light Aloxaf/zsh-sqlite
```

Then, build it with `zsqlite-build`

```zsh
# By default, zsh-sqlite will build against current zsh version. And it will ask you to
# rebuild it everytime your zsh update. However, you can pin the zsh version it use
# by setting something like `ZSQLITE_ZSH_SRC_VERSION=5.8.1`.
zsqlite-build
```

### Build dependencies

- basic build tools(autoconf, make, gcc, ...)
- ncurses
- sqlite3

## Usage

```zsh
zsqlite_open DB_VAR /tmp/sqlite.db
zsqlite_exec DB_VAR OUT_VAR 'CREATE TABLE Person(name TEXT, age INT);'
zsqlite_exec DB_VAR OUT_VAR 'INSERT INTO Person VALUES ("Alice", 20), ("Bob", 21);'
zsqlite_exec DB_VAR OUT_VAR 'SELECT * FROM Person;'
zsqlite_close DB_VAR
echo $OUT_VAR
echo $OUT_VAR_name
echo $OUT_VAR_age
```
