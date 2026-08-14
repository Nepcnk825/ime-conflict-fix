-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.3.3
--
-- Signal mod:
--   1. Execution marker: "Running Lua Script: .../main.lua" (Phase 1 startup).
--   2. "[IME_RUN_STARTED]" on MC_POST_GAME_STARTED (freeze-free run start).
--   3. "[IME_FREEZE]" + a busy-wait freeze window: after this mod has been
--      (re)loaded, we freeze the game main thread for FREEZE_MS so that NO
--      input is processed. ImmDisableIME is only safe when the game is not
--      processing input/modal UI (verified by stress tests - calling it
--      during menu init/popups/page switches freezes the UI). The DLL polls
--      for "[IME_FREEZE]" and disables inside this controlled window.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.3.1"

local mod = RegisterMod(MOD_NAME, 1)

local FREEZE_DELAY_FRAMES = 60     -- 1s after load, signal the DLL
local frameCount = 0
local signaled = false

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

function mod:onRender()
    if signaled then
        return
    end
    frameCount = frameCount + 1
    if frameCount == FREEZE_DELAY_FRAMES then
        -- Tell the DLL we are (re)loaded and the menu is settling. The DLL
        -- suspends the game main thread itself (no Lua timing API needed -
        -- os/io are disabled in the sandbox), disables the IME inside that
        -- controlled window and resumes. Player-visible freeze: ~100ms.
        Isaac.DebugString("[IME_FREEZE]")
        signaled = true
    end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.onRender)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")