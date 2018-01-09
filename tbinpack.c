#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct minrect {
	int width;
	int height;
	int x;
	int y;
	int minw;
	int minh;
};

// return 1 means empty line
static int
find_boundary(stbi_uc *line, int width, int *left, int *right) {
	int i;
	int lb = 0;
	for (i=0;i<width;i++) {
		stbi_uc c = line[i*4+3];	// read alpha
		if (c != 0) {
			lb = i;
			break;
		}
	}
	if (i == width) {
		return 1;
	}
	int rb = 0;
	for (i=width-1;i>=0;i--) {
		stbi_uc c = line[i*4+3];	// read alpha
		if (c != 0) {
			rb = width-1-i;
			break;
		}
	}
	*left = lb;
	*right = rb;
	return 0;
}

static void
calc_rect(struct minrect *rect, stbi_uc *buffer) {
	int w = rect->width;
	int h = rect->height;
	int left, right, bottom, i;

	for (i=0;i<h;i++) {
		int lb, rb;
		int empty = find_boundary(buffer, w, &lb, &rb);
		buffer += 4 * w;
		if (!empty) {
			// found top boundary
			rect->y = i;
			bottom = i;
			++i;
			left = lb;
			right = rb;
			for (;i<h;i++) {
				int empty = find_boundary(buffer, w, &lb, &rb);
				buffer += 4 * w;
				if (!empty) {
					bottom = i;
					if (lb < left)
						left = lb;
					if (rb < right)
						right = rb;
				}
			}
			rect->x = left;
			rect->minw = w - left - right;
			rect->minh = bottom - rect->y + 1;
			return;
		}
	}
	// empty image
	rect->x = 0;
	rect->y = 0;
	rect->minw = 0;
	rect->minh = 0;

}

static void
get_block(lua_State *L, const uint8_t * img, int w, int h, int x, int y) {
	char buffer[16*4];
	uint8_t *target = (uint8_t *)buffer;
	img += (w * y + x) * 4;
	if (x+4 > w || y+4> h) {
		int i,j;
		for (i=0;i<4;i++) {
			if (y+i >= h) {
				memset(target, 0, 16);
			} else {
				for (j=0;j<4;j++) {
					if (x+j >= w) {
						target[j*4+0] = 0;
						target[j*4+1] = 0;
						target[j*4+2] = 0;
						target[j*4+3] = 0;
					} else {
						target[j*4+0] = img[j*4+0];
						target[j*4+1] = img[j*4+1];
						target[j*4+2] = img[j*4+2];
						target[j*4+3] = img[j*4+3];
					}
				}
				img += w * 4;
				target += 16;
			}
		}
	} else {
		int i;
		for (i=0;i<4;i++) {
			memcpy(target, img, 16);
			img += w * 4;
			target += 16;
		}
	}
	lua_pushlstring(L, buffer, 16*4);
}

static int
loadimage(lua_State *L) {
	const char *filename = luaL_checkstring(L, 1);
	int loadcontent = lua_toboolean(L, 2);
	int x,y,channels;
	stbi_uc * buffer = stbi_load(filename, &x, &y, &channels, 0);
	if (buffer == NULL) {
		const char * err = stbi_failure_reason();
		lua_pushstring(L, err);
		return lua_error(L);
	}
	if (channels != 4) {
		stbi_image_free(buffer);
		return luaL_error(L, "%s has not RGBA channels", filename);
	}
	struct minrect rect;
	rect.width = x;
	rect.height = y;
	calc_rect(&rect, buffer);
	lua_pushinteger(L, rect.width);
	lua_pushinteger(L, rect.height);
	lua_pushinteger(L, rect.x);
	lua_pushinteger(L, rect.y);
	lua_pushinteger(L, rect.minw);
	lua_pushinteger(L, rect.minh);
	if (loadcontent) {
		int bw = rect.minw / 4 + 1;	// add 1 pixel border
		int bh = rect.minh / 4 + 1;
		lua_createtable(L, bw*bh, 2);
		lua_pushinteger(L, bw);
		lua_setfield(L, -2, "x");
		lua_pushinteger(L, bh);
		lua_setfield(L, -2, "y");
		int i,j;
		int index = 1;
		for (i=0;i<bh;i++) {
			for (j=0;j<bw;j++) {
				get_block(L, buffer, rect.width, rect.height, rect.x + j*4, rect.y + i*4);
				lua_seti(L, -2, index);
				++index;
			}
		}
	} else {
		lua_pushnil(L);
	}

	stbi_image_free(buffer);
	return 7;
}

static int
binpack(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int width = luaL_checkinteger(L, 2);
	int height = luaL_checkinteger(L, 3);
	int border = luaL_optinteger(L, 4, 1);	// add border to each sprite
	int n = lua_rawlen(L, 1);
	struct stbrp_rect * rect = lua_newuserdata(L, n * sizeof(*rect));
	int i;
	for (i=0;i<n;i++) {
		struct stbrp_rect * r = &rect[i];
		int id = i + 1;
		if (lua_geti(L, 1, i+1) != LUA_TTABLE) {
			return luaL_error(L, "Invalid rect at index %d", id);
		}
		r->id = id;
		if (lua_getfield(L, -1, "w") != LUA_TNUMBER) {
			return luaL_error(L, "Missing w at index %d", id);
		}
		r->w = lua_tointeger(L, -1);
		if (r->w > width) {
			return luaL_error(L, "Rect at index %d's width(%d) > %d", id, r->w, width);
		}
		if (lua_getfield(L, -2, "h") != LUA_TNUMBER) {
			return luaL_error(L, "Missing h at index %d", id);
		}
		r->h = lua_tointeger(L, -1);
		if (r->h > height) {
			return luaL_error(L, "Rect at index %d's height(%d) > %d", id, r->h, height);
		}
		r->w += border;
		r->h += border;
		lua_pop(L, 3);
	}
	width += border;
	height += border;
	int num_nodes = width * 2;
	stbrp_node *temp = lua_newuserdata(L, num_nodes * sizeof(*temp));

	stbrp_context context;
	int texture;

	for (texture = 0; ; ++texture) {
		stbrp_init_target(&context, width, height, temp, num_nodes);
		int all = stbrp_pack_rects (&context, rect, n);
		for (i=0;i<n;i++) {
			struct stbrp_rect * r = &rect[i];
			if (r->was_packed) {
				int id = r->id;
				lua_geti(L, 1, id);
				lua_pushinteger(L, r->x);
				lua_setfield(L, -2, "x");
				lua_pushinteger(L, r->y);
				lua_setfield(L, -2, "y");
				lua_pushinteger(L, texture);
				lua_setfield(L, -2, "tid");
				lua_pop(L, 1);
			}
		}
		if (all)
			break;
		int index = 0;
		for (i=0;i<n;i++) {
			struct stbrp_rect * r = &rect[i];
			if (!r->was_packed) {
				if (index != i) {
					rect[index].id = r->id;
					rect[index].w = r->w;
					rect[index].h = r->h;
				}
				++index;
			}
		}
		n = index;
	}
	return 0;
}

static int
getint(lua_State *L, const char * field, int id) {
	if (lua_getfield(L, -1, field) != LUA_TNUMBER) {
		return luaL_error(L, "Invalid %s at index %d", field, id);
	}
	int r = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return r;
}

static void
write_image(lua_State *L, stbi_uc * buffer, int stride, const char * filename, int kx, int ky, int w, int h, int x, int y) {
	int image_w, image_h, channels;
	stbi_uc * image = stbi_load(filename, &image_w, &image_h, &channels, 4);
	if (kx + w > image_w || ky + h > image_h) {
		luaL_error(L, "Invalid rect (%dx%d %d,%d) for image %s (%dx%d)", w,h,kx,ky, image_w, image_h);
	}
	buffer += (stride * y + x) * 4;
	stbi_uc * src = image + (image_w * ky + kx) * 4;
	int i;
	for (i=0;i<h;i++) {
		memcpy(buffer, src, w * 4);
		buffer += stride * 4;
		src += image_w * 4;
	}

	stbi_image_free(image);
}

// draw write rect
static void
write_image_rect(lua_State *L, stbi_uc * buffer, int stride, int w, int h, int x, int y) {
	buffer += (stride * y + x) * 4;
	int i;
	memset(buffer, 0xff, w*4);
	for (i=0;i<h-2;i++) {
		buffer += stride * 4;
		buffer[0] = 0xff;
		buffer[1] = 0xff;
		buffer[2] = 0xff;
		buffer[3] = 0xff;
		buffer[w * 4 - 4] = 0xff;
		buffer[w * 4 - 3] = 0xff;
		buffer[w * 4 - 2] = 0xff;
		buffer[w * 4 - 1] = 0xff;
	}
	if (h > 0) {
		buffer += stride * 4;
		memset(buffer, 0xff, w*4);
	}
}

static int
combine(lua_State *L) {
	const char * filename = luaL_checkstring(L, 1);
	int width = luaL_checkinteger(L, 2);
	int height = luaL_checkinteger(L, 3);
	luaL_checktype(L, 4, LUA_TTABLE);
	int n = lua_rawlen(L, 4);
	int debugline = lua_toboolean(L, 5);
	int i;
	size_t sz = width * height * 4;
	stbi_uc * buffer = lua_newuserdata(L, sz);
	memset(buffer, 0, sz);
	for (i=0;i<n;i++) {
		int id = i+1;
		if (lua_geti(L, 4, id) != LUA_TTABLE) {
			return luaL_error(L, "Invalid source at index %d", id);
		}
		if (lua_getfield(L, -1, "filename") != LUA_TSTRING) {
			return luaL_error(L, "Invalid filenanme at index %d", id);
		}
		const char * imagefn = luaL_checkstring(L, -1);
		lua_pop(L, 1);
		int kx = getint(L, "kx", id);
		int ky = getint(L, "ky", id);
		int w = getint(L, "w", id);
		int h = getint(L, "h", id);
		int x = getint(L, "x", id);
		int y = getint(L, "y", id);
		if (kx < 0 || ky < 0 || x + w > width || y + h > height) {
			return luaL_error(L, "Out of boundary (%dx%d %d,%d) at index %d", w,h,x,y,id);
		}
		lua_pop(L, 1);

		write_image(L, buffer, width, imagefn, kx, ky, w, h, x, y);
		if (debugline)
			write_image_rect(L, buffer, width, w,h, x, y);
	}
	if (!stbi_write_png(filename, width, height, 4, buffer, width * 4)) {
		return luaL_error(L, "Can't write to %s", filename);
	}
	return 0;
}

struct desc_bits {
	uint8_t *ptr;
	int pos;
};

static void
write_desc_bits(struct desc_bits *bits, int type) {
	if (bits->pos == 0) {
		*bits->ptr = type;
		bits->pos = 2;
	} else {
		*bits->ptr |= type << bits->pos;
		bits->pos += 2;
		if (bits->pos >= 8) {
			++bits->ptr;
			bits->pos = 0;
		}
	}
}

// each block use 2 bit description, 11 for RGBA 128bit, 10 for RGB 64bit, 00 for empty (all alpha are 0)
// one byte store 4 blocks description from low bits to high bits, row-major order. The description bytes just follows block data.
// for example, 7*7 blocks use 13 bytes description, follows by the data stream follows, which is 16 or 8 bytes per block.
static int
etc2pack(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int n = lua_rawlen(L, 1);
	int desc_len = (n + 3)/4;
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	uint8_t tmp[desc_len];
	struct desc_bits desc = { tmp, 0 };
	int i;
	static uint8_t c_zero[8] = { 0,0,0,0,0,0,0,0 };
	static uint8_t c_one[8] = { 255,0,0,0,0,0,0,0 };
	for (i = 1; i <= n; i++) {
		lua_geti(L, 1, i);
		size_t sz;
		const char * block = luaL_checklstring(L, -1, &sz);
		if (sz != 16) {
			return luaL_error(L, "Invalid etc2 block length at index %d", i);
		}
//		if (memcmp(c_zero, block, 8) == 0) {
//			write_desc_bits(&desc, 0);
//		} else if (memcmp(c_one, block, 8) == 0) {
//			write_desc_bits(&desc, 2);	// 10
//			luaL_addlstring(&b, (const char *)block+8, 8);	// write 64bit color only
//		} else {
//			write_desc_bits(&desc, 3);	// 11
			luaL_addlstring(&b, (const char *)block, 16);	// write 64bit color + 64bit alpha
//		}
		lua_pop(L, 1);
	}
//	luaL_addlstring(&b, (const char *)tmp, desc_len);
	luaL_pushresult(&b);
	return 1;
}

LUAMOD_API int
luaopen_tbinpack(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "loadimage", loadimage },
		{ "binpack", binpack },
		{ "combine", combine },
		{ "etc2pack", etc2pack },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}


