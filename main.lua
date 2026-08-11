-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.2.0
--
-- Dual-signal Lua mod:
--   1. When ENABLED, the game executes main.lua and logs
--      "Running Lua Script: .../mods/ime-conflict-fix/main.lua" in log.txt.
--      ime_fix.dll scans that marker in the startup window and disables
--      the IME (ImmDisableIME(-1), irreversible).
--   2. If the mod was enabled mid-session (mod disabled at launch, then
--      toggled on in-game), the startup window has already passed. This
--      mod then writes "[IME_RUN_STARTED]" via MC_POST_GAME_STARTED when a
--      single-player run actually starts; the DLL listens for that marker
--      and disables the IME at that safe moment (game fully loaded, no UI
--      freeze). Online play disables mods, so this marker only fires for
--      single-player runs.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.2.0"

local mod = RegisterMod(MOD_NAME, 1)

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
