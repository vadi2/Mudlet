-- Command-line interface and start-up.
--   sv <swedish text>      translate it
--   sv:<command> [args]    everything else - see sv:help
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

local SAMPLES = {
  "Hej! Välkommen till Stockholm.",
  "Jag skulle vilja ha en kopp kaffe, tack.",
  "Den gamla mannen går långsamt genom skogen.",
  "Draken vaktar en stor skatt i grottan.",
  "Vad heter du, och var bor du?",
  "Det regnar idag, men imorgon blir det sol.",
}

local function onOff(value)
  if value == "on" then
    return true
  elseif value == "off" then
    return false
  end
  return nil
end

function ST.help()
  ST.msg("Swedish → English translator " .. ST.version, "gold")
  local lines = {
    { "sv <swedish text>", "translate text (or select text, right-click → Translate)" },
    { "sv:views", "show the visualizations and switch them on/off" },
    { "sv:view <name> [on|off]", "toggle one view: " .. table.concat(ST.viewOrder, ", ") },
    { "sv:only <name> [name...]", "use exactly these views" },
    { "sv:demo", "see every view side by side, to choose your favorites" },
    { "sv:auto [on|off]", "auto-translate Swedish lines coming from the game" },
    { "sv:save <word>", "add a word to your word list" },
    { "sv:words", "show your word list" },
    { "sv:quiz", "flashcard quiz on your saved words" },
    { "sv:history [n]", "show your last n translations" },
    { "sv:email <address|off>", "raise MyMemory's free daily limit from 5k to 50k characters" },
    { "sv:clear <history|words|cache>", "forget saved data" },
  }
  for _, line in ipairs(lines) do
    ST.out("main", { { "light_goldenrod", "\n  " .. ST.pad(line[1], 32) }, { "light_gray", line[2] } })
  end
  ST.out("main", { { "dim_gray", string.format("\n  auto-translate is %s; translations by MyMemory (mymemory.translated.net)", ST.state.auto and "ON" or "off") } })
end

function ST.showViews()
  ST.msg("visualizations (click to switch on/off):", "gold")
  for _, name in ipairs(ST.viewOrder) do
    local enabled = ST.state.views[name]
    ST.out("main", { { "dim_gray", "\n  " } })
    ST.link("main", enabled and "[■ on ]" or "[□ off]", function()
      ST.setView(name, not ST.state.views[name])
      ST.showViews()
    end, (enabled and "Switch off " or "Switch on ") .. name)
    ST.out("main", {
      { enabled and "pale_green" or "light_gray", "  " .. ST.pad(name, 9) },
      { "dim_gray", ST.viewInfo[name] },
    })
  end
end

function ST.setView(name, enabled)
  if not ST.viewInfo[name] then
    ST.warn(string.format("no view called '%s' - choose from: %s", name, table.concat(ST.viewOrder, ", ")))
    return false
  end
  ST.state.views[name] = enabled
  if not enabled then
    ST.hideView(name)
  end
  ST.scheduleSave()
  return true
end

local function enabledViewNames()
  local names = {}
  for _, name in ipairs(ST.viewOrder) do
    if ST.state.views[name] then
      names[#names + 1] = name
    end
  end
  return #names > 0 and table.concat(names, ", ") or "none"
end

-- Shows the sample sentences, one view each, so the views can be compared directly.
function ST.demo()
  ST.msg("demo: one sample sentence per view. Turn the ones you like on with sv:only <names>", "gold")
  local index = 0
  local function nextView()
    index = index + 1
    local name = ST.viewOrder[index]
    if not name then
      ST.msg("demo done. Currently on: " .. enabledViewNames() .. "  - change with sv:views", "gold")
      ST.finish("main")
      return
    end
    local where = ""
    if name == "panel" then
      where = "  (see side panel)"
    elseif name == "subtitle" then
      where = "  (see bottom of window)"
    end
    ST.out("main", {
      { "dim_gray", "\n\n── " },
      { "gold", name },
      { "dim_gray", " - " .. ST.viewInfo[name] .. where },
    })
    ST.process(SAMPLES[(index - 1) % #SAMPLES + 1], "demo", {
      views = { [name] = true },
      done = function() nextView() end,
    })
  end
  nextView()
end

local commands = {}

commands.help = function() ST.help() end
commands.views = function() ST.showViews() end
commands.demo = function() ST.demo() end
commands.words = function() ST.showWords() end
commands.quiz = function() ST.quiz() end

commands.view = function(args)
  local name, state = args:match("^(%S+)%s*(%S*)$")
  if not name then
    ST.showViews()
    return
  end
  name = name:lower()
  local enabled = onOff(state)
  if state ~= "" and enabled == nil then
    ST.warn("use: sv:view <name> [on|off]")
    return
  end
  if enabled == nil then
    enabled = not ST.state.views[name]
  end
  if ST.setView(name, enabled) then
    ST.msg(string.format("%s view %s (on: %s)", name, enabled and "ON" or "off", enabledViewNames()))
  end
end

commands.only = function(args)
  local wanted = {}
  for name in args:lower():gmatch("[^%s,]+") do
    if not ST.viewInfo[name] then
      ST.warn(string.format("no view called '%s' - choose from: %s", name, table.concat(ST.viewOrder, ", ")))
      return
    end
    wanted[name] = true
  end
  if next(wanted) == nil then
    ST.warn("use: sv:only <name> [name...], e.g. sv:only inline gloss")
    return
  end
  for _, name in ipairs(ST.viewOrder) do
    ST.setView(name, wanted[name] == true)
  end
  ST.msg("views on: " .. enabledViewNames())
end

commands.auto = function(args)
  local enabled = onOff(args:lower())
  if args ~= "" and enabled == nil then
    ST.warn("use: sv:auto [on|off]")
    return
  end
  if enabled == nil then
    enabled = not ST.state.auto
  end
  ST.state.auto = enabled
  ST.scheduleSave()
  ST.msg(enabled and "auto-translate ON: Swedish lines from the game will be translated" or "auto-translate off")
end

commands.save = function(args)
  ST.saveWord(args)
end

commands.forget = function(args)
  ST.forgetWord(args)
end

commands.history = function(args)
  ST.showHistory(tonumber(args))
end

commands.email = function(args)
  if args == "" or args:lower() == "off" then
    ST.state.email = ""
    ST.msg("e-mail removed; using MyMemory's anonymous limit")
  elseif args:find("^[^@%s]+@[^@%s]+%.[^@%s]+$") then
    ST.state.email = args
    ST.msg("MyMemory requests will include " .. args .. " for the larger free limit")
  else
    ST.warn("that does not look like an e-mail address")
    return
  end
  ST.scheduleSave()
end

commands.clear = function(args)
  local what = args:lower()
  if what == "history" then
    ST.state.history = {}
  elseif what == "words" then
    ST.state.vocab = {}
  elseif what == "cache" then
    ST.state.cache = {}
  else
    ST.warn("use: sv:clear history, sv:clear words or sv:clear cache")
    return
  end
  ST.scheduleSave()
  ST.msg("cleared your " .. what)
end

function ST.onCommand(line)
  local command, args = line:match("^sv:(%S*)%s*(.-)%s*$")
  if command then
    local handler = commands[command:lower()]
    if handler then
      handler(args)
    else
      ST.warn(string.format("unknown command 'sv:%s' - see sv:help", command))
    end
  else
    local text = line:match("^sv%s+(.-)%s*$")
    if text and text ~= "" then
      ST.process(text, "command")
    else
      ST.help()
    end
  end
  ST.finish("main")
end

-- Start-up ---------------------------------------------------------------------

-- Scripts re-run whenever they are saved in the editor; keep in-memory state then.
if not ST.state then
  ST.load()
end
ST.registerMouseEvent()

registerNamedEventHandler(ST.handlerUser, "selection", ST.mouseEvent, function(...) ST.onSelection(...) end)
registerNamedEventHandler(ST.handlerUser, "exit", "sysExitEvent", function() ST.save() end)
registerNamedEventHandler(ST.handlerUser, "installed", "sysInstall", function(_, name)
  if name == "SwedishTranslator" then
    ST.msg("installed! Type sv:demo to see every visualization, or sv:help for all commands.", "pale_green")
    ST.finish("main")
  end
end)
registerNamedEventHandler(ST.handlerUser, "uninstalled", "sysUninstall", function(_, name)
  if name ~= "SwedishTranslator" then
    return
  end
  ST.save()
  ST.hideView("panel")
  ST.hideView("subtitle")
  if getMouseEvents()[ST.mouseEvent] then
    removeMouseEvent(ST.mouseEvent)
  end
  deleteAllNamedEventHandlers(ST.handlerUser)
end)
