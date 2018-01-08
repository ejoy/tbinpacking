local tbinpack = require "tbinpack"
local lfs = require "lfs"

local function parse_opt(...)
	local t = { ... }
	local ret = {}
	local opt
	for i = 1, #t do
		local v = t[i]
		if v:sub(1,1) == "-" then
			opt = v:sub(2)
			ret[opt] = true
		else
			if opt then
				ret[opt] = v
				opt = nil
			else
				table.insert(ret, v)
			end
		end
	end
	return ret
end

local function fetch_source(input_path)
	local img = {}
	for filename in lfs.dir(input_path) do
		local name, ext = filename:match "(.*)%.(%a+)$"
		ext = ext and ext:lower()
		if ext == "png" or ext == "tga" then
			filename = string.format("%s/%s", input_path, filename)
			local w,h,x,y,minw,minh =tbinpack.loadimage(filename)
			table.insert(img, { name = name, filename = filename, w = minw, h = minh, kx = x, ky = y } )
		end
	end
	return img
end

local function output_altas(rect, filename)
	local f
	if filename then
		filename = filename .. ".altas"
		f = assert(io.open(filename, "w"))
		io.output(f)
	end

	for _, v in ipairs(rect) do
		local tid = ""
		if v.tid ~= 0 then
			tid = string.format(" tid=%d", v.tid)
		end
		local line = string.format("%s kx=%d ky=%d width=%d height=%d x=%d y=%d%s\n",
			v.filename, v.kx, v.ky, v.w, v.h, v.x, v.y, tid)
		io.write(line)
	end

	if f then
		f:close()
	end
end

local function combine_textures(rect, filename, width, height)
	local t = {}
	for _, v in ipairs(rect) do
		local tid = v.tid + 1
		local texture = t[tid]
		if texture == nil then
			texture = {}
			t[tid] = texture
		end
		table.insert(texture, v)
	end
	for index, v in ipairs(t) do
		local of = string.format("%s%d.png", filename, index-1)
		tbinpack.combine(of, width, height, v)
	end
end

local USAGE = [[
Options:
	-o outputfilename
	-image (if enable, output combined image)
	-i inputdir
	-w width (default is 1024)
	-h height (default is width)

Example:
	lua textpack.lua -o output -i images -w 1024 -h 1024
]]

local function main(...)
	local args = parse_opt(...)
	if next(args) == nil then
		print(USAGE)
	end
	local input_path = assert(args.i)
	local rect = fetch_source(input_path)
	local width = args.w or 1024
	local height = args.h or width
	tbinpack.binpack(rect, width, height)
	output_altas(rect, args.o)
	if args.image then
		combine_textures(rect, args.o or "output", width, height)
	end
end

main(...)
