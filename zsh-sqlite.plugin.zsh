#!/hint/zsh

0="${${ZERO:-${0:#$ZSH_ARGZERO}}:-${(%):-%N}}"
0="${${(M)0:#/*}:-$PWD/$0}"

ZSQLITE_HOME=${0:A:h}
ZSQLITE_ZSH_SRC_VERSION=


zsqlite-build() {
  emulate -LR zsh -o extended_glob

  local zsh_version=${1:-${ZSQLITE_ZSH_SRC_VERSION:-$ZSH_VERSION}}

  # macos check
  local ret bundle nproc
  if [[ $OSTYPE == darwin* ]]; then
    [[ -n ${module_path[1]}/**/*.bundle(#qN) ]] && bundle=true
    nproc=$(sysctl -n hw.logicalcpu)
  else
    nproc=$(nproc)
  fi

  pushd -q $ZSQLITE_HOME

  # clone zsh source code if not exists
  if [[ ! -d ./zsh/$zsh_version ]]; then
    git clone --depth=1 --branch zsh-$zsh_version https://github.com/zsh-users/zsh ./zsh/$zsh_version
    ln -s $PWD/src/sqlite.c   ./zsh/$zsh_version/Src/Modules/
    ln -s $PWD/src/sqlite.mdd ./zsh/$zsh_version/Src/Modules/
  fi

  # build zsh
  # TODO: should I use `pkg-config --libs sqlite3` here?
  cd -q ./zsh/$zsh_version
  [[ -f ./configure ]] || ./Util/preconfig
  LIBS=-lsqlite3 ./configure --disable-gdbm --disable-pcre --without-tcsetpgrp --prefix=/tmp/zsh-sqlite ${bundle:+DL_EXT=bundle}
  make -j$nproc
  ret=$?

  if (( $ret != 0 )); then
    print -P -u2 "%F{red}%BThe module building has failed. See the output above for details.%f%b"
    return $ret
  fi

  # we only need aloxaf/sqlite.so
  cd -q ./Src/Modules
  mkdir -p ./build/aloxaf
  mv sqlite.(so|bundle) ./build/aloxaf/

  popd -q

  print -P "%F{green}%BThe module has been built successfully. Please restart zsh to apply it.%f%b"
}


() {
  emulate -L zsh -o extended_glob

  local zsh_version=${ZSQLITE_ZSH_SRC_VERSION:-$ZSH_VERSION}

  local zsqlite_module_path=$ZSQLITE_HOME/zsh/$zsh_version/Src/Modules/build
  if (( ! $module_path[(I)$zsqlite_module_path] )); then
    module_path+=($zsqlite_module_path)
  fi

  if [[ -z $zsqlite_module_path/aloxaf/sqlite.(so|bundle)(#qN) ]]; then
    print -P "%F{yellow}%BThe module hasn't been build for zsh $zsh_version. Please build it with 'zsqlite-build'%f%b"
    return 1
  fi

  zmodload aloxaf/sqlite
  if [[ $SQLITE_MODULE_VERSION != "0.1.2" ]]; then
    print -P "%F{yellow}%BThe module is outdate. Please rebuild it with 'zsqlite-build'%f%b"
    return 1
  fi
}
