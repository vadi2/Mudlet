#!/bin/bash
# Usage: fetch.sh <dest>  - downloads the Lua C module sources the Windows package ships
set -euo pipefail
dest="$1"
mkdir -p "$dest"
cd "$dest"
get() {
  local name="$1" url="$2"
  mkdir -p "$name"
  curl -fsSL --retry 5 -o "$name.tar.gz" "$url"
  tar xzf "$name.tar.gz" -C "$name" --strip-components=1
}
get lfs https://github.com/lunarmodules/luafilesystem/archive/refs/tags/v1_9_0.tar.gz
get luautf8 https://github.com/starwing/luautf8/archive/refs/tags/0.2.1.tar.gz
get lua-yajl https://github.com/brimworks/lua-yajl/archive/1dab6c43bad1142a53e168773b6e9f95f9dc2b75.tar.gz
get lua-zip https://github.com/brimworks/lua-zip/archive/refs/tags/v0.2.0.tar.gz
get lrexlib https://github.com/rrthomas/lrexlib/archive/refs/tags/rel-2-9-2.tar.gz
get luasql https://github.com/lunarmodules/luasql/archive/refs/tags/2.6.0.tar.gz
get lpeg https://github.com/roberto-ieru/LPeg/archive/refs/tags/v1.1.0.tar.gz
curl -fsSL --retry 5 -o lsqlite3.zip 'http://lua.sqlite.org/home/zip/lsqlite3_v096.zip?uuid=v0.9.6'
unzip -q lsqlite3.zip || python -c "import zipfile; zipfile.ZipFile('lsqlite3.zip').extractall()"
mv lsqlite3_v096 lsqlite3
ls
