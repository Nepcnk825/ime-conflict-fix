-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.2.0
--
-- Minimal Lua mod. Its ONLY job is to be detectable: when this mod is
-- enabled, the game executes main.lua and logs
--   "Running Lua Script: .../mods/ime-conflict-fix/main.lua"
-- in log.txt. The injected ime_fix.dll scans that marker at startup and
-- disables the IME (ImmDisableIME(-1), irreversible).
--
-- Behavior (controlled entirely by the game's mod on/off switch):
--   * Mod ENABLED  -> game runs main.lua -> DLL disables IME at startup
--   * Mod DISABLED -> game skips main.lua -> DLL leaves IME enabled
--
-- No config persistence, no MCM, no callbacks needed.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.2.0"

RegisterMod(MOD_NAME, 1)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
