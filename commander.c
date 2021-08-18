#include "commander.h"
#include <stdlib.h>
#include <string.h>

void parse_command(Glyph *command, State *state) {
  const Glyph end_line[2] = ".";
  Glyph *token;
  token = strtok(command, end_line);
  while (token != NULL) {
    // Parse simple tokens
    if (strcmp(token, "play") == 0) {
      state->is_playing = true;
    } else if (strcmp(token, "stop") == 0) {
      state->is_playing = false;
    } else if (strcmp(token, "run") == 0) {
      state->tick_num++;
    } else {
      const Glyph arguments_separator[2] = ":";
      token = strtok(command, arguments_separator);
      while(token != NULL) {
        // TODO handle errors: https://stackoverflow.com/questions/15229411/input-validation-of-an-integer-using-atoi
        if (strcmp(token, "bpm") == 0) {
          token = strtok(NULL, end_line);
          if (token == NULL) return;
          Glyph *end_ptr;
          state->bpm = (Usz) strtoul(token, &end_ptr, 10);
        } else if (strcmp(token, "frame") == 0) {
          token = strtok(NULL, end_line);
          if (token == NULL) return;
          Glyph *end_ptr;
          state->tick_num = (Usz) strtoul(token, &end_ptr, 10);
        } else if (strcmp(token, "rewind") == 0) {
          token = strtok(NULL, end_line);
          if (token == NULL) return;
          Glyph *end_ptr;
          state->tick_num -= (Usz) strtoul(token, &end_ptr, 10);
        } else if (strcmp(token, "skip") == 0) {
          token = strtok(NULL, end_line);
          if (token == NULL) return;
          Glyph *end_ptr;
          state->tick_num += (Usz) strtoul(token, &end_ptr, 10);
        }
        token = strtok(NULL, arguments_separator);
      }
    }
    token = strtok(NULL, end_line);
  }
}