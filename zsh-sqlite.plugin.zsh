#!/hint/zsh

0="${${ZERO:-${0:#$ZSH_ARGZERO}}:-${(%):-%N}}"
0="${${(M)0:#/*}:-$PWD/$0}"

ZSH_SQLITE_HOME=${0:A:h}
ZSH_SQLITE_ZSH_SRC=5.8.1

autoload -Uz is-at-least

zsh-sqlite-build() {
  local ret macos nproc
  if [[ $OSTYPE == darwin* ]]; then
    macos=true
    nproc=$(sysctl -n hw.logicalcpu)
  else
    nproc=$(nproc)
  fi

  pushd $ZSH_SQLITE_HOME/zsh/$ZSH_SQLITE_ZSH_SRC
  [[ -f ./configure ]] || ./Util/preconfig
  LIBS=-lsqlite3 ./configure --disable-gdbm --disable-pcre --without-tcsetpgrp ${macos:+DL_EXT=bundle}
  make -j$nproc
  ret=$?
  popd

  if (( $ret != 0 )); then
    print -P -u2 "%F{red}%BThe module building has failed. See the output above for details.%f%b"
    return $ret
  fi

  pushd $ZSH_SQLITE_HOME/zsh/$ZSH_SQLITE_ZSH_SRC/Src/Modules
  mkdir -p aloxaf
  mv sqlite.so aloxaf/
  rm -f *.so
  popd

  print -P "%F{green}%BThe module has been built successfully. Please restart zsh to apply it.%f%b"
}

() {
  if is-at-least 5.4.2; then
    ZSH_SQLITE_ZSH_SRC=5.8.1
  fi

  local sqlite_module_path=$ZSH_SQLITE_HOME/zsh/$ZSH_SQLITE_ZSH_SRC/Src/Modules
  if (( ! $module_path[(I)$sqlite_module_path] )); then
    module_path+=($sqlite_module_path)
  fi

  if [[ -z $sqlite_module_path/aloxaf/sqlite.(so|bundle)(#qN) ]]; then
    print -P "%F{yellow}%BThe module hasn't been build. Please build it with 'zsh-sqlite-build'%f%b"
    return 1
  fi

  zmodload aloxaf/sqlite
  if [[ $SQLITE_MODULE_VERSION != "0.1.1" ]]; then
    print -P "%F{yellow}%BThe module is outdate. Please rebuild it with 'zsh-sqlite-build'%f%b"
    return 1
  fi
}
