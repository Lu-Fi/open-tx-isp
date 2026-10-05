#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_mdns.h"
static unsigned int seed=87139;
static unsigned int rng(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }

/* TEMPER (Module_Ratio index 1) end to end: strength -> params -> state ->
 * MDNS register words with the gc5603-t41.bin day rows: 0 and 255 must move
 * the strength-owned registers away from 128. (OEM 128 is not a no-op: values
 * at the row maximum are zeroed, see t41_mdns_strength.) */
static unsigned int mdns_word(const struct t41_dpc_word *w,int n,unsigned int a)
{
	int i;
	for(i=0;i<n;++i) if(w[i].address==a) return w[i].value;
	assert(0);
	return 0;
}
static void temper_end_to_end(void)
{
	static const unsigned char rows[5][11]={
		{3,4,8,12,16,20,24,32,48,64,128},{3,4,8,12,16,20,24,32,48,64,128},
		{32,40,48,64,96,128,128,128,128,128,128},{8,16,24,32,40,48,56,64,72,80,96},
		{24,28,32,40,56,64,80,96,128,128,128}};
	static const unsigned short offsets[]={0,0x21,0x42,0xe7,0xc6};
	static const unsigned int regs[]={0xf114,0xf118,0xf11c,0xf150,0xf198};
	unsigned char original[0x48e],p[T41_MDNS_PARAM_BYTES],cal[T41_MDNS_PARAM_BYTES],s[T41_MDNS_STATE_BYTES];
	struct t41_dpc_word w[3][T41_MDNS_WRITES];
	static const unsigned int strengths[]={128,0,255};
	unsigned int i,j,k,changed=0,gain;
	int n[3];
	for(gain=1U<<16;gain<=(8U<<16);gain<<=3) {
		seed=4711;
		do {
			for(i=0;i<sizeof(cal);++i) cal[i]=rng()%64+1;
			cal[0x26]=cal[0x27]=cal[0x28]=4;	/* block size source rows */
			memset(original,0,sizeof(original));
			for(i=0;i<5;++i) for(j=0;j<11;++j)
				cal[0x27e + offsets[i] + j]=original[offsets[i]+j]=rows[i][j];
			memcpy(p,cal,sizeof(p));
			assert(!t41_mdns_interpolate(p,sizeof(p),s,sizeof(s),gain));
			n[0]=t41_mdns_pack(p,sizeof(p),s,sizeof(s),0,1920,1080,w[0],T41_MDNS_WRITES);
		} while(n[0]<=0);
		for(k=0;k<3;++k) {
			memcpy(p,cal,sizeof(p));
			assert(!t41_mdns_strength(original,sizeof(original),p,sizeof(p),strengths[k],0));
			assert(!t41_mdns_interpolate(p,sizeof(p),s,sizeof(s),gain));
			n[k]=t41_mdns_pack(p,sizeof(p),s,sizeof(s),0,1920,1080,w[k],T41_MDNS_WRITES);
			assert(n[k]>0);
		}
		for(i=0;i<sizeof(regs)/sizeof(regs[0]);++i) {
			unsigned int neutral=mdns_word(w[0],n[0],regs[i]);
			changed+=mdns_word(w[1],n[1],regs[i])!=neutral;
			changed+=mdns_word(w[2],n[2],regs[i])!=neutral;
		}
	}
	assert(changed>=8);
}

int main(void)
{
	unsigned char p[T41_MDNS_PARAM_BYTES+2],s[T41_MDNS_STATE_BYTES+2],before[sizeof(s)],original[0x48e];
	struct t41_dpc_word words[130],saved[130];
	unsigned int i,f;
	memset(p,0,sizeof(p)); memset(s,0xa5,sizeof(s)); memset(words,0xa7,sizeof(words));
	memcpy(before,s,sizeof(s)); memcpy(saved,words,sizeof(words));
	assert(t41_mdns_interpolate(p+1,T41_MDNS_PARAM_BYTES-1,s+1,T41_MDNS_STATE_BYTES,0)<0);
	assert(t41_mdns_interpolate(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES-1,0)<0);
	assert(t41_mdns_interpolate(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,~0U)<0);
	assert(!memcmp(s,before,sizeof(s)));
	assert(t41_mdns_pack(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,2,640,480,words+1,128)<0);
	assert(t41_mdns_pack(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,0,640,480,words+1,127)<0);
	s[5]=0;
	assert(t41_mdns_pack(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,0,640,480,words+1,128)<0);
	s[5]=20; s[1+0x84]=100; s[1+0x85]=92;
	assert(t41_mdns_pack(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,0,640,480,words+1,128)<0);
	assert(!memcmp(words,saved,sizeof(words)));
	for(f=0;f<5000;++f) {
		int count;
		for(i=1;i<sizeof(p)-1;++i) p[i]=rng();
		for(i=0;i<sizeof(original);++i) original[i]=rng();
		assert(!t41_mdns_strength(original,sizeof(original),p+1,T41_MDNS_PARAM_BYTES,f%256,f&1));
		assert(!t41_mdns_interpolate(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,rng()%((16U<<16)+1)));
		assert(s[0]==0xa5 && s[sizeof(s)-1]==0xa5);
		count=t41_mdns_pack(p+1,T41_MDNS_PARAM_BYTES,s+1,T41_MDNS_STATE_BYTES,f&1,
			1+rng()%8192,1+rng()%8192,words+1,128);
		assert(count<0 || (count>0 && count<=128));
		assert(!memcmp(words,saved,sizeof(words[0])));
		assert(!memcmp(words+129,saved+129,sizeof(words[0])));
		assert(t41_mdns_pack_enable(p+1,T41_MDNS_PARAM_BYTES,f&1,f%16,words+1,128)==2);
	}
	temper_end_to_end();
	puts("t41 MDNS ramps, strength, interpolation, geometry, unaligned buffers and zero-divisor guards, temper end to end: ok");
	return 0;
}
