#include "gbuffer.h"
#include "mark.h"
#include "sim.h"

//////// Utilities

static Glyph const indexed_glyphs[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',
    's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '.', '*', ':', ';', '#',
};

enum { Glyphs_array_num = sizeof indexed_glyphs };

// Always returns 0 through (sizeof indexed_glyphs) - 1, and works on
// capitalized glyphs as well. The index of the lower-cased glyph is returned
// if the glyph is capitalized.
#if 1
static Usz index_of(Glyph c) {
  if (c == '.')
    return 0;
  if (c >= '0' && c <= '9')
    return (Usz)(c - '0');
  if (c >= 'A' && c <= 'Z')
    return (Usz)(c - 'A' + 10);
  if (c >= 'a' && c <= 'z')
    return (Usz)(c - 'a' + 10);
  switch (c) {
  case '*':
    return 37;
  case ':':
    return 38;
  case ';':
    return 49;
  case '#':
    return 40;
  }
  return 0;
}
#else
// Reference implementation
inline static Glyph glyph_lowered(Glyph c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - ('a' - 'A')) : c;
}
static Usz index_of(Glyph c) {
  Glyph c0 = glyph_lowered(c);
  if (c0 == '.')
    return 0;
  for (Usz i = 0; i < Glyphs_array_num; ++i) {
    if (indexed_glyphs[i] == c0)
      return i;
  }
  return 0;
}
#endif

static inline Glyph glyph_of(Usz index) {
  assert(index < Glyphs_array_num);
  return indexed_glyphs[index];
}

static Glyph glyphs_add(Glyph a, Glyph b) {
  Usz ia = index_of(a);
  Usz ib = index_of(b);
  return indexed_glyphs[(ia + ib) % Glyphs_array_num];
}

static Glyph glyphs_mod(Glyph a, Glyph b) {
  Usz ia = index_of(a);
  Usz ib = index_of(b);
  return indexed_glyphs[ib == 0 ? 0 : (ia % ib)];
}

ORCA_PURE static bool oper_has_neighboring_bang(Glyph const* gbuf, Usz h, Usz w,
                                                Usz y, Usz x) {
  Glyph const* gp = gbuf + w * y + x;
  if (x < w - 1 && gp[1] == '*')
    return true;
  if (x > 0 && *(gp - 1) == '*')
    return true;
  if (y < h - 1 && gp[w] == '*')
    return true;
  // note: negative array subscript on rhs of short-circuit, may cause ub if
  // the arithmetic under/overflows, even if guarded the guard on lhs is false
  if (y > 0 && *(gp - w) == '*')
    return true;
  return false;
}

ORCA_FORCE_NO_INLINE
static void oper_movement_phase0(Gbuffer gbuf, Mbuffer mbuf, Usz const height,
                                 Usz const width, Usz const y, Usz const x,
                                 Mark const cell_flags,
                                 Glyph const uppercase_char,
                                 Glyph const actual_char, Isz const delta_y,
                                 Isz const delta_x) {
  if (cell_flags & (Mark_flag_lock | Mark_flag_sleep))
    return;
  if ((actual_char != uppercase_char) &&
      !oper_has_neighboring_bang(gbuf, height, width, y, x))
    return;
  Isz y0 = (Isz)y + delta_y;
  Isz x0 = (Isz)x + delta_x;
  if (y0 >= (Isz)height || x0 >= (Isz)width || y0 < 0 || x0 < 0) {
    gbuf[y * width + x] = '*';
    return;
  }
  Glyph* restrict g_at_dest = gbuf + (Usz)y0 * width + (Usz)x0;
  if (*g_at_dest == '.') {
    *g_at_dest = actual_char;
    gbuf[y * width + x] = '.';
    mbuf[(Usz)y0 * width + (Usz)x0] |= Mark_flag_sleep;
  } else {
    gbuf[y * width + x] = '*';
  }
}

typedef struct {
  Bank* bank;
  Usz size;
} Oper_bank_write_params;

typedef struct {
  Bank* bank;
  Usz size;
  Bank_cursor cursor;
} Oper_bank_read_params;

static void oper_bank_store(Oper_bank_write_params* bank_params, Usz width,
                            Usz y, Usz x, I32* restrict vals, Usz num_vals) {
  assert(num_vals > 0);
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  bank_params->size =
      bank_append(bank_params->bank, bank_params->size, index, vals, num_vals);
}
static Usz oper_bank_load(Oper_bank_read_params* bank_params, Usz width, Usz y,
                          Usz x, I32* restrict out_vals, Usz out_count) {
  Usz index = y * width + x;
  assert(index < ORCA_BANK_INDEX_MAX);
  return bank_read(bank_params->bank->data, bank_params->size,
                   &bank_params->cursor, index, out_vals, out_count);
}

ORCA_FORCE_STATIC_INLINE
Usz usz_clamp(Usz val, Usz min, Usz max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

#define ORCA_EXPAND_SOLO_OPER_CHARS(_oper_char, _oper_name)                    \
  Orca_oper_char_##_oper_name = _oper_char,
#define ORCA_EXPAND_DUAL_OPER_CHARS(_upper_oper_char, _lower_oper_char,        \
                                    _oper_name)                                \
  Orca_oper_upper_char_##_oper_name = _upper_oper_char,                        \
  Orca_oper_lower_char_##_oper_name = _lower_oper_char,
#define ORCA_EXPAND_MOVM_OPER_CHARS(_upper_oper_char, _lower_oper_char,        \
                                    _oper_name, _delta_y, _delta_x)            \
  Orca_oper_upper_char_##_oper_name = _upper_oper_char,                        \
  Orca_oper_lower_char_##_oper_name = _lower_oper_char,
#define ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs, _movm_defs)             \
  enum Orca_oper_chars {                                                       \
    _solo_defs(ORCA_EXPAND_SOLO_OPER_CHARS)                                    \
        _dual_defs(ORCA_EXPAND_DUAL_OPER_CHARS)                                \
            _movm_defs(ORCA_EXPAND_MOVM_OPER_CHARS)                            \
  };
#define ORCA_DECLARE_OPERATORS(_solo_defs, _dual_defs, _movm_defs)             \
  ORCA_DEFINE_OPER_CHARS(_solo_defs, _dual_defs, _movm_defs)

#define OPER_PHASE_COMMON_ARGS                                                 \
  Glyph *const restrict gbuffer, Mark *const restrict mbuffer,                 \
      Usz const height, Usz const width, Usz const y, Usz const x,             \
      Usz Tick_number
#define OPER_PHASE_0_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_bank_write_params *const bank_params,           \
      Mark const cell_flags
#define OPER_PHASE_1_COMMON_ARGS                                               \
  OPER_PHASE_COMMON_ARGS, Oper_bank_read_params* const bank_params

#define OPER_IGNORE_COMMON_ARGS()                                              \
  (void)gbuffer;                                                               \
  (void)mbuffer;                                                               \
  (void)height;                                                                \
  (void)width;                                                                 \
  (void)y;                                                                     \
  (void)x;                                                                     \
  (void)Tick_number;                                                           \
  (void)bank_params;

#define OPER_PHASE_SPEC ORCA_FORCE_NO_INLINE static

#define BEGIN_SOLO_PHASE_0(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase0_##_oper_name(OPER_PHASE_0_COMMON_ARGS) {    \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_SOLO_PHASE_1(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase1_##_oper_name(OPER_PHASE_1_COMMON_ARGS) {    \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    enum { This_oper_char = Orca_oper_char_##_oper_name };
#define BEGIN_DUAL_PHASE_0(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase0_##_oper_name(OPER_PHASE_0_COMMON_ARGS,      \
                                                Glyph const This_oper_char) {  \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)cell_flags;                                                          \
    (void)This_oper_char;                                                      \
    enum { Uppercase_oper_char = Orca_oper_upper_char_##_oper_name };          \
    (void)Uppercase_oper_char;
#define BEGIN_DUAL_PHASE_1(_oper_name)                                         \
  OPER_PHASE_SPEC void oper_phase1_##_oper_name(OPER_PHASE_1_COMMON_ARGS,      \
                                                Glyph const This_oper_char) {  \
    OPER_IGNORE_COMMON_ARGS()                                                  \
    (void)This_oper_char;                                                      \
    enum { Uppercase_oper_char = Orca_oper_upper_char_##_oper_name };          \
    (void)Uppercase_oper_char;

#define END_PHASE }

#define PEEK(_delta_y, _delta_x)                                               \
  gbuffer_peek_relative(gbuffer, height, width, y, x, _delta_y, _delta_x)
#define POKE(_delta_y, _delta_x, _glyph)                                       \
  gbuffer_poke_relative(gbuffer, height, width, y, x, _delta_y, _delta_x,      \
                        _glyph)
#define BECOME(_glyph) gbuffer_poke(gbuffer, height, width, y, x, _glyph)
#define STUN(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_sleep)
#define LOCK(_delta_y, _delta_x)                                               \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, Mark_flag_lock)

#define STORE(_i32_array)                                                      \
  oper_bank_store(bank_params, width, y, x, _i32_array,                        \
                  ORCA_ARRAY_COUNTOF(_i32_array))
#define LOAD(_i32_array)                                                       \
  oper_bank_load(bank_params, width, y, x, _i32_array,                         \
                 ORCA_ARRAY_COUNTOF(_i32_array))

#define IN Mark_flag_input
#define OUT Mark_flag_output
#define NONLOCKING Mark_flag_lock
#define HASTE Mark_flag_haste_input

#define REALIZE_DUAL                                                           \
  bool const Dual_is_active =                                                  \
      (Uppercase_oper_char == This_oper_char) ||                               \
      oper_has_neighboring_bang(gbuffer, height, width, y, x);

#define PSEUDO_DUAL bool const Dual_is_active = true

#define BEGIN_DUAL_PORTS                                                       \
  {                                                                            \
    bool const Oper_ports_enabled = Dual_is_active;

#define DUAL_IS_ACTIVE Dual_is_active

#define IS_AWAKE (!(cell_flags & (Mark_flag_lock | Mark_flag_sleep)))

#define STOP_IF_DUAL_INACTIVE                                                  \
  if (!Dual_is_active)                                                         \
  return

#define STOP_IF_NOT_BANGED                                                     \
  if (!oper_has_neighboring_bang(gbuffer, height, width, y, x))                \
  return

#define OPER_PORT_IO_MASK                                                      \
  (Mark_flag_input | Mark_flag_output | Mark_flag_haste_input)
#define OPER_PORT_CELL_ENABLING_MASK (Mark_flag_lock | Mark_flag_sleep)
#define OPER_PORT_FLIP_LOCK_BIT(_flags) ((_flags) ^ Mark_flag_lock)

#define PORT(_delta_y, _delta_x, _flags)                                       \
  if (Oper_ports_enabled && !(cell_flags & OPER_PORT_CELL_ENABLING_MASK))      \
  mbuffer_poke_relative_flags_or(mbuffer, height, width, y, x, _delta_y,       \
                                 _delta_x, OPER_PORT_FLIP_LOCK_BIT(_flags))
#define END_PORTS }

//////// Operators

#define ORCA_SOLO_OPERATORS(_)                                                 \
  _('#', comment)                                                              \
  _('*', bang)

#define ORCA_DUAL_OPERATORS(_)                                                 \
  _('A', 'a', add)                                                             \
  _('B', 'b', banger)                                                          \
  _('C', 'c', clock)                                                           \
  _('D', 'd', delay)                                                           \
  _('F', 'f', if)                                                              \
  _('G', 'g', generator)                                                       \
  _('H', 'h', halt)                                                            \
  _('I', 'i', increment)                                                       \
  _('J', 'j', jump)                                                            \
  _('K', 'k', kill)                                                            \
  _('L', 'l', loop)                                                            \
  _('M', 'm', modulo)                                                          \
  _('O', 'o', offset)                                                          \
  _('P', 'p', push)                                                            \
  _('Q', 'q', query)                                                           \
  _('R', 'r', random)                                                          \
  _('T', 't', track)                                                           \
  _('U', 'u', uturn)                                                           \
  _('V', 'v', beam)                                                            \
  _('X', 'x', teleport)

#define ORCA_MOVEMENT_OPERATORS(_)                                             \
  _('N', 'n', north, -1, 0)                                                    \
  _('E', 'e', east, 0, 1)                                                      \
  _('S', 's', south, 1, 0)                                                     \
  _('W', 'w', west, 0, -1)                                                     \
  _('Z', 'z', southeast, 1, 1)

ORCA_DECLARE_OPERATORS(ORCA_SOLO_OPERATORS, ORCA_DUAL_OPERATORS,
                       ORCA_MOVEMENT_OPERATORS)

#define MOVEMENT_CASES                                                         \
  'N' : case 'n' : case 'E' : case 'e' : case 'S' : case 's' : case 'W'        \
      : case 'w' : case 'Z' : case 'z'

BEGIN_SOLO_PHASE_0(bang)
  if (IS_AWAKE) {
    BECOME('.');
  }
END_PHASE
BEGIN_SOLO_PHASE_1(bang)
END_PHASE

BEGIN_SOLO_PHASE_0(comment)
  if (!IS_AWAKE)
    return;
  Glyph* line = gbuffer + y * width;
  Usz max_x = x + 255;
  if (width < max_x)
    max_x = width;
  for (Usz x0 = x + 1; x0 < max_x; ++x0) {
    Glyph g = line[x0];
    mbuffer_poke_flags_or(mbuffer, height, width, y, x0, Mark_flag_lock);
    if (g == '#')
      break;
  }
END_PHASE
BEGIN_SOLO_PHASE_1(comment)
END_PHASE

BEGIN_DUAL_PHASE_0(add)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(add)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_add(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(banger)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN | NONLOCKING);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(banger)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Glyph g = PEEK(0, 1);
  Glyph result;
  switch (g) {
  case '1':
  case '*':
  case MOVEMENT_CASES:
    result = '*';
    break;
  default:
    result = '.';
  }
  POKE(1, 0, result);
END_PHASE

BEGIN_DUAL_PHASE_0(clock)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    // This is set as haste in js, but not used during .haste(). Mistake?
    // Replicating here anyway.
    PORT(0, -1, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(clock)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz mod_num = index_of(PEEK(0, 1));
  if (mod_num == 0)
    mod_num = 10;
  Usz rate = usz_clamp(index_of(PEEK(0, -1)), 1, 16);
  Glyph g = glyph_of(Tick_number / rate % mod_num);
  POKE(1, 0, g);
END_PHASE

BEGIN_DUAL_PHASE_0(delay)
  PSEUDO_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(delay)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz offset = index_of(PEEK(0, 1));
  Usz rate = usz_clamp(index_of(PEEK(0, -1)), 2, 16);
  Glyph g = (Tick_number + offset) % rate == 0 ? '*' : '.';
  POKE(1, 0, g);
END_PHASE

BEGIN_DUAL_PHASE_0(if)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(if)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Glyph g0 = PEEK(0, 1);
  Glyph g1 = PEEK(0, 2);
  POKE(1, 0, g0 == g1 ? '1' : '0');
END_PHASE

BEGIN_DUAL_PHASE_0(generator)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(1, 0, OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(generator)
  STOP_IF_NOT_BANGED;
  POKE(1, 0, PEEK(0, 1));
  STUN(0, 1);
END_PHASE

BEGIN_DUAL_PHASE_0(halt)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(halt)
END_PHASE

BEGIN_DUAL_PHASE_0(increment)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, IN | OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(increment)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz min = index_of(PEEK(0, 1));
  Usz max = index_of(PEEK(0, 2));
  Usz val = index_of(PEEK(1, 0));
  ++val;
  if (max == 0)
    max = 10;
  if (val >= max)
    val = min;
  POKE(1, 0, glyph_of(val));
END_PHASE

BEGIN_DUAL_PHASE_0(jump)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(-1, 0, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(jump)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, PEEK(-1, 0));
END_PHASE

BEGIN_DUAL_PHASE_0(kill)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(1, 0, OUT | HASTE);
  END_PORTS
  STOP_IF_DUAL_INACTIVE;
  if (IS_AWAKE) {
    POKE(1, 0, '.');
  }
END_PHASE
BEGIN_DUAL_PHASE_1(kill)
END_PHASE

BEGIN_DUAL_PHASE_0(loop)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
  END_PORTS
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = index_of(PEEK(0, -1));
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    if (len == 0)
      len = 1;
    else if (len > 16)
      len = 16;
    if (len > width - x - 1)
      len = width - x - 1;
    Mark* m = mbuffer + y * width + x + 1;
    for (Usz i = 0; i < len; ++i) {
      m[i] |= Mark_flag_lock;
    }
  }
END_PHASE
BEGIN_DUAL_PHASE_1(loop)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  I32 len_data[1];
  // todo should at least stun the 1 column if columns is 1
  if (LOAD(len_data) && len_data[0] >= 0) {
    Usz len = (Usz)len_data[0];
    if (len > width - x - 1)
      len = width - x - 1;
    if (len == 0)
      return;
    if (len > 16)
      len = 16;
    Glyph buff[16];
    Glyph* gs = gbuffer + y * width + x + 1;
    Glyph hopped = *gs;
    // ORCA_MEMCPY(buff, gs + 1, len - 1);
    for (Usz i = 0; i < len; ++i) {
      buff[i] = gs[i + 1];
    }
    buff[len - 1] = hopped;
    // ORCA_MEMCPY(gs, buff, len);
    for (Usz i = 0; i < len; ++i) {
      gs[i] = buff[i];
    }
    Mark* m = mbuffer + y * width + x + 1;
    for (Usz i = 0; i < len; ++i) {
      *m |= Mark_flag_sleep;
    }
  }
END_PHASE

BEGIN_DUAL_PHASE_0(modulo)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(modulo)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  POKE(1, 0, glyphs_mod(PEEK(0, 1), PEEK(0, 2)));
END_PHASE

BEGIN_DUAL_PHASE_0(offset)
  REALIZE_DUAL;
  I32 coords[2];
  coords[0] = 0; // y
  coords[1] = 1; // x
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    coords[0] = (I32)usz_clamp(index_of(PEEK(0, -1)), 0, 16);
    coords[1] = (I32)usz_clamp(index_of(PEEK(0, -2)) + 1, 1, 16);
    STORE(coords);
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(coords[0], coords[1], IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(offset)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  I32 coords[2];
  if (!LOAD(coords)) {
    coords[0] = 0;
    coords[1] = 1;
  }
  POKE(1, 0, PEEK(coords[0], coords[1]));
  STUN(1, 0);
END_PHASE

BEGIN_DUAL_PHASE_0(push)
  REALIZE_DUAL;
  I32 write_val_x[1];
  write_val_x[0] = 0;
  if (IS_AWAKE && DUAL_IS_ACTIVE) {
    Usz len = usz_clamp(index_of(PEEK(0, -1)), 1, 16);
    Usz key = index_of(PEEK(0, -2));
    write_val_x[0] = (I32)(key % len);
    STORE(write_val_x);
    for (Isz i = 0; i < write_val_x[0]; ++i) {
      LOCK(1, i);
    }
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, 1, IN);
    PORT(1, (Isz)write_val_x, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(push)
  STOP_IF_NOT_BANGED;
  I32 write_val_x[1];
  if (!LOAD(write_val_x)) {
    write_val_x[0] = 0;
  }
  POKE(1, write_val_x[0], PEEK(0, 1));
END_PHASE

BEGIN_DUAL_PHASE_0(query)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(1, 0, OUT);
  END_PORTS
  if (IS_AWAKE) {
    Usz len = usz_clamp(index_of(PEEK(0, -1)), 0, 16) + 1;
    I32 len_data[1];
    len_data[0] = (I32)len;
    STORE(len_data);
    Usz max_x = x + len + 1;
    if (max_x > width)
      max_x = width;
    Mark* i = mbuffer + y * width + x + 1;
    Mark* e = mbuffer + y * width + max_x;
    while (i != e) {
      *i = (Mark)(*i | Mark_flag_lock);
      ++i;
    }
  }
END_PHASE
BEGIN_DUAL_PHASE_1(query)
  I32 len_data[1];
  if (LOAD(len_data) && len_data[0] >= 1 && len_data[0] <= 17) {
    Usz len = (Usz)len_data[0];
    Usz max_x = x + len + 1;
    if (max_x >= width)
      max_x = width;
    Glyph const* i = gbuffer + y * width + x + 1;
    Glyph const* e = gbuffer + y * width + max_x;
    Usz count = 0;
    while (i != e) {
      if (*i != '.')
        ++count;
      ++i;
    }
    Glyph g = glyph_of(count % Glyphs_array_num);
    POKE(1, 0, g);
  }
END_PHASE

static Usz hash32_shift_mult(Usz key) {
  Usz c2 = UINT32_C(0x27d4eb2d);
  key = (key ^ UINT32_C(61)) ^ (key >> UINT32_C(16));
  key = key + (key << UINT32_C(3));
  key = key ^ (key >> UINT32_C(4));
  key = key * c2;
  key = key ^ (key >> UINT32_C(15));
  return key;
}

BEGIN_DUAL_PHASE_0(random)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(0, 1, IN);
    PORT(0, 2, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(random)
  REALIZE_DUAL;
  STOP_IF_DUAL_INACTIVE;
  Usz a = index_of(PEEK(0, 1));
  Usz b = index_of(PEEK(0, 2));
  Usz min, max;
  if (a == b) {
    POKE(1, 0, glyph_of(a));
    return;
  } else if (a < b) {
    min = a;
    max = b;
  } else {
    min = b;
    max = a;
  }
  Usz key = y * width + x;
  key = hash32_shift_mult((y * width + x) ^ (Tick_number << UINT32_C(16)));
  Usz val = key % (max - min) + min;
  POKE(1, 0, glyph_of(val));
END_PHASE

BEGIN_DUAL_PHASE_0(track)
  PSEUDO_DUAL;
  Isz read_val_x = 1;
  if (IS_AWAKE) {
    Usz len = usz_clamp(index_of(PEEK(0, -1)), 1, 16);
    Usz key = index_of(PEEK(0, -2));
    read_val_x = (Isz)(key % len + 1);
    I32 ival[1];
    ival[0] = (I32)read_val_x;
    STORE(ival);
    for (Isz i = 0; i < read_val_x; ++i) {
      LOCK(0, i + 1);
    }
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE);
    PORT(0, -2, IN | HASTE);
    PORT(0, (Isz)read_val_x, IN);
    PORT(1, 0, OUT);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(track)
  I32 ival[1];
  if (!LOAD(ival)) {
    ival[0] = 1;
  }
  POKE(1, 0, PEEK(0, ival[0]));
  STUN(1, 0);
END_PHASE

static Isz const uturn_data[] = {
    // clang-format off
  -1, 0, (Isz)'N',
  0, -1, (Isz)'W',
  0, 1, (Isz)'E',
  1, 0, (Isz)'S',
    // clang-format on
};

enum {
  Uturn_per = 3,
  Uturn_loop_limit = Uturn_per * 4,
};

BEGIN_DUAL_PHASE_0(uturn)
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
      PORT(uturn_data[i + 0], uturn_data[i + 1], IN | OUT | HASTE | NONLOCKING);
    }
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(uturn)
  REALIZE_DUAL;
  if (!DUAL_IS_ACTIVE)
    return;
  for (Usz i = 0; i < Uturn_loop_limit; i += Uturn_per) {
    Isz dy = uturn_data[i + 0];
    Isz dx = uturn_data[i + 1];
    Glyph g = PEEK(dy, dx);
    switch (g) {
    case MOVEMENT_CASES:
      POKE(dy, dx, (Glyph)uturn_data[i + 2]);
      STUN(dy, dx);
    }
  }
END_PHASE

BEGIN_DUAL_PHASE_0(beam)
  if (!IS_AWAKE)
    return;
  Usz max_y = y + 255;
  if (height < max_y)
    max_y = height;
  Glyph* col = gbuffer + x;
  Usz y0 = y;
  for (;;) {
    if (y0 + 1 == max_y)
      break;
    Glyph g = col[width * (y0 + 1)];
    if (g == '.' || g == '*')
      break;
    ++y0;
  }
  I32 val_y[1];
  val_y[0] = (I32)(y - y0);
  STORE(val_y);
  REALIZE_DUAL;
  BEGIN_DUAL_PORTS
    PORT(val_y[0], 0, OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(beam)
  STOP_IF_NOT_BANGED;
  I32 val_y[1];
  if (!LOAD(val_y))
    val_y[0] = 1;
  POKE(val_y[0], 0, '.');
END_PHASE

BEGIN_DUAL_PHASE_0(teleport)
  PSEUDO_DUAL;
  I32 coords[2];
  coords[0] = 1; // y
  coords[1] = 0; // x
  if (IS_AWAKE) {
    coords[0] = (I32)usz_clamp(index_of(PEEK(0, -1)), 1, 16);
    coords[1] = (I32)usz_clamp(index_of(PEEK(0, -2)), 0, 16);
    STORE(coords);
  }
  BEGIN_DUAL_PORTS
    PORT(0, -1, IN | HASTE); // y
    PORT(0, -2, IN | HASTE); // x
    PORT(0, 1, IN);
    PORT(coords[0], coords[1], OUT | NONLOCKING);
  END_PORTS
END_PHASE
BEGIN_DUAL_PHASE_1(teleport)
  I32 coords[2];
  if (!LOAD(coords)) {
    coords[0] = 1;
    coords[1] = 0;
  }
  POKE(coords[0], coords[1], PEEK(0, 1));
  STUN(coords[0], coords[1]);
END_PHASE

//////// Run simulation

#define SIM_EXPAND_SOLO_PHASE_0(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, cell_flags);                         \
    break;
#define SIM_EXPAND_SOLO_PHASE_1(_oper_char, _oper_name)                        \
  case _oper_char:                                                             \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params);                                     \
    break;

#define SIM_EXPAND_DUAL_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase0_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, cell_flags, glyph_char);             \
    break;
#define SIM_EXPAND_DUAL_PHASE_1(_upper_oper_char, _lower_oper_char,            \
                                _oper_name)                                    \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_phase1_##_oper_name(gbuf, mbuf, height, width, iy, ix, tick_number,   \
                             bank_params, glyph_char);                         \
    break;

#define SIM_EXPAND_MOVM_PHASE_0(_upper_oper_char, _lower_oper_char,            \
                                _oper_name, _delta_y, _delta_x)                \
  case _upper_oper_char:                                                       \
  case _lower_oper_char:                                                       \
    oper_movement_phase0(gbuf, mbuf, height, width, iy, ix, cell_flags,        \
                         _upper_oper_char, glyph_char, _delta_y, _delta_x);    \
    break;

static void sim_phase_0(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_bank_write_params* bank_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const* glyph_row = gbuf + iy * width;
    Mark const* mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      Mark cell_flags = mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep);
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_0)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_0)
        ORCA_MOVEMENT_OPERATORS(SIM_EXPAND_MOVM_PHASE_0)
      }
    }
  }
}

static void sim_phase_1(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
                        Usz tick_number, Oper_bank_read_params* bank_params) {
  for (Usz iy = 0; iy < height; ++iy) {
    Glyph const* glyph_row = gbuf + iy * width;
    Mark const* mark_row = mbuf + iy * width;
    for (Usz ix = 0; ix < width; ++ix) {
      Glyph glyph_char = glyph_row[ix];
      if (ORCA_LIKELY(glyph_char == '.'))
        continue;
      if (mark_row[ix] & (Mark_flag_lock | Mark_flag_sleep))
        continue;
      switch (glyph_char) {
        ORCA_SOLO_OPERATORS(SIM_EXPAND_SOLO_PHASE_1)
        ORCA_DUAL_OPERATORS(SIM_EXPAND_DUAL_PHASE_1)
      }
    }
  }
}

void orca_run(Gbuffer gbuf, Mbuffer mbuf, Usz height, Usz width,
              Usz tick_number, Bank* bank) {
  mbuffer_clear(mbuf, height, width);
  Oper_bank_write_params bank_write_params;
  bank_write_params.bank = bank;
  bank_write_params.size = 0;
  sim_phase_0(gbuf, mbuf, height, width, tick_number, &bank_write_params);
  Oper_bank_read_params bank_read_params;
  bank_read_params.bank = bank;
  bank_read_params.size = bank_write_params.size;
  bank_cursor_reset(&bank_read_params.cursor);
  sim_phase_1(gbuf, mbuf, height, width, tick_number, &bank_read_params);
}
