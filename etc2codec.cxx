#include "etcpack.cxx"
#undef R
#undef G
#undef B

extern "C" {

#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>

}

static inline void
big_endian_encode(unsigned int block, uint8_t r[4]) {
	r[0] = (uint8_t)(block >> 24);
	r[1] = (uint8_t)(block >> 16);
	r[2] = (uint8_t)(block >> 8);
	r[3] = (uint8_t)(block);
}

static inline unsigned int
big_endian_decode(const uint8_t r[4]) {
	return (unsigned int)r[0] << 24 | r[1] << 16 | r[2] << 8 | r[3];
}

/*
	string source rgba
	string flag	[21][fs][pn]
		f fast default
		s slow
		p perceptual default
		n nonperceptual
 */
static int
lcompress(lua_State *L) {
	size_t sz;
	const char * data = luaL_checklstring(L, 1, &sz);
	if (sz != 16*4) {
		return luaL_error(L, "Not 4x4 RGBA block");
	}
	int fast = 1;
	int perceptual = 1;
	if (lua_isstring(L, 2)) {
		const char *flags = lua_tostring(L, 2);
		int i;
		for (i=0;flags[i];i++) {
			switch(flags[i]) {
			case 's':
				fast = 0;
				break;
			case 'f':
				fast = 1;
				break;
			case 'p':
				perceptual = 1;
				break;
			case 'n':
				perceptual = 0;
				break;
			default:
				return luaL_error(L, "Unknown flags %s", flags);
			}
		}
	}
	uint8_t color[16*16*3];
	uint8_t color_dec[16*16*3];
	uint8_t alpha[16*16];
	uint8_t result[16];
	int i;
	for (i=0;i<16*16;i++) {
		color[i*3+0] = data[i*4+0];
		color[i*3+1] = data[i*4+1];
		color[i*3+2] = data[i*4+2];
		alpha[i] = data[i*4+3];
	}
	unsigned int block1, block2;
	if (fast) {
		if (perceptual) {
			compressBlockETC2FastPerceptual(color, color_dec, 4, 4, 0, 0, block1, block2);
		} else {
			compressBlockETC2Fast(color, alpha, color_dec, 4, 4, 0, 0, block1, block2);
		}
	} else {
		if (perceptual) {
			compressBlockETC2ExhaustivePerceptual(color, color_dec, 4, 4, 0, 0, block1, block2);
		} else {
			compressBlockETC2Exhaustive(color, color_dec, 4, 4, 0, 0, block1, block2);
		}
	}
	if (fast) {
		compressBlockAlphaFast(alpha, 0, 0, 4, 4, result);
	} else {
		compressBlockAlphaSlow(alpha, 0, 0, 4, 4, result);
	}

	big_endian_encode(block1, result+8);
	big_endian_encode(block2, result+12);

	lua_pushlstring(L, (const char *)result, 16);

	return 1;
}

static int
luncompress(lua_State *L) {
	size_t sz;
	const char * data = luaL_checklstring(L, 1, &sz);
	if (sz != 16) {
		return luaL_error(L, "The size of ETC2 RGBA block should be 16 bytes.");
	}
	uint8_t alphaimg[16];
	decompressBlockAlpha((uint8 *)(data),alphaimg,4,4,0,0);
	uint8_t img[16*3];
	unsigned int block1 = big_endian_decode((const uint8_t *)(data + 8));
	unsigned int block2 = big_endian_decode((const uint8_t *)(data + 12));
	decompressBlockETC2(block1, block2,img,4,4,0,0);
	uint8_t result[16*4];
	int i;
	for (i=0;i<16;i++) {
		result[i*4+0] = img[i*3+0];
		result[i*4+1] = img[i*3+1];
		result[i*4+2] = img[i*3+2];
		result[i*4+3] = alphaimg[i];
	}
	lua_pushlstring(L,(const char *)result, 16*4);

	return 1;
}

extern "C" {

LUAMOD_API int
luaopen_etc2codec(lua_State *L) {
	luaL_checkversion(L);
	setupAlphaTableAndValtab();
	luaL_Reg l[] = {
		{ "compress", lcompress },
		{ "uncompress", luncompress },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}

}
