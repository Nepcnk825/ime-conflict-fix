-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.3.12
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
local MOD_VERSION = "0.3.12"

local mod = RegisterMod(MOD_NAME, 1)

-- ============================================================
-- In-game configuration (Mod Config Menu compatible)
-- ============================================================
local MCM_LOADED, MCM = pcall(require, "scripts.modconfig")
local JSON_LOADED, json = pcall(require, "json")

local imeConfig = {
    lock_layout = 1,
    online_chat_toggle = 0,
    online_force = 0,
}

local function loadImeConfig()
    if JSON_LOADED and json then
        local ok, data = pcall(Isaac.LoadModData, mod)
        if ok and data and data ~= "" then
            local ok2, decoded = pcall(json.decode, data)
            if ok2 and decoded then
                if decoded.lock_layout ~= nil then
                    imeConfig.lock_layout = decoded.lock_layout
                end
                if decoded.online_chat_toggle ~= nil then
                    imeConfig.online_chat_toggle = decoded.online_chat_toggle
                end
                if decoded.online_force ~= nil then
                    imeConfig.online_force = decoded.online_force
                end
            end
        end
    end
end

local function saveImeConfig()
    if JSON_LOADED and json then
        pcall(Isaac.SaveModData, mod, json.encode(imeConfig))
    end
end

local function emitLockLayoutSignal()
    local value = imeConfig.lock_layout and 1 or 0
    Isaac.DebugString("[IME_LOCK_LAYOUT:" .. value .. "]")
end

local function emitOnlineChatSignal()
    local value = imeConfig.online_chat_toggle and 1 or 0
    Isaac.DebugString("[IME_ONLINE_CHAT:" .. value .. "]")
end

local function emitOnlineForceSignal()
    local value = imeConfig.online_force and 1 or 0
    Isaac.DebugString("[IME_ONLINE_FORCE:" .. value .. "]")
end

loadImeConfig()

local IDLE_FRAMES = 60 * 3   -- 3 seconds at 60 fps
local PROBE_FRAMES = 60      -- 1 second probe: proves the render callback + Input API work
local idleFrames = 0
local signaled = false
local probeSent = false
local lastMouseX = nil
local lastMouseY = nil

-- Only values that exist in this API version. ACTION_CONFIRM/ACTION_CANCEL do
-- not exist (the real names are ACTION_MENUCONFIRM/ACTION_MENUBACK); using
-- them previously made the render callback error out.
local IDLE_ACTIONS = {
    ButtonAction.ACTION_LEFT,
    ButtonAction.ACTION_RIGHT,
    ButtonAction.ACTION_UP,
    ButtonAction.ACTION_DOWN,
    ButtonAction.ACTION_SHOOTLEFT,
    ButtonAction.ACTION_SHOOTRIGHT,
    ButtonAction.ACTION_SHOOTUP,
    ButtonAction.ACTION_SHOOTDOWN,
    ButtonAction.ACTION_BOMB,
    ButtonAction.ACTION_ITEM,
    ButtonAction.ACTION_PILLCARD,
    ButtonAction.ACTION_DROP,
    ButtonAction.ACTION_PAUSE,
    ButtonAction.ACTION_MAP,
    ButtonAction.ACTION_MENUCONFIRM,
    ButtonAction.ACTION_MENUBACK,
    ButtonAction.ACTION_RESTART,
    ButtonAction.ACTION_FULLSCREEN,
    ButtonAction.ACTION_MUTE,
    ButtonAction.ACTION_JOINMULTIPLAYER,
    ButtonAction.ACTION_MENULEFT,
    ButtonAction.ACTION_MENURIGHT,
    ButtonAction.ACTION_MENUUP,
    ButtonAction.ACTION_MENUDOWN,
    ButtonAction.ACTION_MENULT,
    ButtonAction.ACTION_MENURT,
    ButtonAction.ACTION_MENUTAB,
}

local function isActionActive()
    for i = 1, #IDLE_ACTIONS do
        if Input.IsActionPressed(IDLE_ACTIONS[i], 0) then
            return true
        end
    end
    return false
end

local function isMouseActive()
    if Input.IsMouseBtnPressed(Mouse.MOUSE_BUTTON_LEFT)
        or Input.IsMouseBtnPressed(Mouse.MOUSE_BUTTON_RIGHT)
        or Input.IsMouseBtnPressed(Mouse.MOUSE_BUTTON_MIDDLE) then
        return true
    end

    local pos = Input.GetMousePosition(false)
    if pos then
        local x = pos.X
        local y = pos.Y
        if lastMouseX ~= nil and (x ~= lastMouseX or y ~= lastMouseY) then
            lastMouseX = x
            lastMouseY = y
            return true
        end
        lastMouseX = x
        lastMouseY = y
    end
    return false
end

function mod:onGameStart()
    Isaac.DebugString("[IME_RUN_STARTED]")
end

function mod:onRender()
    if signaled then
        return
    end
    if isActionActive() or isMouseActive() then
        idleFrames = 0
        probeSent = false
    else
        idleFrames = idleFrames + 1
        if not probeSent and idleFrames >= PROBE_FRAMES then
            Isaac.DebugString("[IME_IDLE_PROBE_1S]")
            probeSent = true
        end
        if idleFrames >= IDLE_FRAMES then
            Isaac.DebugString("[IME_IDLE]")
            signaled = true
        end
    end
end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_RENDER, mod.onRender)

-- Register with Mod Config Menu if installed.
if MCM_LOADED and MCM and ModConfigMenu then
    local zh = (MCM.i18n == "Chinese")

    MCM.AddSetting(
        "IME Conflict Fix",
        "Input",
        {
            Type = ModConfigMenu.OptionType.BOOLEAN,
            CurrentSetting = function()
                return imeConfig.lock_layout
            end,
            Display = function()
                if zh then
                    return "锁定英文布局:" .. (imeConfig.lock_layout and "开" or "关")
                end
                return "Lock English layout: " .. (imeConfig.lock_layout and "On" or "Off")
            end,
            OnChange = function(value)
                imeConfig.lock_layout = value
                saveImeConfig()
                emitLockLayoutSignal()
            end,
            Info = zh and {"游戏前台时自动保持在英文键盘布局。"}
                       or {"Keeps the English keyboard layout active while the game is focused."},
        }
    )

    MCM.AddSetting(
        "IME Conflict Fix",
        "Input",
        {
            Type = ModConfigMenu.OptionType.BOOLEAN,
            CurrentSetting = function()
                return imeConfig.online_force
            end,
            Display = function()
                if zh then
                    return "联机禁用Mod也生效:" .. (imeConfig.online_force and "开" or "关")
                end
                return "Apply while mods disabled online: " .. (imeConfig.online_force and "On" or "Off")
            end,
            OnChange = function(value)
                imeConfig.online_force = value
                saveImeConfig()
                emitOnlineForceSignal()
            end,
            Info = zh and {"开启后，即使游戏禁用 Mod，联机时 DLL 仍会保持英文布局。"}
                       or {"Keeps English layout online even when the game disables mods."},
        }
    )

    MCM.AddSetting(
        "IME Conflict Fix",
        "Input",
        {
            Type = ModConfigMenu.OptionType.BOOLEAN,
            CurrentSetting = function()
                return imeConfig.online_chat_toggle
            end,
            Display = function()
                if zh then
                    return "联机聊天切换(实验性):" .. (imeConfig.online_chat_toggle and "开" or "关")
                end
                return "Online chat toggle (experimental): " .. (imeConfig.online_chat_toggle and "On" or "Off")
            end,
            OnChange = function(value)
                imeConfig.online_chat_toggle = value
                saveImeConfig()
                emitOnlineChatSignal()
            end,
            Info = zh and {"实验性：第一次 Enter 切中文，第二次 Enter 切回英文；无超时。效果不稳定。"}
                       or {"Experimental: first Enter switches to Chinese, second Enter back to English. No timeout. Unstable."},
        }
    )
end

emitLockLayoutSignal()
emitOnlineChatSignal()
emitOnlineForceSignal()
Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
