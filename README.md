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

- `zsqlite_open [-t MS] [-r] HANDLE_VAR FILENAME`
    - `-r`: read-only
- `zsqlite_close HANDLE_VAR`
    - `-t MS`: set sqlite3 busy timeout, default is 10
    - `HANDLE_VAR`: set the variable name to store the database handle


- `zsqlite [-t MS] [-r] [-s SEP] [-h] [-v VAR] FILENAME SQL`
- `zsqlite_exec [-s SEP] [-h] [-v VAR] HANDLE_VAR SQL`
  - `-s SEP`: set the separator between columns, default is '|'
  - `-h`: show column headers
  - `-v VAR`: set the variable name to store the result


### Examples

```zsh
> zsqlite ':memory:' 'SELECT 1'
1

> zsqlite_open DB ':memory:'
> zsqlite_exec DB 'CREATE TABLE Person(name TEXT, age INT)'
> zsqlite_exec DB 'INSERT INTO Person VALUES ("Alice", 20), ("Bob", 21)'
> zsqlite_exec DB 'SELECT * FROM Person'
Alice|20
Bob|21

> zsqlite_exec -s ":" -h DB 'SELECT * FROM Person'
name:age
Alice:20
Bob:21

> zsqlite_exec -v OUT_VAR DB 'SELECT * FROM Person'
> echo $OUT_VAR
OUT_VAR_name OUT_VAR_age

> echo $OUT_VAR_name
Alice Bob

> echo $OUT_VAR_age
20 21

> zsqlite_close DB
```
