-- Online translation through the MyMemory API (https://mymemory.translated.net),
-- which needs no account or key. Requests are queued so a burst of game text
-- never has more than a few calls in flight, identical requests share one call,
-- and successful results are cached (see MAX_CACHE in core), so repeated text
-- is normally not fetched again.
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

local API = "https://api.mymemory.translated.net/get?langpair=sv%7Cen&q="
local MAX_IN_FLIGHT = 3
local TIMEOUT_SECONDS = 15
local MAX_QUERY_BYTES = 450 -- MyMemory rejects queries over 500 bytes
local MAX_QUEUED_AUTO = 20
local QUOTA_PAUSE_SECONDS = 30 * 60
local RATE_LIMIT_PAUSE_SECONDS = 60

ST.queue = ST.queue or {}         -- requests waiting for a free slot, oldest first
ST.byKey = ST.byKey or {}         -- [cache key] = request, while queued or in flight
ST.inFlight = ST.inFlight or {}   -- [url] = request
ST.inFlightCount = ST.inFlightCount or 0
ST.autoInFlightCount = ST.autoInFlightCount or 0

local function finish(request, ok, result)
  ST.byKey[request.key] = nil
  for _, callback in ipairs(request.callbacks) do
    local success, err = pcall(callback, ok, result)
    if not success then
      ST.warn("display error: " .. tostring(err))
      ST.finish("main")
    end
  end
end

local function release(request)
  if request.url and ST.inFlight[request.url] == request then
    ST.inFlight[request.url] = nil
    ST.inFlightCount = ST.inFlightCount - 1
    if request.sentAsAuto then
      ST.autoInFlightCount = ST.autoInFlightCount - 1
    end
  end
  if request.timer then
    killTimer(request.timer)
    request.timer = nil
  end
end

-- After MyMemory says the quota or rate limit is exhausted, further requests
-- would only fail the same way, so fail them locally until the pause is over.
local function pauseService(seconds, reason)
  ST.pausedUntil = os.time() + seconds
  ST.pauseReason = reason
end

function ST.resumeService()
  ST.pausedUntil = nil
end

local function servicePaused()
  return ST.pausedUntil and os.time() < ST.pausedUntil
end

-- Auto-translate may only use some of the slots, so a slow or stuck service
-- answering game text can never keep the player's own request waiting.
function ST.pump()
  while ST.inFlightCount < MAX_IN_FLIGHT and #ST.queue > 0 do
    if ST.queue[1].auto and ST.autoInFlightCount >= MAX_IN_FLIGHT - 1 then
      return
    end
    local request = table.remove(ST.queue, 1)
    if servicePaused() then
      finish(request, false, ST.pauseReason)
    else
      local url = API .. ST.urlencode(request.text)
      if ST.state.email ~= "" then
        url = url .. "&de=" .. ST.urlencode(ST.state.email)
      end
      local ok, actualUrl = getHTTP(url)
      if not ok then
        finish(request, false, "could not start the request: " .. tostring(actualUrl))
      else
        -- Mudlet reports completion with the URL it actually requested, which can be
        -- normalized from what we passed in, so key on that.
        request.url = actualUrl
        ST.inFlight[actualUrl] = request
        ST.inFlightCount = ST.inFlightCount + 1
        request.sentAsAuto = request.auto
        if request.auto then
          ST.autoInFlightCount = ST.autoInFlightCount + 1
        end
        request.timer = tempTimer(TIMEOUT_SECONDS, function()
          request.timer = nil
          release(request)
          finish(request, false, "the translation service did not answer in time")
          ST.pump()
        end)
      end
    end
  end
end

-- Auto requests go to the back; the player's own requests go ahead of them.
local function enqueue(request)
  local position = #ST.queue + 1
  if not request.auto then
    for index, queued in ipairs(ST.queue) do
      if queued.auto then
        position = index
        break
      end
    end
  end
  table.insert(ST.queue, position, request)
end

-- Translates `text` and calls `callback(ok, englishOrError)`. Cached results and
-- input errors call back immediately, before this function returns.
-- opts.auto marks low-priority requests from auto-translate: anything the
-- player asked for directly is sent first, and when game text floods in the
-- oldest auto requests are dropped rather than allowed to pile up.
function ST.translate(text, callback, opts)
  opts = opts or {}
  text = ST.trim(text)
  local key = ST.lower(text)
  if key == "" then
    callback(false, "nothing to translate")
    return
  end

  local cached = ST.state.cache[key]
  if cached then
    cached.t = os.time()
    callback(true, cached.en)
    return
  end

  if #text > MAX_QUERY_BYTES then
    callback(false, string.format("text is too long to translate at once (%d bytes, max %d)", #text, MAX_QUERY_BYTES))
    return
  end

  local request = ST.byKey[key]
  if not request and servicePaused() then
    callback(false, ST.pauseReason)
    return
  end
  if request then
    request.callbacks[#request.callbacks + 1] = callback
    if request.auto and not opts.auto then
      -- The player now wants this too: move it ahead of the auto requests.
      request.auto = false
      for index, queued in ipairs(ST.queue) do
        if queued == request then
          table.remove(ST.queue, index)
          enqueue(request)
          break
        end
      end
      ST.pump()
    end
    return
  end

  request = { key = key, text = text, callbacks = { callback }, auto = opts.auto }
  ST.byKey[key] = request
  enqueue(request)

  if opts.auto then
    local queuedAuto = 0
    for _, queued in ipairs(ST.queue) do
      if queued.auto then
        queuedAuto = queuedAuto + 1
      end
    end
    if queuedAuto > MAX_QUEUED_AUTO then
      for index, queued in ipairs(ST.queue) do
        if queued.auto then
          table.remove(ST.queue, index)
          finish(queued, false, "skipped: too much text arriving at once")
          break
        end
      end
    end
  end

  ST.pump()
end

-- Glosses one word: the offline dictionary first, then the cache, then online.
-- With opts.offline it never goes online: auto-translate uses that, because one
-- request per unknown word for every game line would quickly use up the quota.
-- The service capitalizes single words ("världen" -> "World"); that is undone
-- unless the word only ever appeared capitalized, i.e. is probably a name.
function ST.lookupWord(key, callback, opts, keepCase, original)
  opts = opts or {}
  local known = ST.dictionary[key]
  if known then
    callback(true, known)
    return
  end
  if opts.offline and not ST.state.cache[key] then
    callback(false, "not in the offline dictionary")
    return
  end
  ST.translate(keepCase and original or key, function(ok, english)
    if ok and not keepCase then
      english = ST.lower(english)
    end
    callback(ok, english)
  end, opts)
end

-- Looks up every distinct word in `tokens`, then calls `callback(glosses, failure)`
-- once, with glosses[key] = english for the words that could be translated and
-- failure = { count = n, reason = firstError } when some could not (else nil).
function ST.lookupWords(tokens, callback, opts)
  local glosses, keys, capitalizedOnly, spelling = {}, {}, {}, {}
  local failure = nil
  for _, token in ipairs(tokens) do
    if token.key then
      local capitalized = token.word ~= token.key
      if capitalizedOnly[token.key] == nil then
        keys[#keys + 1] = token.key
        capitalizedOnly[token.key] = capitalized
        spelling[token.key] = token.word
      else
        capitalizedOnly[token.key] = capitalizedOnly[token.key] and capitalized
      end
    end
  end
  local remaining = #keys
  if remaining == 0 then
    callback(glosses, failure)
    return
  end
  for _, key in ipairs(keys) do
    ST.lookupWord(key, function(ok, english)
      if ok then
        glosses[key] = english
      else
        failure = failure or { count = 0, reason = english }
        failure.count = failure.count + 1
      end
      remaining = remaining - 1
      if remaining == 0 then
        callback(glosses, failure)
      end
    end, opts, capitalizedOnly[key], spelling[key])
  end
end

local function bare(s)
  return (ST.lower(s):gsub("[%p%s]", ""))
end

-- MyMemory blends machine translation with a crowd-sourced translation memory,
-- and memory hits are occasionally junk: "hej" -> a whole e-mail template,
-- markup like <ex id="_1"/>, or the Swedish text itself handed back. Reject
-- the first two outright; see pickTranslation for the third.
local function plausible(candidate, source)
  return candidate:find("[%a\128-\255]") ~= nil
    and not candidate:find("<%a")
    and not candidate:find("%%s")
    and ST.width(candidate) <= 3 * ST.width(source) + 15
end

-- Picks the best-scoring plausible translation among the top result and the
-- alternative matches.
local function pickTranslation(data, source)
  local candidates = {}
  local function add(text, score)
    if type(text) == "string" then
      text = ST.trim(ST.decodeEntities(text))
      candidates[#candidates + 1] = { text = text, score = tonumber(score) or 0 }
    end
  end
  -- Nudge the headline result so it wins ties against any match-list candidate.
  add(data.responseData.translatedText, (tonumber(data.responseData.match) or 0) + 0.001)
  if type(data.matches) == "table" then
    for _, match in ipairs(data.matches) do
      if type(match) == "table" then
        add(match.translation, match.match)
      end
    end
  end
  table.sort(candidates, function(a, b) return a.score > b.score end)
  -- Text identical to the Swedish is usually the input handed back untranslated,
  -- but a single word can be its own translation (names, and cognates such as
  -- "bank" or "radio"), so only multi-word text has to come back changed.
  local singleWord = not ST.trim(source):find("%s")
  for _, candidate in ipairs(candidates) do
    if plausible(candidate.text, source) and (singleWord or bare(candidate.text) ~= bare(source)) then
      return candidate.text
    end
  end
  return nil
end

local QUOTA_MESSAGE = "today's free MyMemory quota is used up - try again later, or raise it with sv:email"

local function parseResponse(body, source)
  local ok, data = pcall(yajl.to_value, body)
  if not ok or type(data) ~= "table" then
    return false, "the translation service sent an unreadable reply"
  end
  local status = tonumber(data.responseStatus)
  local translated = type(data.responseData) == "table" and data.responseData.translatedText
  local details = type(data.responseDetails) == "string" and data.responseDetails or ""
  if data.quotaFinished == true then
    pauseService(QUOTA_PAUSE_SECONDS, QUOTA_MESSAGE)
    return false, QUOTA_MESSAGE
  end
  -- MyMemory reports some failures as a "translation" that is an error message.
  if type(translated) == "string" and translated:find("^MYMEMORY WARNING") then
    pauseService(QUOTA_PAUSE_SECONDS, QUOTA_MESSAGE)
    return false, QUOTA_MESSAGE
  end
  if status ~= 200 or type(translated) ~= "string" or translated == "" then
    return false, "the translation service refused: " .. (details ~= "" and details or ("status " .. tostring(data.responseStatus)))
  end
  if translated:find("^QUERY LENGTH LIMIT") or translated:find("^INVALID ") then
    return false, "the translation service refused: " .. translated
  end
  local best = pickTranslation(data, source)
  if not best then
    return false, "no usable translation found"
  end
  return true, best
end

function ST.onHttpDone(_, url, body)
  local request = ST.inFlight[url]
  if not request then
    return
  end
  release(request)
  -- Whatever the reply holds, the request must finish: a request that never
  -- calls back would hold up every translation queued after it.
  local called, ok, result = pcall(parseResponse, body, request.text)
  if not called then
    ok, result = false, "could not read the reply: " .. tostring(ok)
  end
  if ok then
    ST.state.cache[request.key] = { en = result, t = os.time() }
    ST.scheduleSave()
  end
  finish(request, ok, result)
  ST.pump()
end

-- Qt's error text embeds the whole request URL - the text being translated, and
-- the user's e-mail when one is set - so describe the failure without it.
local function describeHttpError(message)
  message = tostring(message):gsub("https?://%S+", "the translation service")
  if message:find("Too Many Requests", 1, true) then
    pauseService(RATE_LIMIT_PAUSE_SECONDS, "MyMemory is rate-limiting requests - wait a minute, or raise the limit with sv:email")
    return ST.pauseReason
  end
  local reply = message:match("server replied: (.+)$")
  if reply and reply ~= "" then
    return "the translation service replied: " .. reply
  end
  return "network error: " .. message
end

function ST.onHttpError(_, message, url)
  local request = ST.inFlight[url]
  if not request then
    return
  end
  release(request)
  local called, description = pcall(describeHttpError, message)
  finish(request, false, called and description or "network error")
  ST.pump()
end

registerNamedEventHandler(ST.handlerUser, "httpDone", "sysGetHttpDone", function(...) ST.onHttpDone(...) end)
registerNamedEventHandler(ST.handlerUser, "httpError", "sysGetHttpError", function(...) ST.onHttpError(...) end)
