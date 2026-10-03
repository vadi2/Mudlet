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

function ST.load()
  local state = defaults()
  local path = ST.dataFile()
  if io.exists(path) then
    local loaded = {}
    local ok, err = pcall(table.load, path, loaded)
    if ok and type(loaded) == "table" then
      for key, value in pairs(loaded) do
        if type(value) == type(state[key]) then
          state[key] = value
        end
      end
      for name, enabled in pairs(defaults().views) do
        if state.views[name] == nil then
          state.views[name] = enabled
        end
      end
    else
      ST.warn("could not read saved data (" .. tostring(err) .. "), starting fresh")
      ST.finish("main")
    end
  end
  ST.state = state
end

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
  table.save(ST.dataFile(), ST.state)
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

-- Lowercases ASCII and the Swedish letters, independent of the system locale.
function ST.lower(s)
  s = s:lower()
  return (s:gsub("Å", "å"):gsub("Ä", "ä"):gsub("Ö", "ö"):gsub("É", "é"):gsub("Ü", "ü"))
end

-- Number of characters on screen, counting UTF-8 sequences as one.
function ST.width(s)
  return select(2, s:gsub("[^\128-\191]", ""))
end

function ST.pad(s, width)
  return s .. string.rep(" ", width - ST.width(s))
end

-- Cuts at a character boundary rather than in the middle of a multi-byte letter.
function ST.truncate(s, width)
  if ST.width(s) <= width then
    return s
  end
  local out, count = {}, 0
  for char in s:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
    count = count + 1
    if count >= width then
      break
    end
    out[#out + 1] = char
  end
  return table.concat(out) .. "…"
end

function ST.urlencode(s)
  return (s:gsub("[^%w%-%._~]", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = " " }

function ST.decodeEntities(s)
  s = s:gsub("&#[xX](%x+);", function(hex) return utf8.char(tonumber(hex, 16)) end)
  s = s:gsub("&#(%d+);", function(dec) return utf8.char(tonumber(dec)) end)
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
