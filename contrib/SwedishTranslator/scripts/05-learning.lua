-- Learning aids: history, a personal word list with a spaced-repetition quiz,
-- auto-translation of Swedish game text, and right-click translation of a selection.
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

local MAX_BOX = 5

function ST.addHistory(sv, en)
  local history = ST.state.history
  local last = history[#history]
  if last and last.sv == sv then
    return
  end
  history[#history + 1] = { sv = sv, en = en, t = os.time() }
  ST.scheduleSave()
end

function ST.showHistory(count)
  local history = ST.state.history
  if #history == 0 then
    ST.msg("nothing translated yet - try: sv Hej, hur mår du?")
    return
  end
  count = math.min(math.max(1, math.floor(count or 10)), #history)
  ST.msg(string.format("your last %d translations:", count), "gold")
  for i = #history - count + 1, #history do
    local entry = history[i]
    ST.out("main", {
      { "dim_gray", "\n  " .. os.date("%d %b %H:%M", entry.t) .. "  " },
      { "light_goldenrod", entry.sv },
      { "dim_gray", "  →  " },
      { "pale_green", entry.en },
    })
  end
end

-- Word list ------------------------------------------------------------------

function ST.saveWord(key, english)
  key = ST.lower(ST.trim(key))
  if key == "" then
    ST.warn("which word? e.g. sv:save hund")
    return
  end
  if not english then
    ST.lookupWord(key, function(ok, result)
      if ok then
        ST.saveWord(key, result)
      else
        ST.warn(string.format("could not look up '%s': %s", key, result))
      end
      ST.finish("main")
    end)
    return
  end
  local vocab = ST.state.vocab
  if vocab[key] then
    vocab[key].en = english
    ST.msg(string.format("'%s' (%s) is already in your word list", key, english))
  else
    vocab[key] = { en = english, box = 1, right = 0, wrong = 0, added = os.time() }
    local count = table.size(vocab)
    ST.msg(string.format("saved '%s' = %s  (%d %s in your list)", key, english, count, count == 1 and "word" or "words"), "pale_green")
  end
  ST.scheduleSave()
end

function ST.forgetWord(key)
  key = ST.lower(ST.trim(key))
  if not ST.state.vocab[key] then
    ST.warn(string.format("'%s' is not in your word list", key))
    return
  end
  ST.state.vocab[key] = nil
  ST.msg(string.format("removed '%s' from your word list", key))
  ST.scheduleSave()
end

local function boxMeter(box)
  return string.rep("●", box) .. string.rep("○", MAX_BOX - box)
end

function ST.showWords()
  local keys = table.keys(ST.state.vocab)
  if #keys == 0 then
    ST.msg("your word list is empty - click words in the hover view, or use sv:save <word>")
    return
  end
  table.sort(keys)
  local width = 0
  for _, key in ipairs(keys) do
    width = math.max(width, ST.width(key))
  end
  ST.msg(string.format("your word list (%d words; ● = how well you know it):", #keys), "gold")
  for _, key in ipairs(keys) do
    local word = ST.state.vocab[key]
    ST.out("main", {
      { "dim_gray", "\n  " },
      { "light_goldenrod", ST.pad(key, width) },
      { "dim_gray", "  " .. boxMeter(word.box) .. "  " },
      { "pale_green", word.en },
      { "dim_gray", "  " },
    })
    ST.link("main", "[forget]", function() ST.forgetWord(key) end, "Remove '" .. key .. "' from your word list")
  end
  ST.out("main", { { "dim_gray", "\n  " } })
  ST.link("main", "[start a quiz]", function() ST.quiz() end, "Practice these words")
end

-- Picks a word, favouring the ones in low boxes (not yet known well).
local function pickQuizWord(avoid)
  local total, pool = 0, {}
  for key, word in pairs(ST.state.vocab) do
    if key ~= avoid or table.size(ST.state.vocab) == 1 then
      local weight = (MAX_BOX + 1 - word.box) ^ 2
      total = total + weight
      pool[#pool + 1] = { key = key, weight = weight }
    end
  end
  local roll = math.random() * total
  for _, candidate in ipairs(pool) do
    roll = roll - candidate.weight
    if roll <= 0 then
      return candidate.key
    end
  end
  return pool[#pool] and pool[#pool].key
end

function ST.quiz()
  if table.size(ST.state.vocab) == 0 then
    ST.msg("save a few words first (click them in the hover view, or sv:save <word>), then quiz yourself")
    return
  end
  local key = pickQuizWord(ST.lastQuizWord)
  ST.lastQuizWord = key
  local word = ST.state.vocab[key]
  local answered = false

  local function grade(knewIt)
    if answered then
      return
    end
    answered = true
    if knewIt then
      word.right = word.right + 1
      word.box = math.min(MAX_BOX, word.box + 1)
    else
      word.wrong = word.wrong + 1
      word.box = 1
    end
    ST.scheduleSave()
    ST.out("main", { { "dim_gray", "\n  " .. boxMeter(word.box) .. "  " } })
    ST.link("main", "[next word ▶]", function() ST.quiz() end, "Another word")
    ST.out("main", { { "dim_gray", "  " } })
    ST.link("main", "[stop]", function() ST.msg("quiz over - lycka till!") end, "End the quiz")
  end

  ST.msg("quiz: what does ", "gold")
  ST.out("main", { { "white", key }, { "gold", " mean?  " } })
  ST.link("main", "[show answer]", function()
    ST.out("main", { { "dim_gray", "\n  → " }, { "pale_green", word.en }, { "dim_gray", "   " } })
    ST.link("main", "[✓ I knew it]", function() grade(true) end, "Move it up a box: you will see it less often")
    ST.out("main", { { "dim_gray", "  " } })
    ST.link("main", "[✗ I didn't]", function() grade(false) end, "Back to box 1: you will see it more often")
  end, "Say the answer out loud first!")
end

-- Auto-translate ---------------------------------------------------------------

-- Short, frequent Swedish words that are rare in English (and in French or
-- Spanish), so seeing them is good evidence a line is Swedish. Words that are
-- common in those languages too - "till", "den", "var", "de", "en", "du" - are
-- deliberately left out. This is a heuristic: it mostly leaves English alone.
local MARKERS = {}
for _, word in ipairs({
  "och", "är", "att", "det", "jag", "inte", "på", "som", "med", "för", "har", "av",
  "kan", "så", "här", "där", "vad", "hur", "eller", "ett", "hon", "sig", "mig", "dig",
  "från", "efter", "också", "bara", "skulle", "finns", "kommer", "går", "hej", "tack",
  "nej", "vill", "ska", "inga", "något", "någon", "mycket", "nu", "vi", "ni",
}) do
  MARKERS[word] = true
end

function ST.looksSwedish(line)
  if #line < 3 or #line > 450 then
    return false
  end
  local words, score = 0, 0
  for _, token in ipairs(ST.tokenize(line)) do
    if token.key then
      words = words + 1
      -- Plain finds: a pattern class like "[åäö]" would match single bytes of
      -- any multi-byte character (é, ü, ♥, ...), not these letters.
      local key = token.key
      if MARKERS[key] or key:find("å", 1, true) or key:find("ä", 1, true) or key:find("ö", 1, true) then
        score = score + 1
      end
    end
  end
  return score >= 2 or (score >= 1 and words <= 2)
end

ST.autoTrigger = "SwedishTranslator auto-translate"

-- The trigger matches every line, so it is only switched on while auto-translate is.
function ST.applyAutoTrigger()
  if ST.state.auto then
    enableTrigger(ST.autoTrigger)
  else
    disableTrigger(ST.autoTrigger)
  end
end

function ST.onGameLine(text)
  if ST.state.auto and ST.looksSwedish(text) then
    ST.process(text, "auto")
  end
end

-- Right-click a selection -------------------------------------------------------

ST.mouseEvent = "SwedishTranslator.translateSelection"

-- Mudlet hands over the selection as buffer coordinates (0-based columns,
-- inclusive at both ends); rebuild the selected text from the console's lines.
function ST.onSelection(_, _, console, startColumn, startLine, endColumn, endLine)
  if startLine > endLine or (startLine == endLine and startColumn > endColumn) then
    startColumn, startLine, endColumn, endLine = endColumn, endLine, startColumn, startLine
  end
  local lines = getLines(console, startLine, endLine + 1)
  if type(lines) ~= "table" or #lines == 0 then
    ST.warn("select some Swedish text first, then right-click it")
    ST.finish("main")
    return
  end
  lines[#lines] = utf8.sub(lines[#lines], 1, endColumn + 1)
  lines[1] = utf8.sub(lines[1], startColumn + 1)
  local text = ST.trim(table.concat(lines, " "))
  if text == "" then
    ST.warn("select some Swedish text first, then right-click it")
    ST.finish("main")
    return
  end
  ST.process(text, "selection")
end

function ST.registerMouseEvent()
  if getMouseEvents()[ST.mouseEvent] == nil then
    addMouseEvent(ST.mouseEvent, ST.mouseEvent, "Translate Swedish → English", "Translate the selected Swedish text")
  end
end
