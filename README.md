# zsh-sqlite

Bring sqlite to zsh

## Install

Just install it with your plugin manager

### Dependencies

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
