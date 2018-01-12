#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <math.h>

struct segment {
	int left;
	int right;
};

static void
bitmap2segment(const uint8_t *rgba, int w, int h, int stride, struct segment *line) {
	int i,j;
	for (i=0;i<h;i++) {
		const uint8_t *l = &rgba[i*stride*4];
		int left = -1;
		for (j=0;j<w;j++) {
			int a = l[j*4+3];
			if (a > 0) {
				left = j;
				break;
			}
		}
		if (left == -1) {
			line[i].left = line[i].right = -1;
			continue;
		}
		line[i].left = left;
		line[i].right = left;
		for (j=w-1;j>left;j--) {
			int a = l[j*4+3];
			if (a > 0) {
				line[i].right = j;
				break;
			}
		}
	}
}

static int
skew(struct segment *line, int n, int x, struct segment *output, int *offx) {
	int i;
	int left=INT32_MAX,right=INT32_MIN;
	for (i=0;i<n;i++) {
		if (line[i].left < 0) {
			output[i].left = output[i].right = -1;
			continue;
		}
		int shiftl = x * (n-1-i) / (n-1);
		int shiftr = (x * (n-1-i) + n - 2 ) / (n-1);
		output[i].left = line[i].left + shiftl;
		if (output[i].left < left) {
			left = output[i].left;
		}
		output[i].right = line[i].right + shiftr;
		if (output[i].right > right) {
			right = output[i].right;
		}
	}
	for (i=0;i<n;i++) {
		output[i].left -= left;
		output[i].right -= left;
	}
	*offx = left;
	return right-left+1;
}

static void
rotate_segment(struct segment *line, int n, int width, struct segment *col) {
	int i,j;
	for (i=0;i<width;i++) {
		int top = -1;
		for (j=0;j<n;j++) {
			if (line[j].left < 0)
				continue;
			if (line[j].left <= i && line[j].right >= i) {
				top = j;
				break;
			}
		}
		if (top < 0) {
			col[i].left = -1;
			col[i].right = -1;
			continue;
		}
		col[i].left = top;
		col[i].right = top;
		for (j=n-1;j>top;j--) {
			if (line[j].left < 0)
				continue;
			if (line[j].left <= i && line[j].right >= i) {
				col[i].right = j;
				break;
			}
		}
	}
}

static int
find_min_skew(struct segment *line, int n , int width, int *skewx, int *offx) {
	struct segment temp[n];
	int tmp_off;
	int w = skew(line, n, 0, temp, &tmp_off);
	int i;
	int right = w;
	int right_skewx = 0;
	int right_offx = 0;
	for (i=1;i<width;i++) {
		int nw = skew(line, n, i, temp, &tmp_off);
		if (nw <= right) {
			right = nw;
			right_skewx = i;
			right_offx = tmp_off;
		} else if (nw > right + 1) { // 1 for ignore some error
			break;
		}
	}
	int left = w;
	int left_skewx = 0;
	int left_offx = 0;
	for (i=1;i<width;i++) {
		int nw = skew(line, n, -i, temp, &tmp_off);
		if (nw < left) {
			left = nw;
			left_skewx = -i;
			left_offx = tmp_off;
		} else if (nw > left + 1) {	// 1 for ignore some error
			break;
		}
	}
	if (left < right) {
		*skewx = left_skewx;
		*offx = left_offx;
		return left;
	}
	*skewx = right_skewx;
	*offx = right_offx;
	return right;
}

static int
min_segment(struct segment *line, int n, int *width, int *offx, int *offy) {
	int i;
	int top = -1;
	for (i=0;i<n;i++) {
		if (line[i].left >= 0) {
			top = i;
			break;
		}
	}
	if (top < 0) {
		*width = 0;
		*offx = 0;
		*offy = 0;
		return 0;
	}
	*offy = top;
	int bottom;
	for (bottom=n-1;bottom > top;bottom--) {
		if (line[bottom].left >=0)
			break;
	}
	n = bottom - top + 1;
	if (top > 0) {
		memcpy(line, line+top, n * sizeof(line[0]));
	}
	int left = line[0].left;
	int right = line[0].right;
	for (i=1;i<n;i++) {
		if (line[i].left < 0)
			continue;
		if (line[i].left < left)
			left = line[i].left;
		if (line[i].right > right)
			right = line[i].right;
	}
	if (left != 0) {
		for (i=0;i<n;i++) {
			line[i].left -= left;
			line[i].right -= left;
		}
	}
	*width = right - left + 1;
	*offx = left;
	return n;
}

struct transform {
	int bounding_x;
	int bounding_y;
	int bounding_w;
	int bounding_h;
	int skew_x;
	int skew_y;
	int skew_offx;
	int skew_offy;
	int minw;
	int minh;
};

static int
find_best_skew2(struct segment *line, int height, struct transform * trans) {
	int width;
	height = min_segment(line, height, &width, &trans->bounding_x, &trans->bounding_y);
	trans->bounding_w = width;
	trans->bounding_h = height;
	int area = height * width;
	trans->minw = width;
	trans->minh = height;
	trans->skew_x = 0;
	trans->skew_y = 0;
	int i;
	struct segment temp[height];
	for (i=-width;i<=width;i++) {
		int offx,offy;
		int new_width = skew(line, height, i, temp, &offx);

		struct segment cols[new_width];
		rotate_segment(temp, height, new_width, cols);
		
		int skewy;
		int new_height = find_min_skew(cols, new_width, height, &skewy, &offy);
		int new_area = new_width * new_height;
		if (new_area < area) {
			area = new_area;
			trans->skew_x = i;
			trans->skew_y = skewy;
			trans->skew_offx = offx;
			trans->skew_offy = offy;
			trans->minw = new_width;
			trans->minh = new_height;
		}
	}
	return area;
}

static void
shift_subpixel(const uint8_t *src, uint8_t * dest, float subpixel, int width) {
	int left = (int)(subpixel * 255);
	int right = 255 - left;
	int i,j;
	for (i=0;i<4;i++) {
		dest[0+i] = src[0+i] * left / 255;
		for (j=4;j<width*4;j+=4) {
			dest[j+i] = (src[j+i-4] * right + src[j+i] * left) / 255;
		}
		dest[j+i] = src[j+i-4] * right / 255;
	}
}

static void
skew_x(const uint8_t *src, uint8_t * dest , int w, int h, int offx, int stride_src, int stride_dest) {
	int i;
	if (offx < 0) {
		dest -= offx*4;
	} else {
	}
	stride_src *= 4;
	stride_dest *= 4;
	if (h == 1) {
		memcpy(dest+offx*4, src, w * 4);
		return;
	}
	for (i=0;i<h;i++) {
		float shift = (float)offx * (h-1-i) / (h-1);
		float f = floorf(shift);
		int ishift = (int)f;
		if (f == shift) {
			memcpy(dest + ishift * 4, src, w*4);
		} else {
			float subpixel = 1.0f + f - shift;
			shift_subpixel(src, dest + ishift * 4, subpixel, w);
		}
		src += stride_src;
		dest += stride_dest;
	}
}

static void
rotate_image(const uint8_t *src, uint8_t *dest, int w, int h) {
	int i,j;
	for (i=0;i<h;i++) {
		for (j=0;j<w;j++) {
			memcpy(dest + j*h*4, src, 4);
			src += 4;
		}
		dest += 4;
	}
}

int
transform_image(lua_State *L) {
	int width = luaL_checkinteger(L, 1);
	int height = luaL_checkinteger(L, 2);
	size_t sz;
	const char * img = luaL_checklstring(L, 3, &sz);
	if (sz != width * height * 4) {
		return luaL_error(L, "Invalid image size %dx%dx4=%d, %d", width, height, width * height * 4, (int)sz);
	}
	const uint8_t * buffer = (const uint8_t *)img;

	struct segment lines[height];
	bitmap2segment(buffer,width,height,width,lines);
	struct transform t={0};
	find_best_skew2(lines, height, &t);

	int w = t.bounding_w + abs(t.skew_x);
	int h = t.bounding_h;
	int sz1 = w*h*4;
	uint8_t * skewx_img = malloc(sz1);
	memset(skewx_img, 0, sz1);
	skew_x(buffer + t.bounding_x * 4 + t.bounding_y * width * 4,	// src
		skewx_img, // dest
		t.bounding_w,
		t.bounding_h,
		t.skew_x,
		width,	// stride for src
		t.bounding_w + abs(t.skew_x) // stride for dest
		);

	uint8_t * skewx_img_r = malloc(sz1);
	rotate_image(skewx_img, skewx_img_r, w,h);
	free(skewx_img);

	int w2 = h + abs(t.skew_y);
	int h2 = t.minw;
	int sz2 = w2*h2*4;
	uint8_t * skewy_img = malloc(sz2);
	memset(skewy_img, 0, sz2);
	int offx = t.skew_offx;
	if (t.skew_x < 0) {
		offx -= t.skew_x;
	}
	skew_x(skewx_img_r + offx * h * 4,
		skewy_img, // dest
		h,
		t.minw,
		t.skew_y,
		h,
		w2
		);
	
	free(skewx_img_r);

	uint8_t * output = malloc(sz2);
	rotate_image(skewy_img, output, w2,h2);
	free(skewy_img);

	int offy = t.skew_offy;
	if (t.skew_y < 0) {
		offy -= t.skew_y;
	}

	lua_newtable(L);
	lua_pushinteger(L, t.minw);
	lua_setfield(L, -2, "w");
	lua_pushinteger(L, t.minh);
	lua_setfield(L, -2, "h");
	lua_pushlstring(L, (const char *)(output + t.minw * offy * 4), t.minw * t.minh * 4);
	lua_setfield(L, -2, "content");

	// The transform : 
	//  1. get the bounding box of origin image
	//  2. skew x
	//  3. translation offset x
	//  4. skew y
	//  5. translation offset y
	lua_pushinteger(L, t.skew_x);
	lua_setfield(L, -2, "skewx");
	lua_pushinteger(L, t.skew_y);
	lua_setfield(L, -2, "skewy");
	lua_pushinteger(L, t.skew_offx);
	lua_setfield(L, -2, "offx");
	lua_pushinteger(L, t.skew_offy);
	lua_setfield(L, -2, "offy");

	lua_createtable(L, 16, 0);	// mapping for ejoy2d

	float x[4],y[4];

	y[0] = (float)t.skew_offy - t.skew_y;
	x[0] = t.skew_x * (y[0] - t.bounding_h) / t.bounding_h + t.skew_offx;

	y[1] = (float)t.skew_offy;
	x[1] = t.skew_x * (y[1] - t.bounding_h) / t.bounding_h + t.skew_offx + t.minw;

	y[2] = y[0] + t.minh;
	x[2] = t.skew_x * (y[2] - t.bounding_h) / t.bounding_h + t.skew_offx;

	y[3] = y[1] + t.minh;
	x[3] = t.skew_x * (y[3] - t.bounding_h) / t.bounding_h + t.skew_offx + t.minw;

	int uv[8] = {
		0,0,
		0, t.minh,
		t.minw, t.minh,
		t.minw, 0
	};
	int i;
	for (i=0;i<8;i++) {
		lua_pushinteger(L, uv[i]);
		lua_seti(L, -2, i+1);
	}
	float screen[8] = {
		x[0],y[0],
		x[2],y[2],
		x[3],y[3],
		x[1],y[1],
	};
	for (i=0;i<8;i+=2) {
		screen[i] += t.bounding_x;
		screen[i+1] += t.bounding_y;
	}
	for (i=0;i<8;i++) {
		lua_pushnumber(L, screen[i]);
		lua_seti(L, -2, i+9);
	}
	lua_setfield(L, -2, "mapping");

	// Notice: If lua_pushlstring raise oom error, output may leak.

	free(output);

	return 1;
}
