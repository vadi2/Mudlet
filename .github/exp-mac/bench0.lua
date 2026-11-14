-- Faithful Mudlet-side reimplementation of the PipelineBenchmark trigger set,
-- with per-trigger counting scripts, as used in the smudgy-vs-mudlet comparison.
local IGNORE = -1
_G.benchCounts = {}
_G.benchScriptInvocations = 0
_G.benchN = 0

local function counter(i)
  return function()
    _G.benchCounts[i] = (_G.benchCounts[i] or 0) + 1
    _G.benchScriptInvocations = _G.benchScriptInvocations + 1
  end
end

local ids = {}
local function add(fn, ...)
  _G.benchN = _G.benchN + 1
  local i = _G.benchN
  _G.benchCounts[i] = 0
  local args = {...}
  args[#args + 1] = counter(i)
  local id = nil
  ids[#ids + 1] = id
  return id
end

for _, s in ipairs({"forest","orc","gold","experience","sword","tower","damage","coins","café","Square","dragon","teleport"}) do
  add(tempTrigger, s)
end

for _, r in ipairs({"^(\\w+) tells you '(.+)'$","You gain (\\d+) experience","hits you for (\\d+) damage",
                    "HP: (\\d+)/(\\d+) MP: (\\d+)/(\\d+)","carrying: (.+)$","(\\d+) gold coins","The (\\w+ \\w+)",
                    "^A glowing (\\w+)","whisper from (\\w+):","^\\[(\\d{2}):(\\d{2})\\]","reaches level (\\d+)",
                    "(\\w+) arrives from the (\\w+)"}) do
  add(tempRegexTrigger, r)
end

for _, s in ipairs({"You are","The","HP:","You gain"}) do
  add(tempBeginOfLineTrigger, s)
end

for _, s in ipairs({"You are hungry.","You are thirsty.","It is pitch black.","The door is closed.",
                    "You have no keys.","Nothing happens.","You feel better.","Your wounds close.",
                    "The orc dies.","You are hidden.","A cool breeze blows.","You cannot go that way."}) do
  add(tempExactMatchTrigger, s)
end

for _, fg in ipairs({1, 2, 3, 6}) do
  add(tempAnsiColorTrigger, fg, IGNORE)
end

-- The two multiline (multi-pattern, stay-open) triggers.
local multi = {
  {"bench_multi_1", {"The (\\w+) hits you", "damage"}},
  {"bench_multi_2", {"(\\w+) tells you", "tower"}},
}
for _, m in ipairs({}) do
  _G.benchN = _G.benchN + 1
  local i = _G.benchN
  _G.benchCounts[i] = 0
  permRegexTrigger(m[1], "", m[2],
    ([[_G.benchCounts[%d] = _G.benchCounts[%d] + 1; _G.benchScriptInvocations = _G.benchScriptInvocations + 1]]):format(i, i))
  setTriggerStayOpen(m[1], 5)
end


-- ---------------------------------------------------------------------------
-- Instrumentation.
--
-- Two independent probes run alongside the workload:
--
--   * a 16ms repeating timer, whose firing gaps measure how long the main
--     thread is unavailable - i.e. how long the window is frozen; and
--   * a 250ms sampler recording (time, line count, script invocations), from
--     which sustained throughput is derived.
--
-- Throughput is taken from the samples rather than from a start/end marker so
-- that a build which strands the tail of a burst still reports its real speed.
-- ---------------------------------------------------------------------------
-- Configuration comes from a BENCH_CFG global when one is set (Windows, where
-- the launcher cannot pass an environment), otherwise from the environment.
local CFG = _G.BENCH_CFG or {}
local RESULT = CFG.result or os.getenv("BENCH_RESULT") or "/tmp/bench-result.txt"
local LABEL = CFG.label or os.getenv("BENCH_LABEL") or "unlabelled"

local function dbg(msg)
  local h = io.open(RESULT .. ".dbg", "a")
  if h then h:write(os.date("%H:%M:%S ") .. msg .. "\n"); h:close() end
end

local hbGaps, hbLast, hbOn = {}, nil, false
local function heartbeat()
  local t = getEpoch()
  if hbOn and hbLast then hbGaps[#hbGaps + 1] = t - hbLast end
  hbLast = t
  tempTimer(0.016, heartbeat)
end
tempTimer(0.016, heartbeat)

local samples = {}
local running, finished = false, false
local stallTicks = 0

local function stats(t)
  if #t == 0 then return nil end
  local s = {}
  for i, v in ipairs(t) do s[i] = v end
  table.sort(s)
  local sum = 0
  for _, v in ipairs(s) do sum = sum + v end
  local function p(q) return s[math.max(1, math.ceil(#s * q))] end
  return {n = #s, mean = sum / #s, p50 = p(0.5), p90 = p(0.9), p99 = p(0.99), max = s[#s]}
end

local function finish(why)
  if finished then return end
  finished = true
  running = false
  hbOn = false

  local first, last = samples[1], samples[#samples]
  -- Trim the trailing samples where nothing advanced, so elapsed covers only
  -- the period the client was actually consuming.
  local lastActive = #samples
  while lastActive > 1 and samples[lastActive].lines == samples[lastActive - 1].lines do
    lastActive = lastActive - 1
  end
  last = samples[lastActive] or last

  local elapsed = (last.t - first.t)
  local lines = last.lines - first.lines
  local rate = elapsed > 0 and lines / elapsed or 0

  -- Best sustained rate over any 1 second window.
  local peak = 0
  for i = 1, lastActive do
    for j = i + 1, lastActive do
      local dt = samples[j].t - samples[i].t
      if dt >= 1.0 then
        local r = (samples[j].lines - samples[i].lines) / dt
        if r > peak then peak = r end
        break
      end
    end
  end

  local g = stats(hbGaps)
  local over = {[100] = 0, [250] = 0, [500] = 0}
  for _, v in ipairs(hbGaps) do
    for _, k in ipairs({100, 250, 500}) do
      if v * 1000 > k then over[k] = over[k] + 1 end
    end
  end

  local f = io.open(RESULT, "w")
  f:write(string.format("label %s\nstop_reason %s\n", LABEL, why))
  f:write(string.format("elapsed_s %.3f\nlines %d\nrate_lines_per_s %.1f\npeak_lines_per_s %.1f\n",
    elapsed, lines, rate, peak))
  f:write(string.format("invocations %d\ntriggers %d\n", _G.benchScriptInvocations, _G.benchN))
  f:write("matches")
  for i = 1, _G.benchN do f:write(string.format(" %d", _G.benchCounts[i] or 0)) end
  f:write("\n")
  if g then
    f:write(string.format("hb_samples %d\nhb_mean_ms %.1f\nhb_p50_ms %.1f\nhb_p90_ms %.1f\nhb_p99_ms %.1f\nhb_max_ms %.1f\nhb_over100 %d\nhb_over250 %d\nhb_over500 %d\n",
      g.n, g.mean * 1000, g.p50 * 1000, g.p90 * 1000, g.p99 * 1000, g.max * 1000,
      over[100], over[250], over[500]))
    local blocked = 0
    for _, v in ipairs(hbGaps) do if v > 0.020 then blocked = blocked + (v - 0.016) end end
    f:write(string.format("hb_blocked_s %.3f\nhb_blocked_pct %.1f\n", blocked, elapsed > 0 and blocked / elapsed * 100 or 0))
  else
    f:write("hb_samples 0\n")
  end
  f:write("samples_t_lines")
  for i = 1, lastActive do f:write(string.format(" %.3f:%d", samples[i].t - first.t, samples[i].lines - first.lines)) end
  f:write("\ngaps_ms")
  for _, v in ipairs(hbGaps) do f:write(string.format(" %.0f", v * 1000)) end
  f:write("\n")
  f:close()
  dbg(string.format("FINISHED %s elapsed=%.3f lines=%d rate=%.0f/s peak=%.0f/s", why, elapsed, lines, rate, peak))
  if CFG.quit or os.getenv("BENCH_QUIT") then tempTimer(1.5, function() quit() end) end
end

local function sample()
  if running then
    local n = getLastLineNumber() or 0
    samples[#samples + 1] = {t = getEpoch(), lines = n, inv = _G.benchScriptInvocations}
    local prev = samples[#samples - 1]
    if prev and prev.lines == n then
      stallTicks = stallTicks + 1
      if stallTicks >= 20 and #samples > 8 then finish("stalled") end
    else
      stallTicks = 0
    end
    if #samples > 1200 then finish("sample_limit") end
  end
  tempTimer(0.25, sample)
end
tempTimer(0.25, sample)

tempExactMatchTrigger("--- BEGIN MEASURING ---", function()
  dbg("BEGIN")
  hbGaps, hbLast, hbOn = {}, getEpoch(), true
  samples = {{t = getEpoch(), lines = getLastLineNumber() or 0, inv = 0}}
  running, stallTicks = true, 0
end)

tempTrigger("END MEASURING", function()
  dbg("END marker seen")
  samples[#samples + 1] = {t = getEpoch(), lines = getLastLineNumber() or 0, inv = _G.benchScriptInvocations}
  finish("end_marker")
end)

dbg("installed " .. _G.benchN .. " triggers, label " .. LABEL)

local host = CFG.host or os.getenv("BENCH_HOST") or "127.0.0.1"
local port = CFG.port or tonumber(os.getenv("BENCH_PORT") or "5099")
tempTimer(1.0, function()
  dbg("connecting to " .. host .. ":" .. port)
  connectToServer(host, port)
end)
