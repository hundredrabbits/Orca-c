#!/bin/sh
set -euf

print_usage() {
cat <<EOF
Usage: tool <command> [options] [arguments]
Example:
    tool build --portmidi orca
Commands:
    build <target>
        Compiles the livecoding environment or the CLI tool.
        Targets: orca, cli
        Output: build/<target>
    clean
        Removes build/
    info
        Prints information about the detected build environment.
    help
        Prints this message and exits.
Options:
    -c <name>      Use a specific compiler binary. Default: \$CC, or cc
    -d             Build with debug features. Output changed to:
                   build/debug/<target>
    --harden       Enable compiler safeguards like -fstack-protector.
                   You should probably do this if you plan to give the
                   compiled binary to other people.
    --static       Build static binary.
    --pie          Enable PIE (ASLR).
                   Note: --pie and --static cannot be mixed.
    -s             Print statistics about compile time and binary size.
    -v             Print important commands as they're executed.
    -h or --help   Print this message and exit.
Optional Features:
    --jackmidi     Enable or disable virtual MIDI output support with
    --no-jackmidi  JACK.
                   Default: disabled.
    --portmidi     Enable or disable hardware MIDI output support with
    --no-portmidi  PortMidi. Note: PortMidi has memory leaks and bugs.
                   Default: disabled.
    --mouse        Enable or disable mouse features in the livecoding
    --no-mouse     environment.
                   Default: enabled.
EOF
}

warn() { printf 'Warning: %s\n' "$*" >&2; }
fatal() { printf 'Error: %s\n' "$*" >&2; exit 1; }
script_error() { printf 'Script error: %s\n' "$*" >&2; exit 1; }

if [ -z "${1:-}" ]; then
  printf 'Error: Command required\n' >&2
  print_usage >&2
  exit 1
fi

cmd=$1
shift

case $(uname -s | awk '{print tolower($0)}') in
  linux*) os=linux;;
  darwin*) os=mac;;
  cygwin*) os=cygwin;;
  *bsd*) os=bsd;;
  *) os=unknown; warn "Build script not tested on this platform";;
esac

cc_exe="${CC:-cc}"

if [ $os = cygwin ]; then
  # Under cygwin, specifically ignore the mingw compilers if they're set as the
  # CC environment variable. This may be the default from the cygwin installer.
  # But we want to use 'gcc' from the cygwin gcc-core package (probably aliased
  # to cc), *not* the mingw compiler, because otherwise lots of POSIX stuff
  # will break. (Note that the 'cli' target might be fine, because it doesn't
  # uses curses or networking, but the 'orca' target almost certainly won't
  # be.)
  #
  # I'm worried about ambiguity with 'cc' being still aliased to mingw if the
  # user doesn't have gcc-core installed. I have no idea if that actually
  # happens. So we'll just explicitly set it to gcc. This might mess up people
  # who have clang installed but not gcc, I guess? Is that even possible?
  case $cc_exe in
  i686-w64-mingw32-gcc.exe|x86_64-w64-mingw32-gcc.exe)
    cc_exe=gcc;;
  esac
fi

verbose=0
protections_enabled=0
stats_enabled=0
pie_enabled=0
static_enabled=0
portmidi_enabled=0
jackmidi_enabled=0
mouse_disabled=0
config_mode=release

while getopts c:dhsv-: opt_val; do
  case $opt_val in
    -) case $OPTARG in
         harden) protections_enabled=1;;
         help) print_usage; exit 0;;
         static) static_enabled=1;;
         pie) pie_enabled=1;;
         portmidi) portmidi_enabled=1;;
         no-portmidi|noportmidi) portmidi_enabled=0;;
         jackmidi) jackmidi_enabled=1;;
         no-jackmidi|nojackmidi) jackmidi_enabled=0;;
         mouse) mouse_disabled=0;;
         no-mouse|nomouse) mouse_disabled=1;;
         *) printf 'Unknown option --%s\n' "$OPTARG" >&2; exit 1;;
       esac;;
    c) cc_exe=$OPTARG;;
    d) config_mode=debug;;
    h) print_usage; exit 0;;
    s) stats_enabled=1;;
    v) verbose=1;;
    \?) print_usage >&2; exit 1;;
  esac
done

case $(uname -m) in
  x86_64) arch=x86_64;;
  *) arch=unknown;;
esac

verbose_echo() {
  # Don't print 'timed_stats' if it's the first part of the command
  if [ $verbose = 1 ] && [ $# -gt 1 ]; then
    printf '%s ' "$@" | sed -E -e 's/^timed_stats[[:space:]]+//' -e 's/ $//' \
      | tr -d '\n'
    printf '\n'
  fi
  "$@"
}

file_size() {
  wc -c < "$1" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//'
}

timed_stats_result=
timed_stats() {
  if [ $stats_enabled = 1 ] && command -v time >/dev/null 2>&1; then
    TIMEFORMAT='%3R'
    { timed_stats_result=$( { time "$@" 1>&3- 2>&4-; } 2>&1 ); } 3>&1 4>&2
  else
    "$@"
  fi
}

normalized_version() {
  printf '%s\n' "$@" | awk -F. '{ printf("%d%03d%03d%03d\n", $1,$2,$3,$4); }';
}

cc_id=
cc_vers=
lld_detected=0
lld_name=lld
if preproc_result=$( \
  ("$cc_exe" -E -xc - 2>/dev/null | tail -n 2 | tr -d '\040') <<EOF
#if defined(__clang__)
clang
__clang_major__.__clang_minor__.__clang_patchlevel__
#elif defined(__GNUC__)
gcc
__GNUC__.__GNUC_MINOR__.__GNUC_PATCHLEVEL__
#elif defined(__TINYC__)
tcc
__TINYC__
#else
#error Unknown compiler
#endif
EOF
); then
  cc_id=$(printf %s "$preproc_result" | head -n 1)
  cc_vers=$(printf %s "$preproc_result" | tail -n 1)
fi

if [ "$cc_id" = clang ]; then
  case $os in
    # Mac clang/llvm doesn't say the real version of clang. Assume it's 3.9.0.
    mac) cc_vers=3.9.0;;
    *)
      # Debian names versions clang like "clang-9" and also LLD like "lld-9".
      # To tell clang to use LLD, we have to pass an argument like
      # '-fuse-ld=lld'. You would expect that the Debian versions of clang,
      # like clang-9, would want '-fuse-ld=lld-9', but it seems to work both as
      # '-fuse-ld=lld-' and also as '-fuse-ld=lld'. I'm not sure if this holds
      # true if multiple versions of clang are installed.
      if output=$(printf %s "$cc_exe" | awk -F- '
          /^clang\+?\+?-/ && $NF ~ /^[0-9]+$/ { a=$NF }
          END { if (a == "") exit -1; printf("lld-%s", a) }'); then
        lld_name=$output
      fi
      if command -v "$lld_name" >/dev/null 2>&1; then lld_detected=1; fi
    ;;
  esac
fi

test -z "$cc_id" && warn "Failed to detect compiler type"
test -z "$cc_vers" && warn "Failed to detect compiler version"

cc_vers_normalized=$(normalized_version "$cc_vers")

cc_vers_is_gte() {
  test "$cc_vers_normalized" -ge "$(normalized_version "$1")"
}

cc_id_and_vers_gte() {
  test "$cc_id" = "$1" && cc_vers_is_gte "$2"
}

# Append arguments to a string, separated by newlines. Like a bad array.
add() {
  if [ -z "${1:-}" ]; then
    script_error "At least one argument required for add"
  fi
  _add_name=${1}
  shift
  while [ -n "${1+x}" ]; do
    # shellcheck disable=SC2034
    _add_hidden=$1
    eval "$_add_name"'=$(printf '"'"'%s\n%s.'"' "'"$'"$_add_name"'" "$_add_hidden")'
    eval "$_add_name"'=${'"$_add_name"'%.}'
    shift
  done
}

try_make_dir() {
  if ! [ -e "$1" ]; then
    verbose_echo mkdir "$1"
  elif ! [ -d "$1" ]; then
    fatal "File $1 already exists but is not a directory"
  fi
}

build_dir=build

build_target() {
  cc_flags=
  libraries=
  source_files=
  out_exe=
  add cc_flags -std=c99 -pipe -finput-charset=UTF-8 -Wall -Wpedantic -Wextra \
    -Wwrite-strings
  if cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 3.9.0; then
    add cc_flags -Wconversion -Wshadow -Wstrict-prototypes \
      -Werror=implicit-function-declaration -Werror=implicit-int \
      -Werror=incompatible-pointer-types -Werror=int-conversion
  fi
  if [ "$cc_id" = tcc ]; then
    add cc_flags -Wunsupported
  fi
  if [ $os = mac ] && [ "$cc_id" = clang ]; then
    # The clang that's shipped with Mac 10.12 has bad behavior for issuing
    # warnings for structs initialed with {0} in C99. We have to disable this
    # warning, or it will issue a bunch of useless warnings. It might be fixed
    # in later versions, but Apple makes the version of clang/LLVM
    # indecipherable, so we'll just always turn it off.
    add cc_flags -Wno-missing-field-initializers
  fi
  if [ $lld_detected = 1 ]; then
    add cc_flags "-fuse-ld=$lld_name"
  fi
  if [ $protections_enabled = 1 ]; then
    add cc_flags -D_FORTIFY_SOURCE=2 -fstack-protector-strong
  fi
  if [ $pie_enabled = 1 ]; then
    add cc_flags -pie -fpie -Wl,-pie
  # Only explicitly specify no-pie if cc version is new enough
  elif cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 6.0.0; then
    add cc_flags -no-pie -fno-pie
  fi
  if [ $static_enabled = 1 ]; then
    add cc_flags -static
  fi
  case $config_mode in
    debug)
      add cc_flags -DDEBUG -g
      # cygwin gcc doesn't seem to have this stuff, so just elide for now
      if [ $os != cygwin ]; then
        if cc_id_and_vers_gte gcc 6.0.0 || cc_id_and_vers_gte clang 3.9.0; then
          add cc_flags -fsanitize=address -fsanitize=undefined \
            -fsanitize=float-divide-by-zero
        fi
        if cc_id_and_vers_gte clang 7.0.0; then
          add cc_flags -fsanitize=implicit-conversion \
            -fsanitize=unsigned-integer-overflow
        fi
      fi
      case $os in
        mac) add cc_flags -O1;; # Our Mac clang does not have -Og
        *) add cc_flags -Og;;
      esac
      case $cc_id in
        tcc) add cc_flags -g -bt10;;
      esac
    ;;
    release)
      add cc_flags -DNDEBUG -O2 -g0
      if [ $protections_enabled != 1 ]; then
        add cc_flags -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
        case $cc_id in
          gcc|clang) add cc_flags -fno-stack-protector
        esac
      fi
      # -flto is good on both clang and gcc on Linux and Cygwin. Not supported
      # on BSD, and no improvement on Mac. -s gives an obsolescence warning on
      # Mac. For tcc, -flto gives and unsupported warning, and -s is ignored.
      case $cc_id in gcc|clang) case $os in
        linux|cygwin) add cc_flags -flto -s;;
        bsd) add cc_flags -s;;
      esac esac
    ;;
    *) fatal "Unknown build config \"$config_mode\"";;
  esac

  case $arch in
    x86_64)
      case $cc_id in
        # 'nehalem' tuning actually produces faster code for orca than later
        # archs, for both gcc and clang, even if it's running on a later arch
        # CPU. This is likely due to smaller emitted code size. gcc earlier
        # than 4.9 does not recognize the arch flag for it it, though, and I
        # haven't tested a compiler that old, so I don't know what optimization
        # behavior we get with it is. Just leave it at default, in that case.
        gcc)
          if cc_vers_is_gte 4.9; then
            add cc_flags -march=nehalem
          fi
        ;;
        clang) add cc_flags -march=nehalem;;
      esac
    ;;
  esac

  add source_files gbuffer.c field.c vmio.c sim.c
  case $1 in
    cli)
      add source_files cli_main.c
      out_exe=cli
    ;;
    orca|tui)
      add source_files osc_out.c term_util.c sysmisc.c thirdparty/oso.c tui_main.c
      add cc_flags -D_XOPEN_SOURCE_EXTENDED=1
      # thirdparty headers (like sokol_time.h) should get -isystem for their
      # include dir so that any warnings they generate with our warning flags
      # are ignored. (sokol_time.h may generate sign conversion warning on
      # mac.)
      add cc_flags -isystem thirdparty
      out_exe=orca
      case $os in
        mac)
          if ! brew_prefix=$(printenv HOMEBREW_PREFIX); then
             brew_prefix=/usr/local
          fi
          ncurses_dir="$brew_prefix/opt/ncurses"
          if ! [ -d "$ncurses_dir" ]; then
            printf 'Error: ncurses directory not found at %s\n' \
              "$ncurses_dir" >&2
            printf 'Install with: brew install ncurses\n' >&2
            exit 1
          fi
          # prefer homebrew version of ncurses if installed. Will give us
          # better terminfo, so we can use A_DIM in Terminal.app, etc.
          add libraries "-L$ncurses_dir/lib"
          add cc_flags "-I$ncurses_dir/include"
          # todo mach time stuff for mac?
          if [ $portmidi_enabled = 1 ]; then
            portmidi_dir="$brew_prefix/opt/portmidi"
            if ! [ -d "$portmidi_dir" ]; then
              printf 'Error: PortMidi directory not found at %s\n' \
                "$portmidi_dir" >&2
              printf 'Install with: brew install portmidi\n' >&2
              exit 1
            fi
            add libraries "-L$portmidi_dir/lib"
            add cc_flags "-I$portmidi_dir/include"
          fi
          # needed for using pbpaste instead of xclip
          add cc_flags -DORCA_OS_MAC
        ;;
        bsd)
          if [ $portmidi_enabled = 1 ]; then
            add libraries "-L/usr/local/lib"
            add cc_flags "-I/usr/local/include"
          fi
        ;;
        *)
          # librt and high-res posix timers on Linux
          add libraries -lrt
          add cc_flags -D_POSIX_C_SOURCE=200809L
        ;;
      esac
      # Depending on the Linux distro, ncurses might have been built with tinfo
      # as a separate library that explicitly needs to be linked, or it might
      # not. And if it does, it might need to be either -ltinfo or -ltinfow.
      # Yikes. If this is Linux, let's try asking pkg-config what it thinks.
      curses_flags=0
      if [ $os = linux ]; then
        if curses_flags=$(pkg-config --libs ncursesw formw 2>/dev/null); then
          # Split by spaces intentionall
          # shellcheck disable=SC2086
          IFS=' ' add libraries $curses_flags
          curses_flags=1
        else
          curses_flags=0
        fi
      fi
      # If we didn't get the flags by pkg-config, just guess. (This will work
      # most of the time, including on Mac with Homebrew, and cygwin.)
      if [ $curses_flags = 0 ]; then
        add libraries -lncursesw -lformw
      fi
      if [ $portmidi_enabled = 1 ]; then
        add libraries -lportmidi
        add cc_flags -DFEAT_PORTMIDI
        if [ $config_mode = debug ]; then
          cat >&2 <<EOF
Warning: The PortMidi library contains code that may trigger address sanitizer
in debug builds. These are probably not bugs in orca.
EOF
        fi
      fi
      if [ $jackmidi_enabled = 1 ]; then
        add libraries -ljack
        add cc_flags -DFEAT_JACKMIDI
      fi
      if [ $mouse_disabled = 1 ]; then
        add cc_flags -DFEAT_NOMOUSE
      fi
    ;;
    *)
      printf 'Unknown build target %s\nValid build targets: %s\n' \
        "$1" 'orca, cli' >&2
      exit 1
    ;;
  esac
  try_make_dir "$build_dir"
  if [ $config_mode = debug ]; then
    build_dir=$build_dir/debug
    try_make_dir "$build_dir"
  fi
  out_path=$build_dir/$out_exe
  IFS='
'
  # shellcheck disable=SC2086
  verbose_echo timed_stats "$cc_exe" $cc_flags -o "$out_path" $source_files $libraries
  compile_ok=$?
  if [ $stats_enabled = 1 ]; then
    if [ -n "$timed_stats_result" ]; then
      printf '%s\n' "time: $timed_stats_result"
    else
      printf '%s\n' "time: unavailable (missing 'time' command)"
    fi
    if [ $compile_ok = 0 ]; then
      printf '%s\n' "size: $(file_size "$out_path")"
    fi
  fi
}

print_info() {
  if [ $lld_detected = 1 ]; then
    linker_name=LLD
    # Not sure if we should always print the specific LLD name or not. Or never
    # print it.
    if [ "$lld_name" != lld ]; then
      linker_name="$linker_name ($lld_name)"
    fi
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

case $cmd in
  info)
    test "$#" -gt 1 && fatal "Too many arguments for 'info'"
    print_info; exit 0
  ;;
  build)
    test "$#" -lt 1 && fatal "Too few arguments for 'build'"
    if [ "$#" -gt 1 ]; then
      cat >&2 <<EOF
Too many arguments for 'build'
The syntax has changed. Updated usage examples:
./tool build --portmidi orca   (release)
./tool build -d orca           (debug)
EOF
      exit 1
    fi
    build_target "$1"
  ;;
  clean)
    if [ -d "$build_dir" ]; then
      verbose_echo rm -rf "$build_dir";
    fi
  ;;
  help)
    print_usage; exit 0
  ;;
  -*) cat >&2 <<EOF
The syntax has changed for the 'tool' build script.
The options now need to come after the command name.
Do it like this instead:
./tool build --portmidi orca
EOF
    exit 1
  ;;
  *) fatal "Unrecognized command $cmd";;
esac

