#pragma once

#define UNIQUE_OPERATORS(_)                                                    \
  _('!', midicc, "CC", "Sends MIDI control change")                            \
  _('#', comment, "Comment", "Halts line")                                     \
  _('%', midi, "Mono", "Sends MIDI monophonic note")                           \
  _('*', bang, "Bang", "Bangs neighboring operands")                           \
  _(':', midi, "Midi", "Sends MIDI note")                                      \
  _(';', udp, "UDP", "Sends UDP message")                                      \
  _('=', osc, "OSC", "Sends OSC message")                                      \
  _('?', midipb, "PB", "Sends MIDI pitch bend")

#define ALPHA_OPERATORS(_)                                                     \
  _('A', add, "Add", "Outputs sum of inputs")                                  \
  _('B', subtract, "Subtract", "Outputs difference of inputs")                 \
  _('C', clock, "Clock", "Outputs modulo of frame")                            \
  _('D', delay, "Delay", "Bangs on modulo of frame")                           \
  _('E', movement, "East", "Moves eastward, or bangs")                         \
  _('F', if, "If", "Bangs if inputs are equal")                                \
  _('G', generator, "Generator", "Writes operands with offset")                \
  _('H', halt, "Halt", "Halts southward operand")                              \
  _('I', increment, "Increment", "Increments southward operand")               \
  _('J', jump, "Jumper", "Outputs northward operand")                          \
  _('K', konkat, "Konkat", "Reads multiple variables")                         \
  _('L', lesser, "Lesser", "Outputs smallest input")                           \
  _('M', multiply, "Multiply", "Outputs product of inputs")                    \
  _('N', movement, "North", "Moves Northward, or bangs")                       \
  _('O', offset, "Read", "Reads operand with offset")                          \
  _('P', push, "Push", "Writes eastward operand")                              \
  _('Q', query, "Query", "Reads operands with offset")                         \
  _('R', random, "Random", "Outputs random value")                             \
  _('S', movement, "South", "Moves southward, or bangs")                       \
  _('T', track, "Track", "Reads eastward operand")                             \
  _('U', uclid, "Uclid", "Bangs on Euclidean rhythm")                          \
  _('V', variable, "Variable", "Reads and writes variable")                    \
  _('W', movement, "West", "Moves westward, or bangs")                         \
  _('X', teleport, "Write", "Writes operand with offset")                      \
  _('Y', yump, "Jymper", "Outputs westward operand")                           \
  _('Z', lerp, "Lerp", "Transitions operand to target")
