-- opz_log.lua — log every YM2414 (OPZ) register write in MAME's TX81Z driver
--
-- Usage:
--   mame tx81z -window -autoboot_script opz_log.lua [-midiin "<port>"]
--
-- Output: opz_log.csv in the directory you start MAME from
--   time_s,reg,val       (time = emulated seconds, reg/val in hex)
--
-- The script finds the OPZ device and the CPU address range it is mapped to
-- by walking the CPU's address map. If that fails, set the base address by hand:
--   OPZ_ADDR=0x2000 mame tx81z -autoboot_script opz_log.lua

local OUT = os.getenv("OPZ_LOG") or "opz_log.csv"

-- ── find devices ────────────────────────────────────────────────────────────
local opz, cpu
for tag, dev in pairs(manager.machine.devices) do
  local sn = dev.shortname or ""
  if sn:find("ym2414") then opz = dev end
  if (sn:find("6303") or sn:find("6301")) and dev.spaces and dev.spaces["program"] then cpu = dev end
end

if not cpu then
  -- fall back to the first device that has a program space
  for tag, dev in pairs(manager.machine.devices) do
    if dev.spaces and dev.spaces["program"] then cpu = dev; break end
  end
end

print(string.format("[opz_log] cpu: %s (%s)", cpu and cpu.tag or "?", cpu and cpu.shortname or "?"))
print(string.format("[opz_log] opz: %s", opz and opz.tag or "not found"))

local space = cpu.spaces["program"]

-- ── find the OPZ's address range ────────────────────────────────────────────
local base, last
local env = os.getenv("OPZ_ADDR")
if env then
  base = tonumber(env); last = base + 1
else
  print("[opz_log] program space map:")
  for _, e in ipairs(space.map.entries) do
    local w = e.write
    local wtag = (w and w.tag) or ""
    local wname = (w and w.name) or ""
    print(string.format("   %04x-%04x  write:%s %s %s",
      e.address_start, e.address_end, w and w.handlertype or "-", wtag, wname))
    if opz and (wtag == opz.tag or wtag:find("ym") or wname:find("ym")) then
      base, last = e.address_start, e.address_end
    end
  end
end

if not base then
  print("[opz_log] could not locate the OPZ in the map — read the list above and rerun with OPZ_ADDR=0x....")
  return
end
print(string.format("[opz_log] tapping writes at %04x-%04x -> %s", base, last, OUT))

-- ── tap writes ──────────────────────────────────────────────────────────────
local f = assert(io.open(OUT, "w"))
f:write("time_s,reg,val\n")
local latched = nil
local count = 0

-- keep references in globals so they are not garbage-collected
opz_tap = space:install_write_tap(base, last, "opz_log", function(offset, data, mask)
  if ((offset - base) & 1) == 0 then
    latched = data & 0xff                      -- address port
  elseif latched then
    f:write(string.format("%.6f,%02x,%02x\n", manager.machine.time:as_double(), latched, data & 0xff))
    count = count + 1
  end
end)

opz_stop = emu.add_machine_stop_notifier(function()
  f:close()
  print(string.format("[opz_log] wrote %d register writes to %s", count, OUT))
end)
