#include "base.h"

typedef struct Oosc_dev Oosc_dev;

typedef enum {
  Oosc_udp_create_error_ok = 0,
  Oosc_udp_create_error_couldnt_open_socket = 1,
} Oosc_udp_create_error;

Oosc_udp_create_error oosc_dev_create_udp(Oosc_dev** out_dev_ptr, U16 port);
void oosc_dev_destroy(Oosc_dev* dev);
// raw UDP datagram
void oosc_send_datagram(Oosc_dev* dev, char const* data, Usz size);
void oosc_send_int32s(Oosc_dev* dev, char const* osc_address, I32 const* vals,
                      Usz count);
