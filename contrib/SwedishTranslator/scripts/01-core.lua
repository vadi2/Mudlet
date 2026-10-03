-- Swedish -> English translator for language learners.
-- State, persistence and small text helpers shared by the other scripts.
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

ST.version = "1.0.0"
ST.handlerUser = "SwedishTranslator"

ST.viewOrder = { "inline", "gloss", "hover", "reveal", "panel", "subtitle" }
ST.viewInfo = {
  inline = "translation printed right under the Swedish text",
  gloss = "word-by-word interlinear gloss: Swedish on top, English below",
  hover = "each Swedish word is a link - hover for English, click to save it",
  reveal = "translation hidden behind a click, for active-recall practice",
  panel = "dockable side window with a running Swedish/English history",
  subtitle = "movie-style subtitle card floating over the main window",
}

local MAX_HISTORY = 200
local MAX_CACHE = 3000

local function defaults()
  return {
    views = { inline = true, gloss = true, hover = false, reveal = false, panel = false, subtitle = false },
    auto = false,
    email = "",
    cache = {},   -- [lowercased swedish] = { en = english, t = os.time() }
    vocab = {},   -- [lowercased word] = { en = english, box = 1..5, right = n, wrong = n, added = os.time() }
    history = {}, -- { { sv = swedish, en = english, t = os.time() } }, oldest first
  }
end

function ST.dataFile()
  return getMudletHomeDir() .. "/SwedishTranslator.data.lua"
end

-- Reads a saved state file; returns the table, or nil and the reason it failed.
local function readStateFile(path)
  if not io.exists(path) then
    return nil
  end
  local loaded = {}
  local ok, err = pcall(table.load, path, loaded)
  if not ok then
    return nil, tostring(err)
  end
  return loaded
end

-- Copies over only well-formed values, so a hand-edited or partly written file
-- cannot leave entries behind that would break the code reading them later.
local function adopt(state, loaded)
  if type(loaded.views) == "table" then
    for name in pairs(state.views) do
      if type(loaded.views[name]) == "boolean" then
        state.views[name] = loaded.views[name]
      end
    end
  end
  if type(loaded.auto) == "boolean" then
    state.auto = loaded.auto
  end
  if type(loaded.email) == "string" then
    state.email = loaded.email
  end
  local dropped = 0
  -- On disk the cache and word list are lists of { key = ..., ... } (see toDisk).
  for key, entry in pairs(type(loaded.cache) == "table" and loaded.cache or {}) do
    key = type(entry) == "table" and entry.key or key
    if type(key) == "string" and type(entry) == "table" and type(entry.en) == "string" then
      state.cache[key] = { en = entry.en, t = tonumber(entry.t) or 0 }
    else
      dropped = dropped + 1
    end
  end
  for key, word in pairs(type(loaded.vocab) == "table" and loaded.vocab or {}) do
    key = type(word) == "table" and word.key or key
    if type(key) == "string" and type(word) == "table" and type(word.en) == "string" then
      state.vocab[key] = {
        en = word.en,
        box = math.min(5, math.max(1, math.floor(tonumber(word.box) or 1))),
        right = tonumber(word.right) or 0,
        wrong = tonumber(word.wrong) or 0,
        added = tonumber(word.added) or os.time(),
      }
    else
      dropped = dropped + 1
    end
  end
  for _, entry in ipairs(type(loaded.history) == "table" and loaded.history or {}) do
    if type(entry) == "table" and type(entry.sv) == "string" and type(entry.en) == "string" then
      state.history[#state.history + 1] = { sv = entry.sv, en = entry.en, t = tonumber(entry.t) or 0 }
    else
      dropped = dropped + 1
    end
  end
  return dropped
end

function ST.load()
  local state = defaults()
  local path = ST.dataFile()
  local loaded, err = readStateFile(path)
  if not loaded and not err and io.exists(path .. ".bak") then
    -- Mudlet stopped between the two renames in ST.save; the backup is the latest save.
    loaded, err = readStateFile(path .. ".bak")
    if err then
      ST.warn("your saved data could not be read (" .. err .. "); starting with an empty word list")
      ST.finish("main")
    end
  elseif err then
    -- Keep the unreadable file rather than letting the next save overwrite it,
    -- and fall back to the copy of the previous save.
    local aside = path .. ".unreadable-" .. os.date("%Y%m%d-%H%M%S")
    os.rename(path, aside)
    loaded = readStateFile(path .. ".bak")
    ST.warn(string.format("your saved data could not be read (%s). It was kept as %s; %s", err, aside,
      loaded and "restored the previous save instead." or "starting with an empty word list."))
    ST.finish("main")
  end
  if loaded then
    local dropped = adopt(state, loaded)
    if dropped > 0 then
      ST.warn(string.format("ignored %d damaged entries in your saved data", dropped))
      ST.finish("main")
    end
  end
  ST.state = state
end

-- table.save silently skips keys named after standard libraries ("os", "io",
-- "string", ...), and "os" is a Swedish word, so words are values on disk, never keys.
local function toDisk(state)
  local disk = { views = state.views, auto = state.auto, email = state.email, history = state.history, cache = {}, vocab = {} }
  for key, entry in pairs(state.cache) do
    disk.cache[#disk.cache + 1] = { key = key, en = entry.en, t = entry.t }
  end
  for key, word in pairs(state.vocab) do
    disk.vocab[#disk.vocab + 1] = { key = key, en = word.en, box = word.box, right = word.right, wrong = word.wrong, added = word.added }
  end
  return disk
end

-- Writes to a temporary file first and keeps the previous save as .bak, so a
-- crash or full disk mid-write never destroys the word list.
function ST.save()
  if ST.saveTimer then
    killTimer(ST.saveTimer)
    ST.saveTimer = nil
  end
  if not ST.state then
    return
  end
  ST.pruneCache()
  while #ST.state.history > MAX_HISTORY do
    table.remove(ST.state.history, 1)
  end
  local path = ST.dataFile()
  local temporary = path .. ".tmp"
  local _, err = table.save(temporary, toDisk(ST.state))
  if not err and not loadfile(temporary) then
    err = "the written file is incomplete"
  end
  if not err then
    -- Rotate only when there is a save to keep; otherwise the .bak is the only copy.
    if io.exists(path) then
      os.remove(path .. ".bak")
      os.rename(path, path .. ".bak")
    end
    local renamed, renameErr = os.rename(temporary, path)
    if not renamed then
      err = renameErr
    end
  end
  if err then
    os.remove(temporary)
    -- Once per problem, not on every autosave.
    if ST.lastSaveError ~= err then
      ST.lastSaveError = err
      ST.warn(string.format("could not save your data to %s (%s) - changes will be lost when Mudlet closes", path, tostring(err)))
      ST.finish("main")
    end
  else
    ST.lastSaveError = nil
  end
end

-- Coalesce bursts of changes (e.g. auto-translating a screenful of text) into one write.
function ST.scheduleSave()
  if ST.saveTimer then
    return
  end
  ST.saveTimer = tempTimer(5, function()
    ST.saveTimer = nil
    ST.save()
  end)
end

function ST.pruneCache()
  local entries = {}
  for key, entry in pairs(ST.state.cache) do
    entries[#entries + 1] = { key = key, t = entry.t or 0 }
  end
  if #entries <= MAX_CACHE then
    return
  end
  table.sort(entries, function(a, b) return a.t > b.t end)
  for i = MAX_CACHE + 1, #entries do
    ST.state.cache[entries[i].key] = nil
  end
end

-- Text helpers --------------------------------------------------------------

function ST.trim(s)
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

function ST.lower(s)
  return utf8.lower(s)
end

-- Number of characters on screen, counting UTF-8 sequences as one.
function ST.width(s)
  return utf8.len(s) or #s
end

function ST.pad(s, width)
  return s .. string.rep(" ", width - ST.width(s))
end

function ST.truncate(s, width)
  if ST.width(s) <= width then
    return s
  end
  return utf8.sub(s, 1, width - 1) .. "…"
end

function ST.urlencode(s)
  return (s:gsub("[^%w%-%._~]", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = " " }

-- Out-of-range numbers (which crowd-sourced replies can contain) are left as typed.
local function codepoint(n)
  if n and n <= 0x10FFFF and not (n >= 0xD800 and n <= 0xDFFF) then
    return utf8.char(n)
  end
  return nil
end

function ST.decodeEntities(s)
  s = s:gsub("&#[xX](%x+);", function(hex) return codepoint(tonumber(hex, 16)) end)
  s = s:gsub("&#(%d+);", function(dec) return codepoint(tonumber(dec)) end)
  return (s:gsub("&(%a+);", function(name) return ENTITIES[name] end))
end

function ST.htmlEscape(s)
  return (s:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"))
end

local EDGE_PUNCTUATION = { "«", "»", "“", "”", "„", "–", "—", "…", "’", "‘", "¡", "¿" }

local function stripEdges(s)
  local previous
  repeat
    previous = s
    s = s:gsub("^[%p%s]+", ""):gsub("[%p%s]+$", "")
    for _, mark in ipairs(EDGE_PUNCTUATION) do
      if s:sub(1, #mark) == mark then
        s = s:sub(#mark + 1)
      end
      if #s >= #mark and s:sub(-#mark) == mark then
        s = s:sub(1, -#mark - 1)
      end
    end
  until s == previous
  return s
end

-- Splits text into display tokens. Each token keeps its original spelling (with
-- punctuation) for display, plus the bare lowercased word used for lookups;
-- `key` is nil for tokens that hold no letters, like numbers or dashes.
function ST.tokenize(text)
  local tokens = {}
  for raw in text:gmatch("%S+") do
    local bare = stripEdges(raw)
    local key = nil
    if bare:find("[%a\128-\255]") then
      key = ST.lower(bare)
    end
    tokens[#tokens + 1] = { text = raw, word = bare, key = key }
  end
  return tokens
end

-- Output helpers ------------------------------------------------------------

-- True when the window's last line is empty, so the next echo starts a fresh line.
function ST.atLineStart(window)
  local last = getLastLineNumber(window)
  local lines = getLines(window, last, last + 1)
  return type(lines) ~= "table" or (lines[1] or "") == ""
end

-- Echoes { {color, text}, ... } without interpreting any markup in the text, so
-- game or user text containing "<" or "#" can never be mistaken for color codes.
-- A leading "\n" starts a new line, and is dropped when already at a line start
-- so output never leaves stray blank lines.
function ST.out(window, segments)
  window = window or "main"
  for index, segment in ipairs(segments) do
    local text = segment[2]
    if index == 1 and text:sub(1, 1) == "\n" and ST.atLineStart(window) then
      text = text:sub(2)
    end
    fg(window, segment[1])
    echo(window, text)
  end
  resetFormat(window)
end

-- Ends the current line, so whatever is echoed next (game text, or the command
-- line's echo of the next command) starts on a line of its own instead of being
-- glued onto the end of ours.
function ST.finish(window)
  window = window or "main"
  if not ST.atLineStart(window) then
    echo(window, "\n")
  end
end

function ST.tag(window)
  ST.out(window, { { "dim_gray", "\n[" }, { "gold", "sv" }, { "dim_gray", "] " } })
end

function ST.msg(text, color)
  ST.tag("main")
  ST.out("main", { { color or "light_gray", text } })
end

function ST.warn(text)
  ST.msg(text, "tomato")
end

function ST.link(window, text, action, hint)
  echoLink(window, text, function()
    action()
    ST.finish("main")
  end, hint, false)
end
