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

static int
loadimage(lua_State *L) {
	const char *filename = luaL_checkstring(L, 1);
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
	stbi_image_free(buffer);
	lua_pushinteger(L, rect.width);
	lua_pushinteger(L, rect.height);
	lua_pushinteger(L, rect.x);
	lua_pushinteger(L, rect.y);
	lua_pushinteger(L, rect.minw);
	lua_pushinteger(L, rect.minh);
	return 6;
}

static int
binpack(lua_State *L) {
	int border = 1;	// add border to each sprite
	luaL_checktype(L, 1, LUA_TTABLE);
	int width = luaL_checkinteger(L, 2);
	int height = luaL_checkinteger(L, 3);
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

LUAMOD_API
int luaopen_tbinpack(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "loadimage", loadimage },
		{ "binpack", binpack },
		{ "combine", combine },
		{ NULL, NULL },
	};
	luaL_newlib(L, l);
	return 1;
}


