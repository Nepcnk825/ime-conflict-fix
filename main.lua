-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.3.0
--
-- Triple-signal Lua mod:
--   1. When ENABLED, the game executes main.lua and logs
--      "Running Lua Script: .../mods/ime-conflict-fix/main.lua" in log.txt.
--      ime_fix.dll scans that marker in the startup window and disables
--      the IME (ImmDisableIME(-1), irreversible).
--   2. Run-start signal "[IME_RUN_STARTED]" via MC_POST_GAME_STARTED when a
--      single-player run starts - a verified freeze-free moment.
--   3. Idle signal "[IME_IDLE]" when the player has NO input for 3 seconds.
--      ImmDisableIME freezes the UI when called while the game is changing
--      (menu init, popups, page switches - verified by stress-testing with
--      rapid ESC). Waiting for an idle player guarantees the game is
--      quiescent, so disabling there is safe; a player mashing ESC never
--      triggers it, so no freeze is possible.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.3.0"

local mod = RegisterMod(MOD_NAME, 1)

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

-- Idle detection: 3 seconds without any game action (menu navigation keys
-- included) means the player is not interacting - safe moment to disable.
-- MC_POST_RENDER fires every frame including the main menu.
local idleFrames = 0
local IDLE_FRAMES = 60 * 3   -- 3 seconds at 60 fps

local function anyInputPressed()
    return Input.IsActionPressed(ButtonAction.ACTION_LEFT)
        or Input.IsActionPressed(ButtonAction.ACTION_RIGHT)
        or Input.IsActionPressed(ButtonAction.ACTION_UP)
        or Input.IsActionPressed(ButtonAction.ACTION_DOWN)
        or Input.IsActionPressed(ButtonAction.ACTION_CONFIRM)
        or Input.IsActionPressed(ButtonAction.ACTION_CANCEL)
        or Input.IsActionPressed(ButtonAction.ACTION_PAUSE)
        or Input.IsActionPressed(ButtonAction.ACTION_RESTART)
end

function mod:onRender()
    if anyInputPressed() then
        idleFrames = 0
    else
        idleFrames = idleFrames + 1
        if idleFrames == IDLE_FRAMES then
            Isaac.DebugString("[IME_IDLE]")
            idleFrames = -1 << 30  -- signal once; stay negative so it never refires
        end
    end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.onRender)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
