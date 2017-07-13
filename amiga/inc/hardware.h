#include "types.h"
#include "graphx/bmaptype.h"

void init_fdc(int drive);
int jumptotrack(uint8_t t);
uint8_t readsector(uint8_t sectornum, uint8_t *data, uint8_t invalidate_cache);
uint8_t writesector(uint8_t sectornum, uint8_t *data);
int get_start_unit(char *path);

uint8_t wait_function_key(void);
uint8_t get_char(void);
void flush_char(void);
char *strlwr(char *s);

void reboot(void);

int init_display(void);
void display_sprite(uint8_t *membuffer, bmaptype *sprite, int x, int y);
void print_char8x8(uint8_t *membuffer, bmaptype *font,
                   int x, int y, uint8_t c);

void sleep(int secs);
void waitms(int ms);

#define L_INDIAN(var)                           \
    (((var&0x000000FF)<<24)                     \
     | ((var&0x0000FF00)<<8)                    \
     | ((var&0x00FF0000)>>8)                    \
     | ((var&0xFF000000)>>24))

int process_command_line(int argc, char *argv[]);

void lockup(void);
