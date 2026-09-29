-- Frontend witness for the SDLPoP package: the project's movie plays inside
-- Chimera for a fixed number of frames, then the Game State and Level domains
-- are written out for the gate to compare with the native reference, and -
-- when this Chimera has them - the game.* functions are tried against the
-- same bytes.
--
-- The job comes from the file named by MINIHAWK_JOB:
--   frames=<how many frames to advance>
--   out=<directory for the dumps>
--   meta=<path for the result>
--   shot=<optional screenshot path>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local meta = {}
local function finish(status, detail)
	local lines = { "status=" .. status, "detail=" .. (detail or "") }
	for k, v in pairs(meta) do
		if k ~= "path" then lines[#lines + 1] = k .. "=" .. tostring(v) end
	end
	table.sort(lines)
	if meta.path then writeAll(meta.path, table.concat(lines, "\n") .. "\n") end
	client.exit()
end

local job = {}
for line in io.lines(os.getenv("MINIHAWK_JOB")) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.path = job.meta

if emu.getsystemid() ~= "PrinceOfPersia" then finish("ERROR", "wrong system id: " .. tostring(emu.getsystemid())) end
if emu.getcorename() ~= "SDLPoP" then finish("ERROR", "wrong core: " .. tostring(emu.getcorename())) end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

local frames = tonumber(job.frames) or 100
for _ = 1, frames do
	emu.frameadvance()
end
meta.frames = emu.framecount()
meta.lag = emu.lagcount()

local function dump(domain, file)
	memory.usememorydomain(domain)
	local size = memory.getcurrentmemorydomainsize()
	local bytes = memory.read_bytes_as_array(0, size, domain)
	local chunks = {}
	for i = 1, #bytes do chunks[i] = string.char(bytes[i]) end
	writeAll(job.out .. "/" .. file, table.concat(chunks))
	return bytes
end
local gs = dump("Game State", "gamestate.bin")
dump("Level", "level.bin")
if job.shot ~= nil and job.shot ~= "" then client.screenshot(job.shot) end

-- the project saved where it was opened, at the movie's end: its game time
-- headers are the machine's (docs/game-cores.md, gameTimer)
if job.save == "1" then
	local ok, err = pcall(function() movie.save() end)
	meta.saved = ok and "1" or tostring(err)
end

-- the property library, where this Chimera has it (docs/game-cores.md, Lua)
if game ~= nil and game.list ~= nil then
	local ok, err = pcall(function()
		local names = game.list()
		meta.game_list = #names
		meta.game_kid_x = game.get("Kid.X")
		meta.game_kid_x_byte = gs[1] -- Kid.X is the block's first byte
		meta.game_level_next = game.get("Level.Next")
		meta.game_tile = game.get("Room 1.Tile[10]")
		meta.game_set = tostring(game.set("Kid.X", 120))
		meta.game_kid_x_after_set = game.get("Kid.X")
		meta.game_unknown = tostring(game.get("No Such Property"))
		meta.game_igt_ms = game.get("Time.IGT Ms")
	end)
	if not ok then meta.game_error = tostring(err) end
else
	meta.game_list = "absent"
end

finish("OK", "")
