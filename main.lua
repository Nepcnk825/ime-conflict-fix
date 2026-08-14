-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.3.2
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

local FREEZE_DELAY_FRAMES = 60     -- 1s after load (reload/menu settle)
local FREEZE_MS = 600              -- controlled freeze window (minimized: DLL detects in ~200ms, calls in ~100ms)
local frameCount = 0
local frozen = false

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

function mod:onRender()
    if frozen then
        return
    end
    frameCount = frameCount + 1
    if frameCount == FREEZE_DELAY_FRAMES then
        Isaac.DebugString("[IME_FREEZE]")
        -- Busy-wait on the game main thread: no input is processed during
        -- this window, giving the DLL a guaranteed-safe moment to disable
        -- the IME.
        local t0 = os.clock()
        while os.clock() - t0 < FREEZE_MS / 1000.0 do
            -- spin
        end
        frozen = true
    end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.onRender)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")