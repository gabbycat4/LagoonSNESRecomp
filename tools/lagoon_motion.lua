-- Lagoon motion log for Mesen 2 (SNES). gabbycat4 fork, Step 2.
-- Writes the same numbers as the port's LAGOON_MOTION_LOG (frame, game tick,
-- room, camera, player) so the original game's step rhythm can be compared
-- with the port's. 20 seconds, then it stops by itself.
--
-- Mesen: Debug -> Script Window -> File -> Open this file -> Run.
-- If the script window asks for file access, allow it (Script -> Settings ->
-- "Allow access to I/O and OS functions"). Without it, lines go to the log
-- window instead (it only keeps the last ~500).

local SECONDS = 20
local LIMIT = SECONDS * 60
local home = os and os.getenv and os.getenv("HOME") or "."
local path = home .. "/Projects/LagoonSNESRecomp/mesen_motion.log"
local out = io and io.open and io.open(path, "w")

local function emit(s)
  if out then out:write(s, "\n") else emu.log(s) end
end

-- Work RAM: prefer the WRAM memory type; fall back to the CPU bus at $7E:xxxx.
local mt, base = emu.memType.snesWorkRam, 0
if mt == nil then mt, base = emu.memType.snesMemory, 0x7E0000 end
local function b(a) return emu.read(base + a, mt) end
local function w(a) return b(a) + 256 * b(a + 1) end

emit("# lagoon mesen motion log v1: frame t=<tick $0408> r=<room> c=<$0480>,<$0482>,<$0484>,<$0486> p=<$0502>,<$0504> (hex)")
emit("# memType=" .. (emu.memType.snesWorkRam and "snesWorkRam" or "snesMemory") .. " file=" .. (out and path or "log window"))

local frame = 0
local function onFrame()
  if frame >= LIMIT then return end
  frame = frame + 1
  emit(string.format("F %d t=%x r=%x c=%x,%x,%x,%x p=%x,%x", frame, b(0x408), w(0x48E),
    w(0x480), w(0x482), w(0x484), w(0x486), w(0x502), w(0x504)))
  if frame == LIMIT then
    if out then out:close() end
    emu.displayMessage("Lagoon", "Motion log done (" .. SECONDS .. " s)")
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
emu.displayMessage("Lagoon", "Recording motion for " .. SECONDS .. " seconds: walk now")
