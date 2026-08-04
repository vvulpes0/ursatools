#include "reloc.h"

static long long
extractu8(unsigned char const *buf)
{
	return *buf;
}
static _Bool
writeu8(unsigned char *buf, long long v)
{
	if (v < -0x80LL || v > 0xFFLL) return 0;
	buf[0] = v&0xFF;
	return 1;
}

static long long
extractu16(unsigned char const *buf)
{
	return 0LL + buf[0] + (buf[1]<<8LL);
}
static _Bool
writeu16(unsigned char *buf, long long v)
{
	if (v < -0x8000LL || v > 0xFFFFLL) return 0;
	buf[0] = v&0xFF;
	buf[1] = (v>>8)&0xFF;
	return 1;
}

static long long
extractu32(unsigned char const *buf)
{
	return 0LL + buf[0] + (buf[1]<<8LL) + (buf[2]<<16LL) + (buf[3]<<24LL);
}
static _Bool
writeu32(unsigned char *buf, long long v)
{
	if (v < -0x80000000LL || v > 0xFFFFFFFFLL) return 0;
	buf[0] = v&0xFF;
	buf[1] = (v>>8)&0xFF;
	buf[2] = (v>>16)&0xFF;
	buf[3] = (v>>24)&0xFF;
	return 1;
}

static long long
unalu4(long long v)
{
	long long x = v&0x0700;
	long long b = v&0x000F;
	return b<<(x>>6);
}
static _Bool
writealu4(unsigned char *buf, long long v)
{
	int i = 0;
	for (i = 0; i < 32; i += 4) {
		if ((v&~(0xF<<i)) == 0) break;
	}
	if (i >= 32) return 0;
	buf[0] = (buf[0]&~0xF)|(v>>i);
	buf[1] = (buf[1]&~0x7)|(i>>2);
	return 1;
}

static long long
unsset8(long long v)
{
	long long sx = v&0x0F00;
	long long  b = v&0x000F;
	return (sx>>4)|b;
}
static _Bool
writesset8(unsigned char *buf, long long v)
{
	if (v < 0LL || v > 0xFFLL) return 0;
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0xF)|((v>>4)&0xF);
	return 1;
}
static _Bool
writesset8nc0(unsigned char *buf, long long v)
{
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0xF)|((v>>4)&0xF);
	return 1;
}
static _Bool
writesset8nc1(unsigned char *buf, long long v)
{
	v >>= 8;
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0xF)|((v>>4)&0xF);
	return 1;
}
static _Bool
writesset8nc2(unsigned char *buf, long long v)
{
	v >>= 16;
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0xF)|((v>>4)&0xF);
	return 1;
}
static _Bool
writesset8nc3(unsigned char *buf, long long v)
{
	v >>= 24;
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0xF)|((v>>4)&0xF);
	return 1;
}

static long long
unabs5(long long v)
{
	long long s = v&0x0800;
	long long b = v&0x000F;
	return (s>>7)|b;
}
static _Bool
writeabs5(unsigned char *buf, long long v)
{
	if (v < 0LL | v > 0x1FLL) return 0;
	buf[0] = (buf[0]&~0xF)|(v&0xF);
	buf[1] = (buf[1]&~0x8)|((v&0x10)>>1);
	return 1;
}

static long long
unboff3(long long v)
{
	long long x = v&0x0700;
	return x>>8;
}
static _Bool
writeboff3(unsigned char *buf, long long v)
{
	if (v < 0LL || v > 0x7LL) return 0;
	buf[1] = (buf[1]&~0x7)|v;
	return 1;
}

static long long
unwoff3(long long v)
{
	long long x = v&0x0700;
	return x>>6;
}
static _Bool
writewoff3(unsigned char *buf, long long v)
{
	if (v&0x3LL) return 0;
	v >>= 2;
	if (v < 0LL || v > 0x7LL) return 0;
	buf[1] = (buf[1]&~0x7)|v;
	return 1;
}


static long long
unpc8(long long v)
{
	long long ab = v&0x00FF;
	if (ab > 127) ab -= 256;
	return ab<<1;
}
static _Bool
writepc8(unsigned char *buf, long long v)
{
	if (v&1) return 0;
	v /= 2;
	if (v < -128LL || v >= 128LL) return 0;
	buf[0] = v;
	return 1;
}

_Bool
rel_apply(enum reloc r, unsigned char *buf,
          long long place, long long value, long long xadd)
{
	long long add = rel_extract(r, buf) + xadd;
	long long v = (r == R_URSA_PC8)? value + add - place : value + add;
	switch (r) {
	case R_URSA_NONE: return 0;
	case R_URSA_ABS8: return writeu8(buf, v);
	case R_URSA_ABS16: return writeu16(buf, v);
	case R_URSA_ABS32: return writeu32(buf, v);
	case R_URSA_ALU4: return writealu4(buf, v);
	case R_URSA_SSET8: return writesset8(buf, v);
	case R_URSA_SSET8_0_NC: return writesset8nc0(buf, v);
	case R_URSA_SSET8_1_NC: return writesset8nc1(buf, v);
	case R_URSA_SSET8_2_NC: return writesset8nc2(buf, v);
	case R_URSA_SSET8_3_NC: return writesset8nc3(buf, v);
	case R_URSA_ABS5: return writeabs5(buf, v);
	case R_URSA_BOFF3: return writeboff3(buf, v);
	case R_URSA_WOFF3: return writewoff3(buf, v);
	case R_URSA_PC8: return writepc8(buf, v);
	case R_URSA_ILLEGAL: return 0;
	default: return 0;
	}
	return 0;
}

long long
rel_extract(enum reloc r, unsigned char const *buf)
{
	if (!buf) return 0;
	switch (r) {
	case R_URSA_NONE: return 0;
	case R_URSA_ABS8: return extractu8(buf);
	case R_URSA_ABS16: return extractu16(buf);
	case R_URSA_ABS32: return extractu32(buf);
	case R_URSA_ALU4: return unalu4(extractu16(buf));
	case R_URSA_SSET8:
	case R_URSA_SSET8_0_NC:
	case R_URSA_SSET8_1_NC:
	case R_URSA_SSET8_2_NC:
	case R_URSA_SSET8_3_NC:
		return unsset8(extractu16(buf));
	case R_URSA_ABS5: return unabs5(extractu16(buf));
	case R_URSA_BOFF3: return unboff3(extractu16(buf));
	case R_URSA_WOFF3: return unwoff3(extractu16(buf));
	case R_URSA_PC8: return unpc8(extractu16(buf));
	default: return 0;
	}
	return 0;
}
