#include <stdio.h>
#include "priv/alt_busy_sleep.h"
#include "altera_avalon_pio_regs.h"
#include "system.h"

typedef struct {
  volatile int *pCSR;
  volatile unsigned short *pMEM;
  int span;
} t_hasher;

void write_led(int val)
{
  IOWR_ALTERA_AVALON_PIO_DATA(
    LEDS_BASE,
    val
  );
}

int read_csr(t_hasher* p_hasher)
{
  return *(p_hasher->pCSR);
}

void write_csr(t_hasher* p_hasher, int val)
{
  *(p_hasher->pCSR) = val;
}

int success(unsigned short r4, unsigned short r6)
{
  return (r4 == 0xfeb1 && r6 == 0x9298);
}

unsigned short swapb(unsigned short val)
{
  return (val >> 8) | ((val & 0xFF) << 8);
}

void rehash(short pw_bytes, unsigned short *r4, unsigned short *r6)
{
  unsigned short swapped = swapb(pw_bytes);
  unsigned short new_r4 = *r6 ^ swapped;
  *r6 = swapb(*r4 + swapped);
  *r4 = new_r4;
}

int test(unsigned short *p)
{
    unsigned short r4 = 0;
    unsigned short r6 = 0;

    rehash(p[0], &r4, &r6);
    rehash(p[1], &r4, &r6);
    rehash(p[2], &r4, &r6);
    return success(r4, r6);
}

void fix(unsigned short p[3])
{
  p[0]--;
  if (p[0] == 0xFFFF)
  {
    p[1]--;
    if (p[1] == 0xFFFF)
    {
      p[2] -= 0x100;
    }
  }
}

void print(unsigned short *p)
{
  printf("%04X%04X%02X\n", p[0], p[1], p[2] >> 8);
}

int dump(t_hasher *p_hasher)
{
  volatile unsigned short *pMem1 = p_hasher->pMEM;

  unsigned short p[3] = {0};
  int accept_count = 0;
  for (int i = 0; i < p_hasher->span / sizeof(unsigned short); ++i)
  {
    p[i % 3] = pMem1[i];
    if ((i % 3) == 2)
    {
      fix(p);
      if (test(p)) {
        printf("accept: ");
        print(p);
        accept_count++;
      } else {
        printf("reject: "); print(p);
      }
      p[0] = p[1] = p[2] = 0;
    }
  }
  if (accept_count) {
	 printf("hasher at base address 0x%08X produced %d passwords\n", pMem1, accept_count);
  }
  return accept_count;
}

void clear(t_hasher *p_hasher)
{
  volatile unsigned short *pMem1 = p_hasher->pMEM;

  for (int i = 0; i < p_hasher->span / sizeof(unsigned short); ++i)
  {
	  pMem1[i] = 0;
  }
}

// Warning: note assumptions about relative base addresses. See hollywood_hash.tcl.
void init_hashers(t_hasher hashers[], int num_hashers, int gen_base, int mem_base, int span)
{
  int i;
  for (i = 0; i < num_hashers; ++i)
  {
	hashers[i].pCSR = (volatile int*)(0x00020000 + 8 * i);
	hashers[i].pMEM = (volatile unsigned short*)(0x00018040 + 0x40 * i);
	hashers[i].span = span;
  }
}

#define NUM_HASHERS 32
int main()
{
  int count = 0;
  t_hasher hashers[NUM_HASHERS];
  init_hashers(hashers, NUM_HASHERS, PW_GEN_0_BASE, PW_MEM_0_BASE, PW_MEM_0_SPAN);

  printf("Hollywood hasher\n");
#if 1
  printf("setting 'run' to 0\n");
  for (int i = 0; i < NUM_HASHERS; ++i) {
    write_csr(&hashers[i], 0);
  }

  printf("clearing dual-port memory\n");
  for (int i = 0; i < NUM_HASHERS; ++i) {
    clear(&hashers[i]);
  }
  printf("setting 'run' to 1\n");
  for (int i = 0; i < NUM_HASHERS; ++i) {
    write_csr(&hashers[i], 1);
  }
  printf("waiting for hashing engine\n");
  // wait for the last - it was started last, so when it's finished,
  // all should be.
  while(read_csr(&hashers[NUM_HASHERS - 1])) {
    alt_busy_sleep(1000000);
    write_led((count & 1) ? -1 : 0);
    printf("count: %d\n", count);
    count++;
  }
#endif

  int num_passwords = 0;
  for (int i = 0; i < NUM_HASHERS; ++i) {
    num_passwords += dump(&hashers[i]);
  }
  printf("%d hashing engines found %d passwords total\n", NUM_HASHERS, num_passwords);
  write_led(0);
  return 0;
}
