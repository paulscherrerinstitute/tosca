#ifndef pevCompat_h
#define pevCompat_h

#ifdef __cplusplus
extern "C" {
#endif

void* pev_init(int x);
void* pevx_init(int x);
int pev_csr_rd(int addr);
void pev_csr_wr(int addr, int val);
void pev_csr_set(int addr, int val);
int pev_elb_rd(int addr);
int pev_smon_rd(int addr);
int pev_bmr_read(unsigned int card, unsigned int addr, unsigned int *val, unsigned int count);
float pev_bmr_conv_11bit_u(unsigned short val);
float pev_bmr_conv_11bit_s(unsigned short val);
float pev_bmr_conv_16bit_u(unsigned short val);
int pev_elb_wr(int addr, int val);
void pev_smon_wr(int addr, int val);
int pev_bmr_write(unsigned int card, unsigned int addr, unsigned int val, unsigned int count);

#ifdef __cplusplus
}
#endif
#endif
