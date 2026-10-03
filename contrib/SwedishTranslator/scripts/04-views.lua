-- The visualizations. Each view takes an entry
--   { sv = swedish, en = english, tokens = ST.tokenize(sv), glosses = {[key] = english}, source = ... }
-- and shows it its own way; any combination of views can be switched on at once.
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

ST.views = ST.views or {}
local views = ST.views

local PANEL = "SwedishTranslatorPanel"
local SUBTITLE = "SwedishTranslatorSubtitle"

local function wrapWidth()
  local wrap = tonumber(getWindowWrap("main")) or 0
  if wrap < 40 then
    wrap = 100
  end
  return wrap
end

function views.inline(entry)
  ST.out("main", { { "dim_gray", "\n  en› " }, { "pale_green", entry.en } })
end

-- Interlinear gloss: every Swedish word sits directly above its English meaning,
-- wrapped into as many row pairs as the console width needs.
function views.gloss(entry)
  local columns = {}
  for _, token in ipairs(entry.tokens) do
    local english = ""
    if token.key then
      english = ST.truncate(entry.glosses[token.key] or "?", 22)
    end
    columns[#columns + 1] = {
      sv = token.text,
      en = english,
      width = math.max(ST.width(token.text), ST.width(english)),
    }
  end

  local indent = 6
  local limit = wrapWidth()
  local rows, row, used = {}, {}, indent
  for _, column in ipairs(columns) do
    if #row > 0 and used + column.width > limit then
      rows[#rows + 1] = row
      row, used = {}, indent
    end
    row[#row + 1] = column
    used = used + column.width + 2
  end
  if #row > 0 then
    rows[#rows + 1] = row
  end

  for index, cells in ipairs(rows) do
    local top, bottom = {}, {}
    for _, cell in ipairs(cells) do
      top[#top + 1] = ST.pad(cell.sv, cell.width)
      bottom[#bottom + 1] = ST.pad(cell.en, cell.width)
    end
    ST.out("main", {
      { "dim_gray", index == 1 and "\n  sv│ " or "\n    │ " },
      { "light_goldenrod", table.concat(top, "  ") },
      { "dim_gray", index == 1 and "\n  en│ " or "\n    │ " },
      { "sky_blue", table.concat(bottom, "  ") },
    })
  end
end

-- Every word is a link: hovering shows its meaning, clicking adds it to the word list.
function views.hover(entry)
  ST.out("main", { { "dim_gray", "\n  ⓘ   " } })
  for index, token in ipairs(entry.tokens) do
    if index > 1 then
      echo("main", " ")
    end
    if token.key then
      local english = entry.glosses[token.key]
      local hint
      if english then
        hint = string.format("%s = %s\n(click to save it to your word list)", token.word, english)
      else
        hint = token.word .. ": not translated yet (click to look it up and save it)"
      end
      ST.link("main", token.text, function()
        ST.saveWord(token.key, english)
      end, hint)
    else
      echo("main", token.text)
    end
  end
  ST.out("main", { { "dim_gray", "   ← hover the words" } })
end

-- Active recall: the learner tries to translate before choosing to look.
function views.reveal(entry)
  ST.out("main", { { "dim_gray", "\n  en› " } })
  ST.link("main", "[ think first - then click to reveal ]", function()
    ST.out("main", {
      { "dim_gray", "\n  " },
      { "light_goldenrod", entry.sv },
      { "dim_gray", "\n  → " },
      { "pale_green", entry.en },
    })
  end, "Translate it in your head first, then click to check yourself")
end

function ST.getPanel()
  if not ST.panel then
    ST.panel = Geyser.UserWindow:new({
      name = PANEL,
      titleText = "Svenska → English",
      docked = true,
      dockPosition = "right",
    })
    -- A docked window otherwise opens only a few characters wide.
    ST.panel:setStyleSheet("QDockWidget { min-width: 380px; }")
    ST.panel:setFontSize(11)
    ST.panel:enableAutoWrap()
    -- Mudlet keeps the window when the Lua state is rebuilt (e.g. after a
    -- profile's scripts are reset), so start it over from the saved history.
    ST.panel:clear()
    ST.out(PANEL, { { "gold", "Svenska → English\n" }, { "dim_gray", "Your translations collect here.\n" } })
    local history = ST.state.history
    for i = math.max(1, #history - 19), #history do
      ST.panelEntry(history[i])
    end
  end
  ST.panel:show()
  return ST.panel
end

function ST.panelEntry(entry)
  ST.out(PANEL, {
    { "dim_gray", "\n" .. os.date("%H:%M", entry.t) .. "  " },
    { "light_goldenrod", entry.sv },
    { "dim_gray", "\n       " },
    { "pale_green", entry.en },
    { "dim_gray", "\n" },
  })
end

function views.panel(entry)
  ST.getPanel()
  ST.panelEntry({ sv = entry.sv, en = entry.en, t = os.time() })
end

function ST.getSubtitle()
  if not ST.subtitle then
    ST.subtitle = Geyser.Label:new({
      name = SUBTITLE,
      x = "8%", y = "-150px",
      width = "84%", height = "115px",
    })
    ST.subtitle:setStyleSheet([[
      background-color: rgba(12, 14, 24, 225);
      border: 1px solid rgba(255, 215, 0, 150);
      border-radius: 12px;
      padding: 6px 14px;
    ]])
    ST.subtitle:setToolTip("Click to dismiss")
    ST.subtitle:setClickCallback(function()
      ST.subtitle:hide()
    end)
  end
  return ST.subtitle
end

function views.subtitle(entry)
  local label = ST.getSubtitle()
  label:echo(string.format(
    [[<span style="color:#eedd82; font-size:13pt;">%s</span><br><span style="color:#ffffff; font-size:18pt; font-weight:600;">%s</span>]],
    ST.htmlEscape(entry.sv), ST.htmlEscape(entry.en)), nil, "c")
  label:show()
  raiseWindow(SUBTITLE)
  if ST.subtitleTimer then
    killTimer(ST.subtitleTimer)
  end
  -- Long enough to read both lines: a base plus time per word, within sane bounds.
  local seconds = math.min(15, math.max(5, 3 + #entry.tokens * 0.7))
  ST.subtitleTimer = tempTimer(seconds, function()
    ST.subtitleTimer = nil
    label:hide()
  end)
end

function ST.hideView(name)
  if name == "panel" and ST.panel then
    ST.panel:hide()
  elseif name == "subtitle" and ST.subtitle then
    ST.subtitle:hide()
  end
end

local function needsGlosses(viewSet)
  return viewSet.gloss or viewSet.hover
end

local function anyEnabled(viewSet)
  for _, enabled in pairs(viewSet) do
    if enabled then
      return true
    end
  end
  return false
end

-- inline and reveal only show English, so they need the Swedish printed above
-- them; gloss and hover already show the Swedish themselves.
local function needsHeader(viewSet)
  return viewSet.inline or viewSet.reveal
end

-- Requests finish in whatever order the network returns them, but auto-translated
-- lines are shown strictly in the order they arrived, so a run of them reads top
-- to bottom like the game text itself. Anything the player asked for directly
-- skips this queue and appears as soon as it is ready.
ST.outputQueue = ST.outputQueue or {}

local function flushOutput()
  while ST.outputQueue[1] and ST.outputQueue[1].render do
    local slot = table.remove(ST.outputQueue, 1)
    local ok, err = pcall(slot.render)
    if not ok then
      ST.warn("display error: " .. tostring(err))
      ST.finish("main")
    end
  end
end

local AUTO_ERROR_QUIET_SECONDS = 60

-- A failing service would otherwise print the same error under every Swedish
-- line from the game; say it once, then stay quiet for a while.
local function reportAutoFailure(reason)
  if reason:find("^skipped") or (ST.autoQuietUntil and os.time() < ST.autoQuietUntil) then
    return
  end
  ST.autoQuietUntil = os.time() + AUTO_ERROR_QUIET_SECONDS
  ST.warn("auto-translate: " .. reason .. " (repeats of this are hidden for a minute)")
end

-- Translates `text` and shows it in every view in `opts.views` (default: the
-- views switched on). `source` is "command", "selection", "auto" or "demo";
-- for "auto" the Swedish line is already on screen so it is not repeated.
-- `opts.done(success)` is called once everything has been shown.
function ST.process(text, source, opts)
  opts = opts or {}
  local viewSet = opts.views or ST.state.views
  local auto = source == "auto"
  if not anyEnabled(viewSet) then
    if not auto then
      ST.warn("every view is switched off - turn one on with sv:views")
    end
    if opts.done then
      opts.done(false)
    end
    return
  end
  local entry = { sv = ST.trim(text), source = source, tokens = ST.tokenize(text), glosses = {} }
  local glossing = needsGlosses(viewSet)
  local pending = glossing and 2 or 1
  local failed, glossFailure = nil, nil

  local function render()
    if failed then
      if auto then
        reportAutoFailure(failed)
      else
        ST.warn("translation failed: " .. failed)
      end
    else
      if not auto and needsHeader(viewSet) then
        ST.out("main", { { "dim_gray", "\n  sv› " }, { "light_goldenrod", entry.sv } })
      end
      for _, name in ipairs(ST.viewOrder) do
        if viewSet[name] then
          local ok, err = pcall(views[name], entry)
          if not ok then
            ST.warn(string.format("the %s view failed: %s", name, tostring(err)))
          end
        end
      end
      if glossFailure and not auto then
        ST.out("main", { { "dim_gray", string.format("\n  (%d %s could not be glossed: %s)", glossFailure.count,
          glossFailure.count == 1 and "word" or "words", tostring(glossFailure.reason)) } })
      end
      ST.addHistory(entry.sv, entry.en)
    end
    ST.finish("main")
    if opts.done then
      opts.done(not failed)
    end
  end

  local slot = nil
  if auto then
    slot = {}
    ST.outputQueue[#ST.outputQueue + 1] = slot
  end
  local function step()
    pending = pending - 1
    if pending == 0 then
      if slot then
        slot.render = render
        flushOutput()
      else
        render()
      end
    end
  end

  -- Auto-translate glosses words only from the dictionary and cache: one request
  -- per unknown word for every game line would soon exhaust the free quota.
  local requestOpts = { auto = auto, offline = auto }
  ST.translate(entry.sv, function(ok, result)
    if ok then
      entry.en = result
    else
      failed = result
    end
    step()
  end, requestOpts)
  if glossing then
    if failed then
      -- The sentence was rejected outright (e.g. too long): looking up its
      -- words would only spend requests on output that is never shown.
      step()
    else
      ST.lookupWords(entry.tokens, function(glosses, failure)
        entry.glosses = glosses
        glossFailure = failure
        step()
      end, requestOpts)
    end
  end
end
