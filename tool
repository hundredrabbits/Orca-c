#!/usr/bin/env bash
set -eu -o pipefail

print_usage() {
cat <<EOF
Usage: tool [options] command [args]
Commands:
    build <config> <target>
        Compile orca.
        Configs: debug, release
        Targets: orca, cli
        Output: build/<config>/<target>
    clean
        Removes build/
    info
        Prints information about the detected build environment.
Options:
    -v            Print important commands as they're executed.
    -c <name>     Use a specific compiler binary.
                  Default: \$CC, or cc
    -d            Enable compiler safeguards like -fstack-protector.
                  You should probably do this if you plan to give the
                  compiled binary to other people.
    --static      Build static binary.
    --pie         Enable PIE (ASLR).
                  Note: --pie and --static cannot be mixed.
    -s            Print statistics about compile time and binary size.
    -h or --help  Print this message and exit.
Optional Features:
    --portmidi    Enable hardware MIDI output support with PortMIDI.
                  Default: not enabled
                  Note: PortMIDI has memory leaks and bugs.
EOF
}

os=
case $(uname -s | awk '{print tolower($0)}') in
  linux*) os=linux;;
  darwin*) os=mac;;
  cygwin*) os=cygwin;;
  *bsd*) os=bsd;;
  *) os=unknown;;
esac

cc_exe="${CC:-cc}"

verbose=0
protections_enabled=0
stats_enabled=0
pie_enabled=0
static_enabled=0
portmidi_enabled=0

while getopts c:dhsv-: opt_val; do
  case "$opt_val" in
    -)
      case "$OPTARG" in
        help) print_usage; exit 0;;
        static) static_enabled=1;;
        pie) pie_enabled=1;;
        portmidi) portmidi_enabled=1;;
        *)
          echo "Unknown long option --$OPTARG" >&2
          print_usage >&2
          exit 1
          ;;
      esac
      ;;
    c) cc_exe="$OPTARG";;
    d) protections_enabled=1;;
    h) print_usage; exit 0;;
    s) stats_enabled=1;;
    v) verbose=1;;
    \?) print_usage >&2; exit 1;;
    *) break;;
  esac
done

arch=
case $(uname -m) in
  x86_64) arch=x86_64;;
  *) arch=unknown;;
esac

warn() {
  echo "Warning: $*" >&2
}
fatal() {
  echo "Error: $*" >&2
  exit 1
}
script_error() {
  echo "Script error: $*" >&2
  exit 1
}

verbose_echo() {
  if [[ $verbose = 1 ]]; then
    echo "$@"
  fi
  "$@"
}

TIMEFORMAT='%3R'

last_time=

file_size() {
  wc -c < "$1" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'
}

timed_stats() {
  if [[ $stats_enabled = 1 ]]; then
    { last_time=$( { time "$@" 1>&3- 2>&4-; } 2>&1 ); } 3>&1 4>&2
  else
    "$@"
  fi
}

version_string_normalized() {
  echo "$@" | awk -F. '{ printf("%d%03d%03d%03d\n", $1,$2,$3,$4); }';
}

if [[ ($os == bsd) || ($os == unknown) ]]; then
  warn "Build script not tested on this platform"
fi

# This is not perfect by any means
cc_id=
cc_vers=
lld_detected=0
if cc_vers=$(echo -e '#ifndef __clang__\n#error Not found\n#endif\n__clang_major__.__clang_minor__.__clang_patchlevel__' | "$cc_exe" -E -xc - 2>/dev/null | tail -n 1 | tr -d '\040'); then
  cc_id=clang
  # Mac clang/llvm doesn't say the real version of clang. Just assume it's 3.9.0
  if [[ $os == mac ]]; then
    cc_vers=3.9.0
  else
    if command -v "lld" >/dev/null 2>&1; then
      lld_detected=1
    fi
  fi
elif cc_vers=$(echo -e '#ifndef __GNUC__\n#error Not found\n#endif\n__GNUC__.__GNUC_MINOR__.__GNUC_PATCHLEVEL__' | "$cc_exe" -E -xc - 2>/dev/null | tail -n 1 | tr -d '\040'); then
  cc_id=gcc
fi

if [[ -z $cc_id ]]; then
  warn "Failed to detect compiler type"
fi
if [[ -z $cc_vers ]]; then
  warn "Failed to detect compiler version"
fi

cc_vers_is_gte() {
  if [[ $(version_string_normalized "$cc_vers") -ge $(version_string_normalized "$1") ]]; then
    return 0
  else
    return 1
  fi
}

cc_id_and_vers_gte() {
  if [[ $cc_id == "$1" ]] && cc_vers_is_gte "$2"; then
    return 0
  else
    return 1
  fi
}

add() {
  if [[ -z "${1:-}" ]]; then
    script_error "At least one argument required for array add"
  fi
  local array_name
  array_name=${1}
  shift
  eval "$array_name+=($(printf "'%s' " "$@"))"
}

concat() {
  if [[ -z "${1:-}" || -z "${2:-}" ]]; then
    script_error "Two arguments required for array concat"
  fi
  local lhs_name
  local rhs_name
  lhs_name=${1}
  rhs_name=${2}
  eval "$lhs_name+=(\"\${${rhs_name}[@]}\")"
}

try_make_dir() {
  if ! [[ -e "$1" ]]; then
    verbose_echo mkdir "$1"
  elif ! [[ -d "$1" ]]; then
    fatal "File $1 already exists but is not a directory"
  fi
}

build_dir=build

build_target() {
  local build_subdir
  local cc_flags=()
  local libraries=()
  local source_files=()
  local out_exe
  add cc_flags -std=c99 -pipe -finput-charset=UTF-8 -Wall -Wpedantic -Wextra
  if cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 3.9.0; then
    add cc_flags -Wconversion -Wstrict-prototypes -Werror=implicit-function-declaration -Werror=implicit-int -Werror=incompatible-pointer-types -Werror=int-conversion
  fi
  if [[ $lld_detected = 1 ]]; then
    add cc_flags -fuse-ld=lld
  fi
  if [[ $protections_enabled = 1 ]]; then
    add cc_flags -D_FORTIFY_SOURCE=2 -fstack-protector-strong
  fi
  if [[ $pie_enabled = 1 ]]; then
    add cc_flags -pie -fpie -Wl,-pie
  # Only explicitly specify no-pie if cc version is new enough
  elif cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 6.0.0; then
    add cc_flags -no-pie -fno-pie
  fi
  if [[ $static_enabled = 1 ]]; then
    add cc_flags -static
  fi
  case "$1" in
    debug)
      build_subdir=debug
      add cc_flags -DDEBUG -ggdb
      # cygwin gcc doesn't seem to have this stuff, just elide for now
      if [[ $os != cygwin ]]; then
        if cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 3.9.0; then
          add cc_flags -fsanitize=address -fsanitize=undefined
        fi
      fi
      if [[ $os = mac ]]; then
        # Our mac clang does not have -Og
        add cc_flags -O1
      else
        add cc_flags -Og
        # needed if address is already specified? doesn't work on mac clang, at
        # least
        # add cc_flags -fsanitize=leak
      fi
      ;;
    release)
      build_subdir=release
      add cc_flags -DNDEBUG -O2 -g0
      if [[ $protections_enabled != 1 ]]; then
        add cc_flags -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-stack-protector
      fi
      if [[ $os = mac ]]; then
        # todo some stripping option
        true
      else
        # -flto is good on both clang and gcc on Linux
        add cc_flags -flto -s
      fi
      ;;
    *) fatal "Unknown build config \"$1\"";;
  esac

  case $arch in
    x86_64)
      # 'nehalem' tuning actually produces faster code for orca than later
      # archs, for both gcc and clang, even if it's running on a later arch
      # CPU. This is likely due to smaller emitted code size. gcc earlier than
      # 4.9 does not recognize the arch flag for it it, though, and I haven't
      # tested a compiler that old, so I don't know what optimization behavior
      # we get with it is. Just leave it at default, in that case.
      case $cc_id in
        gcc)
          if cc_vers_is_gte 4.9; then
            add cc_flags -march=nehalem
          fi
          ;;
        clang)
          add cc_flags -march=nehalem
          ;;
      esac
      ;;
  esac

  add source_files gbuffer.c field.c mark.c bank.c sim.c
  case "$2" in
    cli)
      add source_files cli_main.c
      out_exe=cli
      ;;
    orca|tui)
      add source_files osc_out.c term_util.c tui_main.c
      add cc_flags -D_XOPEN_SOURCE_EXTENDED=1
      # thirdparty headers (like sokol_time.h) should get -isystem for their
      # include dir so that any warnings they generate with our warning flags
      # are ignored. (sokol_time.h may generate sign conversion warning on
      # mac.)
      add cc_flags -isystem thirdparty
      out_exe=orca
      case $os in
        mac)
          local brew_prefix=
          if ! brew_prefix=$(printenv HOMEBREW_PREFIX); then
             brew_prefix=/usr/local/
          fi
          local ncurses_dir="$brew_prefix/opt/ncurses"
          if ! [[ -d "$ncurses_dir" ]]; then
            echo "Error: ncurses directory not found at $ncurses_dir" >&2
            echo "Install with: brew install ncurses" >&2
            exit 1
          fi
          # prefer homebrew version of ncurses if installed. Will give us
          # better terminfo, so we can use A_DIM in Terminal.app, etc.
          add libraries "-L$ncurses_dir/lib"
          add cc_flags "-I$ncurses_dir/include"
          # todo mach time stuff for mac?
          if [[ $portmidi_enabled = 1 ]]; then
            local portmidi_dir="$brew_prefix/opt/portmidi"
            if ! [[ -d "$portmidi_dir" ]]; then
              echo "Error: PortMIDI directory not found at $portmidi_dir" >&2
              echo "Install with: brew install portmidi" >&2
              exit 1
            fi
            add libraries "-L$portmidi_dir/lib"
            add cc_flags "-I$portmidi_dir/include"
          fi
        ;;
        *)
          # librt and high-res posix timers on Linux
          add libraries -lrt
          add cc_flags -D_POSIX_C_SOURCE=200809L
        ;;
      esac
      add libraries -lmenuw -lformw -lncursesw
      if [[ $portmidi_enabled = 1 ]]; then
        add libraries -lportmidi
        add cc_flags -DFEAT_PORTMIDI
        if [[ $1 = debug ]]; then
          echo -e "Warning: The PortMIDI library contains bugs.\\nIt may trigger address sanitizer in debug builds.\\nThese are not bugs in orca." >&2
        fi
      fi
      ;;
  esac
  try_make_dir "$build_dir"
  try_make_dir "$build_dir/$build_subdir"
  local out_path=$build_dir/$build_subdir/$out_exe
  # bash versions quirk: empty arrays might give error on expansion, use +
  # trick to avoid expanding second operand
  verbose_echo timed_stats "$cc_exe" "${cc_flags[@]}" -o "$out_path" "${source_files[@]}" ${libraries[@]+"${libraries[@]}"}
  if [[ $stats_enabled = 1 ]]; then
    echo "time: $last_time"
    echo "size: $(file_size "$out_path")"
  fi
}

print_info() {
  local linker_name
  if [[ $lld_detected = 1 ]]; then
    linker_name=LLD
  else
    linker_name=default
  fi
  cat <<EOF
Operating system: $os
Architecture:     $arch
Compiler name:    $cc_exe
Compiler type:    $cc_id
Compiler version: $cc_vers
Linker:           $linker_name
EOF
}

shift $((OPTIND - 1))

if [[ -z "${1:-}" ]]; then
  echo "Error: Command required" >&2
  print_usage >&2
  exit 1
fi

case "$1" in
  info)
    if [[ "$#" -gt 1 ]]; then
      fatal "Too many arguments for 'info'"
    fi
    print_info
    exit 0
    ;;
  build)
    if [[ "$#" -lt 3 ]]; then
      fatal "Too few arguments for 'build'"
    fi
    if [[ "$#" -gt 3 ]]; then
      fatal "Too many arguments for 'build'"
    fi
    build_target "$2" "$3"
    ;;
  clean)
    if [[ -d "$build_dir" ]]; then
      verbose_echo rm -rf "$build_dir"
    fi
    ;;
  *) fatal "Unrecognized command $1";;
esac

