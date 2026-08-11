-- IME Conflict Fix for The Binding of Isaac: Repentance+
-- Version: 0.2.0
--
-- Pure Lua mod. Writes its config via Isaac.SaveModData() so the
-- winmm.dll proxy (in the game directory) can read it and decide whether
-- to disable the IME. Mirrors the 官中补丁 (Chinese patch) config-passing
-- mechanism: Lua -> Isaac.SaveModData -> save file -> DLL reads file.
--
-- Behavior:
--   * Mod ENABLED in-game  -> writes {"ime":"1"} -> DLL disables IME at startup
--   * Mod DISABLED/absent  -> no save file -> DLL leaves IME enabled (chat works)
--
-- The user's insight: "using a mod = not playing multiplayer" (the game
-- disables mods in online mode). So enabling the mod = single player = IME
-- should be disabled immediately.

local MOD_NAME = "IME Conflict Fix"
local MOD_VERSION = "0.2.0"
local mod = RegisterMod(MOD_NAME, 1)

-- Isaac Repentance+ runs ONLY on Windows. No package.config in the sandbox.
local IS_WINDOWS = true

-- ============================================================
-- Config persistence — the ONLY thing that matters to the DLL.
-- ============================================================

-- JSON-encode a tiny table without pulling in json libs we can't guarantee.
local function encode_simple(t)
    return '{"ime":"' .. (t.ime and "1" or "0") .. '"}'
end

local cfg = { ime = true }  -- default: IME fix ON

-- Load existing config from save data
local data_str = nil
pcall(function() data_str = Isaac.LoadModData(mod) end)
if data_str and data_str ~= "" then
    -- crude parse: look for "ime":"0" to disable, else default on
    if data_str:find('"ime"%s*:%s*"0"') then
        cfg.ime = false
    end
end

local function save()
    pcall(function()
        Isaac.SaveModData(mod, encode_simple(cfg))
    end)
end

-- ============================================================
-- Mod Config Menu Integration (optional, mirrors 官中补丁)
-- ============================================================

function mod:setupMCM()
    if not ModConfigMenu then
        return  -- MCM not installed, skip
    end

    local categoryName = "IME Conflict Fix"

    if ModConfigMenu.RemoveCategory then
        ModConfigMenu.RemoveCategory(categoryName)
    end

    -- Setting: Enable/Disable IME Fix (persisted for the DLL)
    ModConfigMenu.AddSetting(MOD_NAME, categoryName, {
        Type = ModConfigMenu.OptionType.BOOLEAN,
        CurrentSetting = function() return cfg.ime end,
        Display = function()
            return "IME Fix: " .. (cfg.ime and "ON" or "OFF")
        end,
        OnChange = function(v)
            cfg.ime = v
            save()
        end,
        Info = {"禁用中文输入法（重启游戏后生效）"}
    })

    Isaac.DebugString("[IME Conflict Fix] MCM initialized")
end

-- ============================================================
-- Callbacks
-- ============================================================

function mod:onPostUpdate()
    if not IS_WINDOWS then return end
    -- Save config once after load (so the DLL sees it even if user never
    -- opens MCM). Uses a one-shot flag.
    if not savedOnce then
        savedOnce = true
        save()
    end
end

function mod:onGameStart()
    if not IS_WINDOWS then return end
    Isaac.DebugString("[IME Conflict Fix] v" .. MOD_VERSION .. " loaded")
end

local savedOnce = false

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, mod.onGameStart)
mod:AddCallback(ModCallbacks.MC_POST_UPDATE, mod.onPostUpdate)

Isaac.DebugString("[IME Conflict Fix] Registered (v" .. MOD_VERSION .. ")")
