-- Online translation through the MyMemory API (https://mymemory.translated.net),
-- which needs no account or key. Requests are queued so a burst of game text
-- never has more than a few calls in flight, identical requests share one call,
-- and every result is cached so a phrase is only ever fetched once.
SwedishTranslator = SwedishTranslator or {}
local ST = SwedishTranslator

local API = "https://api.mymemory.translated.net/get?langpair=sv%7Cen&q="
local MAX_IN_FLIGHT = 3
local TIMEOUT_SECONDS = 15
local MAX_QUERY_BYTES = 450 -- MyMemory rejects queries over 500 bytes
local MAX_QUEUED_AUTO = 20

ST.queue = ST.queue or {}         -- requests waiting for a free slot, oldest first
ST.byKey = ST.byKey or {}         -- [cache key] = request, while queued or in flight
ST.inFlight = ST.inFlight or {}   -- [url] = request
ST.inFlightCount = ST.inFlightCount or 0

local function finish(request, ok, result)
  ST.byKey[request.key] = nil
  for _, callback in ipairs(request.callbacks) do
    local success, err = pcall(callback, ok, result)
    if not success then
      ST.warn("display error: " .. tostring(err))
    end
  end
end

local function release(request)
  if request.url and ST.inFlight[request.url] == request then
    ST.inFlight[request.url] = nil
    ST.inFlightCount = ST.inFlightCount - 1
  end
  if request.timer then
    killTimer(request.timer)
    request.timer = nil
  end
end

function ST.pump()
  while ST.inFlightCount < MAX_IN_FLIGHT and #ST.queue > 0 do
    local request = table.remove(ST.queue, 1)
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
      request.timer = tempTimer(TIMEOUT_SECONDS, function()
        request.timer = nil
        release(request)
        finish(request, false, "the translation service did not answer in time")
        ST.pump()
      end)
    end
  end
end

-- Translates `text` and calls `callback(ok, englishOrError)`. Cached and
-- dictionary results call back immediately, before this function returns.
-- opts.auto marks low-priority requests from auto-translate, which are dropped
-- (oldest first) rather than allowed to pile up behind a flood of game text.
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
  if request then
    request.callbacks[#request.callbacks + 1] = callback
    request.auto = request.auto and opts.auto
    return
  end

  request = { key = key, text = text, callbacks = { callback }, auto = opts.auto }
  ST.byKey[key] = request
  ST.queue[#ST.queue + 1] = request

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
-- The service capitalizes single words ("världen" -> "World"); that is undone
-- unless the word only ever appeared capitalized, i.e. is probably a name.
function ST.lookupWord(key, callback, opts, keepCase)
  local known = ST.dictionary[key]
  if known then
    callback(true, known)
    return
  end
  ST.translate(key, function(ok, english)
    if ok and not keepCase then
      english = ST.lower(english)
    end
    callback(ok, english)
  end, opts)
end

-- Looks up every distinct word in `tokens`, then calls `callback(glosses)` once
-- with glosses[key] = english (or nil when a word could not be translated).
function ST.lookupWords(tokens, callback, opts)
  local glosses, keys, capitalizedOnly = {}, {}, {}
  for _, token in ipairs(tokens) do
    if token.key then
      local capitalized = token.word ~= token.key
      if capitalizedOnly[token.key] == nil then
        keys[#keys + 1] = token.key
        capitalizedOnly[token.key] = capitalized
      else
        capitalizedOnly[token.key] = capitalizedOnly[token.key] and capitalized
      end
    end
  end
  local remaining = #keys
  if remaining == 0 then
    callback(glosses)
    return
  end
  for _, key in ipairs(keys) do
    ST.lookupWord(key, function(ok, english)
      if ok then
        glosses[key] = english
      end
      remaining = remaining - 1
      if remaining == 0 then
        callback(glosses)
      end
    end, opts, capitalizedOnly[key])
  end
end

-- MyMemory blends machine translation with a crowd-sourced translation memory,
-- and the top memory hit is occasionally junk (e.g. "hej" -> a whole e-mail
-- template). Reject candidates that look like that.
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
  -- The headline result wins a tie with the same text from the match list.
  add(data.responseData.translatedText, (tonumber(data.responseData.match) or 0) + 0.001)
  if type(data.matches) == "table" then
    for _, match in ipairs(data.matches) do
      if type(match) == "table" then
        add(match.translation, match.match)
      end
    end
  end
  table.sort(candidates, function(a, b) return a.score > b.score end)
  for _, candidate in ipairs(candidates) do
    if plausible(candidate.text, source) then
      return candidate.text
    end
  end
  return nil
end

local function parseResponse(body, source)
  local ok, data = pcall(yajl.to_value, body)
  if not ok or type(data) ~= "table" then
    return false, "the translation service sent an unreadable reply"
  end
  local status = tonumber(data.responseStatus)
  local translated = type(data.responseData) == "table" and data.responseData.translatedText
  local details = type(data.responseDetails) == "string" and data.responseDetails or ""
  if data.quotaFinished == true then
    return false, "today's free MyMemory quota is used up - try again tomorrow, or raise it with sv:email"
  end
  if status ~= 200 or type(translated) ~= "string" or translated == "" then
    return false, "the translation service refused: " .. (details ~= "" and details or ("status " .. tostring(data.responseStatus)))
  end
  -- MyMemory reports some failures as a 200 whose "translation" is an error message.
  if translated:find("^MYMEMORY WARNING") or translated:find("^QUERY LENGTH LIMIT") or translated:find("^INVALID ") then
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
  local ok, result = parseResponse(body, request.text)
  if ok then
    ST.state.cache[request.key] = { en = result, t = os.time() }
    ST.scheduleSave()
  end
  finish(request, ok, result)
  ST.pump()
end

function ST.onHttpError(_, message, url)
  local request = ST.inFlight[url]
  if not request then
    return
  end
  release(request)
  finish(request, false, "network error: " .. tostring(message))
  ST.pump()
end

registerNamedEventHandler(ST.handlerUser, "httpDone", "sysGetHttpDone", function(...) ST.onHttpDone(...) end)
registerNamedEventHandler(ST.handlerUser, "httpError", "sysGetHttpError", function(...) ST.onHttpError(...) end)
