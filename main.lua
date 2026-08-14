-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.3.4
--
-- Signal mod:
--   1. Execution marker: "Running Lua Script: .../main.lua" (Phase 1 startup).
--   2. "[IME_RUN_STARTED]" on MC_POST_GAME_STARTED (freeze-free run start).
--   3. "[IME_IDLE]" when the player has NO input for 3 seconds. ImmDisableIME
--      freezes the UI when called while the game is changing (menu init,
--      popups, page switches - verified by stress tests). A player who is
--      actively pressing keys never reaches the idle state, so no freeze is
--      possible; a player who stops for 3s gives us a quiescent moment.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.3.4"

local mod = RegisterMod(MOD_NAME, 1)

local IDLE_FRAMES = 60 * 3   -- 3 seconds at 60 fps
local idleFrames = 0
local signaled = false

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

function mod:onRender()
    if signaled then
        return
    end
    if Input.IsActionPressed(ButtonAction.ACTION_LEFT)
        or Input.IsActionPressed(ButtonAction.ACTION_RIGHT)
        or Input.IsActionPressed(ButtonAction.ACTION_UP)
        or Input.IsActionPressed(ButtonAction.ACTION_DOWN)
        or Input.IsActionPressed(ButtonAction.ACTION_CONFIRM)
        or Input.IsActionPressed(ButtonAction.ACTION_CANCEL)
        or Input.IsActionPressed(ButtonAction.ACTION_PAUSE)
        or Input.IsActionPressed(ButtonAction.ACTION_RESTART) then
        idleFrames = 0
    else
        idleFrames = idleFrames + 1
        if idleFrames == IDLE_FRAMES then
            Isaac.DebugString("[IME_IDLE]")
            signaled = true
        end
    end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.onRender)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
