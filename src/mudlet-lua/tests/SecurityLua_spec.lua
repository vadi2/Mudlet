describe("Tests Lua-side security vulnerabilities (regression tests)", function()

  -- These tests are designed to FAIL against the current (vulnerable) code.
  -- When the corresponding fixes are applied, these tests should PASS.

  -------------------------------------------------------------------------
  -- 1. timeframe() loadstring injection via vname (Other.lua:987)
  --
  -- The code builds a string like: loadstring("vname = value")
  -- A malicious vname such as "x; injected = true; y" will execute
  -- arbitrary code when loadstring compiles and runs it.
  -- Similarly, a string value containing a single quote can break out.
  --
  -- After fix: timeframe() should validate/sanitize vname to be a simple
  -- identifier, and properly escape string values.
  -------------------------------------------------------------------------
  describe("Tests timeframe() loadstring injection via vname", function()
    local oldTempTimer, oldKillTimer

    setup(function()
      oldTempTimer = _G.tempTimer
      oldKillTimer = _G.killTimer
      _G.tempTimer = function(time, code)
        if type(code) == "string" then
          local fn = loadstring(code)
          if fn then fn() end
        elseif type(code) == "function" then
          code()
        end
        return 1
      end
      _G.killTimer = function() end
    end)

    teardown(function()
      _G.tempTimer = oldTempTimer
      _G.killTimer = oldKillTimer
      _G.injected_via_vname = nil
      _G.injected_via_value = nil
      _G.x = nil
      _G.y = nil
    end)

    it("should NOT allow code injection through vname parameter", function()
      -- The malicious vname will cause loadstring to compile:
      --   "x; injected_via_vname = true; y = true"
      -- which executes the injected assignment as a side effect.
      _G.injected_via_vname = nil
      local malicious_vname = "x; injected_via_vname = true; y"
      -- time=0 means the function executes immediately
      timeframe(malicious_vname, 1, nil)
      assert.is_nil(_G.injected_via_vname,
        "VULNERABILITY: timeframe() allowed code injection via vname. " ..
        "A crafted variable name executed arbitrary code through loadstring().")
    end)

    it("should NOT allow code injection through string value with quote breakout", function()
      -- The code wraps string values in single quotes: 'value'
      -- A value containing a quote like: hello'; injected_via_value = true; x='
      -- will produce: vname = 'hello'; injected_via_value = true; x=''
      _G.injected_via_value = nil
      _G.x = nil
      local malicious_value = "hello'; injected_via_value = true; x='"
      timeframe("x", {0, malicious_value})
      assert.is_nil(_G.injected_via_value,
        "VULNERABILITY: timeframe() allowed code injection via string value. " ..
        "A single-quote in the value broke out of the string literal.")
    end)
  end)

  -------------------------------------------------------------------------
  -- 2. setActionCallback loadstring injection (GUIUtils.lua:2558)
  --
  -- The code does: loadstring("return "..func.."(...)")
  -- Passing an arbitrary string like "os.execute('echo pwned')" as the
  -- callback function name will compile and potentially execute it.
  --
  -- After fix: setActionCallback should only accept actual function
  -- references, or validate that the string is a simple function name.
  -------------------------------------------------------------------------
  describe("Tests setActionCallback loadstring injection", function()
    -- These tests verify the core vulnerability in setActionCallback's
    -- loadstring pattern. The actual setActionCallback function wraps
    -- setLabelClickCallback (and other callback functions) at module load
    -- time, making it hard to intercept in tests. Instead, we directly
    -- test the loadstring pattern that setActionCallback uses.

    it("should NOT compile arbitrary code from a string callback argument", function()
      -- The vulnerable code in setActionCallback does:
      --   func = loadstring("return "..func.."(...)")
      -- This is the exact pattern. Let's verify it compiles arbitrary code.
      local malicious = "os.execute('echo pwned')"
      local compiled = loadstring("return " .. malicious .. "(...)")

      -- If loadstring succeeds, the arbitrary code can be compiled and
      -- later executed when the callback fires. This is the vulnerability.
      -- After fix, setActionCallback should reject non-identifier strings
      -- or only accept function references.
      assert.is_nil(compiled,
        "VULNERABILITY: setActionCallback's loadstring pattern compiles arbitrary code. " ..
        "loadstring('return '..func..'(...)') with func='" .. malicious .. "' " ..
        "successfully compiled, meaning it could execute when the callback fires.")
    end)

    it("should NOT allow arbitrary expression execution via callback string", function()
      -- The pattern "return EXPR(...)" evaluates EXPR first, which can have
      -- side effects. For example, (function() injected = true end)() is a
      -- valid expression that executes arbitrary code as a side effect.
      _G.callback_injected = nil
      local malicious = "(function() callback_injected = true end)()"
      local compiled = loadstring("return " .. malicious)

      -- The loadstring should either fail to compile, or even if it compiles,
      -- the setActionCallback should reject non-identifier strings.
      if compiled then
        -- If it compiled, executing it would set callback_injected = true
        compiled()
      end

      assert.is_nil(_G.callback_injected,
        "VULNERABILITY: setActionCallback's loadstring pattern allows arbitrary " ..
        "expression execution. Any Lua expression can be embedded in the callback string.")
      _G.callback_injected = nil
    end)
  end)

  -------------------------------------------------------------------------
  -- 3. table.load() uses dofile() (Other.lua:397)
  --
  -- dofile() executes arbitrary Lua code in a file, not just table data.
  -- A malicious save file can include side-effect code before the return.
  --
  -- After fix: table.load() should use a safe deserialization method
  -- that only loads data, not executable code.
  -------------------------------------------------------------------------
  describe("Tests table.load() arbitrary code execution via dofile", function()
    local tempfile

    setup(function()
      -- Create a temporary file with malicious Lua code followed by
      -- valid table data that dofile() would return
      tempfile = os.tmpname()
      local f = io.open(tempfile, "w")
      f:write('injected_by_dofile = true\n')
      f:write('return { { ["1"] = "hello" } }\n')
      f:close()
    end)

    teardown(function()
      _G.injected_by_dofile = nil
      if tempfile then
        os.remove(tempfile)
      end
    end)

    it("should NOT execute arbitrary code when loading a table file", function()
      _G.injected_by_dofile = nil
      local dest = {}
      table.load(tempfile, dest)
      assert.is_nil(_G.injected_by_dofile,
        "VULNERABILITY: table.load() executed arbitrary code via dofile(). " ..
        "A crafted save file can run any Lua code when loaded.")
    end)
  end)

  -------------------------------------------------------------------------
  -- 4. Zip Slip path traversal in unzip() (LuaGlobal.lua:45,64)
  --
  -- The code does: local _path = dest .. file.filename
  -- A zip entry with filename "../../../etc/evil" would write outside
  -- the intended destination directory.
  --
  -- After fix: unzip() should reject or sanitize filenames containing
  -- "../" path traversal sequences.
  -------------------------------------------------------------------------
  describe("Tests unzip() Zip Slip path traversal", function()
    local oldZip, oldLfs, oldIoOpen, oldCecho
    local writtenPaths

    setup(function()
      writtenPaths = {}

      oldCecho = _G.cecho
      _G.cecho = function() end

      -- Mock the zip library to return a zip with traversal filenames
      oldZip = _G.zip
      _G.zip = {
        open = function(what)
          local files_list = {
            { filename = "safe_file.txt", uncompressed_size = 10 },
            { filename = "../etc/malicious.txt", uncompressed_size = 10 },
            { filename = "subdir/../../outside.txt", uncompressed_size = 10 },
          }
          local idx = 0
          return {
            files = function()
              return function()
                idx = idx + 1
                if idx <= #files_list then
                  return files_list[idx]
                end
                return nil
              end
            end,
            open = function(self, filename)
              return {
                read = function(self, mode) return "malicious content" end,
                close = function() end,
              }
            end,
            close = function() end,
          }
        end,
      }

      oldLfs = _G.lfs
      if not _G.lfs then
        _G.lfs = {}
      end
      _G.lfs.mkdir = function() end

      oldIoOpen = io.open
      io.open = function(path, mode)
        if mode and mode:find("w") then
          writtenPaths[#writtenPaths + 1] = path
          -- Return a mock file handle
          return {
            write = function() end,
            close = function() end,
          }
        end
        return oldIoOpen(path, mode)
      end
    end)

    teardown(function()
      _G.zip = oldZip
      _G.lfs = oldLfs
      io.open = oldIoOpen
      _G.cecho = oldCecho
    end)

    it("should NOT write files with path traversal sequences", function()
      unzip("/fake/package.zip", "/tmp/safe_dest/")

      -- Check that no written path contains ".." traversal
      local traversalFound = false
      for _, path in ipairs(writtenPaths) do
        if path:find("%.%.") then
          traversalFound = true
          break
        end
      end

      assert.is_false(traversalFound,
        "VULNERABILITY: unzip() wrote files with '../' path traversal. " ..
        "Zip entries with traversal sequences can escape the destination directory.")
    end)
  end)

  -------------------------------------------------------------------------
  -- 5. processedEchoToHTML missing HTML escaping (GUIUtils.lua:1279-1338)
  --
  -- The function concatenates text directly into HTML without escaping
  -- special characters like <, >, &. This allows HTML/script injection
  -- when user-controlled text is displayed in labels.
  --
  -- After fix: Text content should have <, >, & escaped to their HTML
  -- entity equivalents before concatenation.
  -------------------------------------------------------------------------
  describe("Tests processedEchoToHTML missing HTML escaping", function()

    it("should escape angle brackets in hecho2html output", function()
      -- hecho uses |cRRGGBB for colors, so <script> in plain text will
      -- pass through the parser as literal text and should be HTML-escaped.
      local result = hecho2html("<script>alert(1)</script>")

      -- The output should NOT contain raw <script> tags.
      -- It should contain &lt;script&gt; instead.
      assert.is_nil(result:find("<script>", 1, true),
        "VULNERABILITY: hecho2html did not escape '<script>' tag. " ..
        "Raw HTML tags in text content can lead to script injection in labels.")
    end)

    it("should escape ampersands in hecho2html output", function()
      -- hecho uses |cRRGGBB for colors, so plain & should pass through
      -- and be escaped in the HTML output.
      local result = hecho2html("Tom & Jerry")

      assert.is_truthy(result:find("&amp;", 1, true),
        "VULNERABILITY: hecho2html did not escape '&' to '&amp;'. " ..
        "Unescaped ampersands can break HTML entity parsing.")
    end)

    it("should escape angle brackets as entities in hecho2html", function()
      -- Verify that < becomes &lt; and > becomes &gt;
      local result = hecho2html("if x > 0 then")

      -- The literal > should be escaped to &gt;
      -- We check that the output contains &gt; somewhere
      assert.is_truthy(result:find("&gt;", 1, true),
        "VULNERABILITY: hecho2html did not escape '>' to '&gt;'. " ..
        "Unescaped angle brackets can inject HTML elements into labels.")
    end)
  end)

  -------------------------------------------------------------------------
  -- 6. GeyserReposition nil arg crash (GeyserReposition.lua:14)
  --
  -- The code does: arg.."Container" == window.name
  -- When arg is nil (which can happen for sysUserWindowResizeEvent),
  -- this concatenation throws "attempt to concatenate a nil value".
  --
  -- After fix: The function should handle nil arg gracefully, either
  -- by defaulting it to an empty string or skipping the comparison.
  -------------------------------------------------------------------------
  describe("Tests GeyserReposition nil arg crash", function()

    setup(function()
      -- Ensure Geyser.windowList has at least one userwindow entry
      -- so the code path that accesses arg is exercised.
      if not Geyser.windowList then
        Geyser.windowList = {}
      end
      Geyser.windowList["testUserWindow"] = {
        type = "userwindow",
        name = "testUserWindowContainer",
        reposition = function() end,
      }
    end)

    teardown(function()
      Geyser.windowList["testUserWindow"] = nil
    end)

    it("should NOT crash when arg is nil for sysUserWindowResizeEvent", function()
      -- This should not throw "attempt to concatenate a nil value".
      -- VULNERABILITY: GeyserReposition crashes with nil arg because
      -- the code does arg.."Container" without checking for nil first.
      assert.has_no.errors(function()
        GeyserReposition("sysUserWindowResizeEvent", 800, 600, nil)
      end)
    end)
  end)

  -------------------------------------------------------------------------
  -- 7. Undefined `cons` in changeContainer (GeyserGeyser.lua:218)
  --
  -- The code: container:add2(self, cons, false)
  -- Here `cons` is not a local or parameter - it's the global `_G.cons`.
  -- This means the window's actual constraints are NOT passed to add2;
  -- instead, whatever random value is in _G.cons gets used.
  --
  -- After fix: changeContainer should pass self (the window's own
  -- constraints) or the window's constraint table to add2.
  -------------------------------------------------------------------------
  describe("Tests undefined cons in changeContainer", function()
    local oldCons

    setup(function()
      oldCons = _G.cons
    end)

    teardown(function()
      _G.cons = oldCons
    end)

    it("should NOT use _G.cons when calling add2 from changeContainer", function()
      -- Set a sentinel value in _G.cons
      local sentinel = { __sentinel = true, name = "SENTINEL_SHOULD_NOT_APPEAR" }
      _G.cons = sentinel

      -- Create two containers so we can move a window between them
      local container1 = Geyser.Container:new({
        name = "secTest_container1",
        x = 0, y = 0, width = "100px", height = "100px",
      })

      local container2 = Geyser.Container:new({
        name = "secTest_container2",
        x = 0, y = 0, width = "100px", height = "100px",
      })

      -- Create a window inside container1
      local window = Geyser.Container:new({
        name = "secTest_movableWindow",
        x = "10px", y = "10px", width = "50px", height = "50px",
      }, container1)

      -- Spy on add2 to see what cons argument it receives
      local add2Spy = spy.on(container2, "add2")

      -- Move the window to container2
      window:changeContainer(container2)

      -- Verify add2 was called
      assert.spy(add2Spy).was.called(1)

      -- The second argument to add2 should be the window's constraints (or nil/self),
      -- NOT the global _G.cons sentinel.
      local call_args = add2Spy.calls[1]
      local cons_arg = call_args.vals[2]

      -- If cons_arg is our sentinel, the bug exists
      local is_sentinel = type(cons_arg) == "table" and cons_arg.__sentinel == true
      assert.is_false(is_sentinel,
        "VULNERABILITY: changeContainer passed _G.cons (a global variable) to add2. " ..
        "The undefined local 'cons' resolves to whatever _G.cons happens to be.")

      -- Clean up
      container2.add2:revert()
      container1:hide()
      container2:hide()
    end)
  end)

  -------------------------------------------------------------------------
  -- 8. tempTimer string eval in flash() (GeyserContainer.lua:306)
  --
  -- The code: tempTimer(time, "hideWindow(\"" .. name .. "\")")
  -- If the container name contains escaped quotes or injected code,
  -- the string will be compiled and executed by tempTimer.
  -- For example, a name like: "); injected = true; hideWindow("
  -- would produce: hideWindow(""); injected = true; hideWindow("")
  --
  -- After fix: flash() should use a function closure instead of a
  -- string with tempTimer to avoid code injection.
  -------------------------------------------------------------------------
  describe("Tests tempTimer string eval injection in flash()", function()
    local oldTempTimer, oldCreateLabel, oldResizeWindow, oldMoveWindow
    local oldSetBackgroundColor, oldEnableClickthrough, oldShowWindow, oldHideWindow
    local capturedTimerArg

    setup(function()
      oldTempTimer = _G.tempTimer
      oldCreateLabel = _G.createLabel
      oldResizeWindow = _G.resizeWindow
      oldMoveWindow = _G.moveWindow
      oldSetBackgroundColor = _G.setBackgroundColor
      oldEnableClickthrough = _G.enableClickthrough
      oldShowWindow = _G.showWindow
      oldHideWindow = _G.hideWindow

      _G.createLabel = function() end
      _G.resizeWindow = function() end
      _G.moveWindow = function() end
      _G.setBackgroundColor = function() end
      _G.enableClickthrough = function() end
      _G.showWindow = function() end
      _G.hideWindow = function() end

      _G.tempTimer = function(time, code)
        capturedTimerArg = code
        -- Simulate what Mudlet's tempTimer does with string arguments:
        -- it compiles and executes them via loadstring
        if type(code) == "string" then
          local fn = loadstring(code)
          if fn then fn() end
        elseif type(code) == "function" then
          code()
        end
        return 1
      end
    end)

    teardown(function()
      _G.tempTimer = oldTempTimer
      _G.createLabel = oldCreateLabel
      _G.resizeWindow = oldResizeWindow
      _G.moveWindow = oldMoveWindow
      _G.setBackgroundColor = oldSetBackgroundColor
      _G.enableClickthrough = oldEnableClickthrough
      _G.showWindow = oldShowWindow
      _G.hideWindow = oldHideWindow
      capturedTimerArg = nil
      _G.flash_injected = nil
    end)

    it("should NOT allow code injection through container name in flash()", function()
      _G.flash_injected = nil

      -- Create a container with a name that will break out of the string
      local malicious_name = 'evil\"); flash_injected = true; hideWindow(\"'
      local container = Geyser.Container:new({
        name = malicious_name,
        x = 0, y = 0, width = "100px", height = "100px",
      })

      -- The flash function builds a name from self.name .. "_dimensions_flash"
      -- and then does: tempTimer(time, "hideWindow(\"" .. name .. "\")")
      -- With our malicious name, this becomes:
      -- hideWindow("evil"); flash_injected = true; hideWindow("_dimensions_flash")
      container:flash(0.1)

      assert.is_nil(_G.flash_injected,
        "VULNERABILITY: flash() allowed code injection via container name. " ..
        "String concatenation in tempTimer call enables arbitrary code execution.")

      container:hide()
    end)
  end)

  -------------------------------------------------------------------------
  -- 9. IDManager access control (IDManager.lua:223)
  --
  -- The IDManager uses the `user` parameter as a namespace key, but
  -- there is no verification that the caller actually belongs to that
  -- package/user. Any code can pass any user string and manipulate
  -- another package's event handlers.
  --
  -- After fix: There should be some form of access control so that
  -- package "A" cannot register/delete handlers belonging to package "B".
  -------------------------------------------------------------------------
  describe("Tests IDManager cross-package access control", function()
    local RESpy, KESpy

    setup(function()
      RESpy = spy.on(_G, "registerAnonymousEventHandler")
      KESpy = spy.on(_G, "killAnonymousEventHandler")
    end)

    teardown(function()
      registerAnonymousEventHandler:revert()
      killAnonymousEventHandler:revert()
      deleteAllNamedEventHandlers("packageA")
      deleteAllNamedEventHandlers("packageB")
    end)

    it("should NOT allow packageB to delete packageA's handlers", function()
      -- Package A registers a handler
      local handlerFunc = function() end
      registerNamedEventHandler("packageA", "myHandler", "testEvent", handlerFunc)

      -- Verify packageA's handler exists
      local handlersA = getNamedEventHandlers("packageA")
      assert.is_equal(1, #handlersA)

      -- Package B tries to delete packageA's handler by using packageA's name
      -- Currently this succeeds because there's no access control
      local result = deleteNamedEventHandler("packageA", "myHandler")

      -- After fix, this should fail because the caller is not packageA.
      -- Currently it succeeds, proving the vulnerability.
      -- We verify the handler was deleted (which it should NOT have been).
      local handlersAfter = getNamedEventHandlers("packageA")

      -- This assertion will FAIL currently (handlers will be empty, meaning
      -- the delete succeeded). After fix, it should PASS (handler preserved).
      assert.is_equal(1, #handlersAfter,
        "VULNERABILITY: No access control on IDManager. " ..
        "Any code can delete another package's handlers by passing its name. " ..
        "There is no caller verification.")
    end)

    it("should NOT allow packageB to overwrite packageA's handlers", function()
      -- Package A registers a handler
      local funcA = function() return "A" end
      registerNamedEventHandler("packageA", "sharedName", "testEvent", funcA)

      -- Package B registers a handler with the SAME user and name
      -- This should fail since B is not A, but currently succeeds
      local funcB = function() return "B" end
      local result = registerNamedEventHandler("packageA", "sharedName", "testEvent", funcB)

      -- The fact that result is true means B successfully overwrote A's handler.
      -- After fix, this should return false or error.
      -- For now, we document that this is a vulnerability.
      -- Since we can't distinguish callers in the test framework either,
      -- we mark this as pending with explanation.
      pending("Cannot distinguish callers in the test framework. " ..
        "VULNERABILITY: Any code can call registerNamedEventHandler with any user string " ..
        "and overwrite another package's handlers. There is no caller authentication.")
    end)
  end)

end)
