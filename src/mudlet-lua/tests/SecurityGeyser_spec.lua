describe("Tests Geyser framework security vulnerabilities", function()

  -- -----------------------------------------------------------------------
  -- 1. GeyserSetConstraints locale corruption
  -- -----------------------------------------------------------------------
  describe("Tests that Geyser.calc_constraints restores locale after an error", function()
    it("should restore the numeric locale even when calc_constraints errors", function()
      -- Save the current locale before the test
      local originalLocale = os.setlocale(nil, "numeric")

      -- Build minimal window/container tables that calc_constraints expects
      local window = {}
      -- Deliberately pass constraints with a nil value for "x" so the
      -- function hits string.find(nil, ...) and errors out mid-calculation,
      -- after os.setlocale("C") has already been called.
      local badCons = { x = nil, y = "10px", width = "100px", height = "100px" }

      -- The call should error because num becomes nil
      local ok = pcall(Geyser.calc_constraints, window, badCons, nil)

      -- We expect the call to have failed
      assert.is_false(ok, "calc_constraints should have errored on nil constraint")

      -- The locale must be restored to its original value, NOT left as "C".
      -- CURRENT BUG: the locale remains "C" because the error skips the
      -- os.setlocale(oldlocale) cleanup at the end of the function.
      local localeAfterError = os.setlocale(nil, "numeric")
      assert.equals(originalLocale, localeAfterError,
        "Numeric locale was not restored after an error in calc_constraints. " ..
        "Expected '" .. originalLocale .. "' but got '" .. localeAfterError .. "'")

      -- Manually restore locale so subsequent tests are not affected
      os.setlocale(originalLocale, "numeric")
    end)
  end)

  -- -----------------------------------------------------------------------
  -- 2. GeyserAdjustableContainer path traversal in save/load
  -- -----------------------------------------------------------------------
  describe("Tests that Adjustable.Container rejects path traversal in names", function()
    local originalIoExists, originalTableSave, originalTableLoad, originalIoOpen
    local originalLfsMkdir, originalOsRemove
    local capturedSavePath, capturedLoadPath, capturedDeletePath
    local testContainer

    before_each(function()
      capturedSavePath = nil
      capturedLoadPath = nil
      capturedDeletePath = nil

      -- Preserve originals
      originalIoExists = io.exists
      originalTableSave = table.save
      originalTableLoad = table.load
      originalIoOpen = io.open
      originalLfsMkdir = lfs.mkdir
      originalOsRemove = os.remove

      -- Mock io.exists to return false (no existing file)
      io.exists = function() return false end

      -- Mock table.save to capture the path
      table.save = function(path, _tbl)
        capturedSavePath = path
      end

      -- Mock table.load to capture the path and do nothing
      table.load = function(path, _tbl)
        capturedLoadPath = path
      end

      -- Mock lfs.mkdir to do nothing
      lfs.mkdir = function() return true end

      -- Mock os.remove to capture the path
      os.remove = function(path)
        capturedDeletePath = path
        return true
      end
    end)

    after_each(function()
      -- Restore originals
      io.exists = originalIoExists
      table.save = originalTableSave
      table.load = originalTableLoad
      io.open = originalIoOpen
      lfs.mkdir = originalLfsMkdir
      os.remove = originalOsRemove

      if testContainer then
        testContainer:hide()
        testContainer = nil
      end
    end)

    it("should not allow path traversal sequences in save path", function()
      testContainer = Adjustable.Container:new({
        name = "../../etc/evil",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoLoad = false,
        autoSave = false,
      })

      testContainer:save()

      -- The save path should not contain "../" path traversal sequences.
      -- CURRENT BUG: the name is used verbatim in the path, allowing
      -- traversal outside the intended AdjustableContainer directory.
      assert.is_not_nil(capturedSavePath, "save() should have written a file")
      assert.is_nil(
        string.find(capturedSavePath, "%.%./"),
        "Save path contains path traversal sequence '../': " .. capturedSavePath
      )
    end)

    it("should not allow path traversal sequences in load path", function()
      -- Mock io.exists to return true so load() proceeds to table.load
      io.exists = function() return true end

      testContainer = Adjustable.Container:new({
        name = "../../etc/evil",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoLoad = false,
        autoSave = false,
      })

      testContainer:load()

      -- The load path should not contain "../" path traversal sequences.
      -- CURRENT BUG: the name is used verbatim, allowing loading files
      -- from arbitrary locations.
      assert.is_not_nil(capturedLoadPath, "load() should have called table.load")
      assert.is_nil(
        string.find(capturedLoadPath, "%.%./"),
        "Load path contains path traversal sequence '../': " .. capturedLoadPath
      )
    end)

    it("should not allow path traversal sequences in deleteSaveFile path", function()
      -- Mock io.exists to return true so deleteSaveFile proceeds to os.remove
      io.exists = function() return true end

      testContainer = Adjustable.Container:new({
        name = "../../etc/evil",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoLoad = false,
        autoSave = false,
      })

      testContainer:deleteSaveFile()

      -- The delete path should not contain "../" path traversal sequences.
      -- CURRENT BUG: the name is used verbatim, allowing deletion of files
      -- outside the intended directory.
      assert.is_not_nil(capturedDeletePath, "deleteSaveFile() should have called os.remove")
      assert.is_nil(
        string.find(capturedDeletePath, "%.%./"),
        "Delete path contains path traversal sequence '../': " .. capturedDeletePath
      )
    end)
  end)

  -- -----------------------------------------------------------------------
  -- 3. GeyserAdjustableContainer autoLoad defaults to true
  -- -----------------------------------------------------------------------
  describe("Tests that Adjustable.Container autoLoad defaults to true", function()
    local originalIoExists, originalTableLoad
    local loadCalled

    before_each(function()
      loadCalled = false

      originalIoExists = io.exists
      originalTableLoad = table.load

      -- Mock io.exists to return true so load() proceeds
      io.exists = function() return true end

      -- Mock table.load to track if it was called
      table.load = function(_path, _tbl)
        loadCalled = true
      end
    end)

    after_each(function()
      io.exists = originalIoExists
      table.load = originalTableLoad
    end)

    it("should automatically call load() when autoLoad is not explicitly set to false", function()
      -- When autoLoad is not set (nil), it defaults to true and load()
      -- is called during construction. This is a design concern because
      -- it means newly created containers will attempt to load and execute
      -- files from disk without the user explicitly requesting it.
      local ac = Adjustable.Container:new({
        name = "testAutoLoadDefault",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoSave = false,
        -- autoLoad deliberately not set
      })

      assert.is_true(ac.autoLoad,
        "autoLoad should default to true when not explicitly set")
      assert.is_true(loadCalled,
        "load() should have been called during construction when autoLoad is not false")

      ac:hide()
    end)

    it("should NOT call load() when autoLoad is explicitly set to false", function()
      local ac = Adjustable.Container:new({
        name = "testAutoLoadExplicitFalse",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoLoad = false,
        autoSave = false,
      })

      assert.is_false(ac.autoLoad,
        "autoLoad should be false when explicitly set")
      assert.is_false(loadCalled,
        "load() should not have been called during construction when autoLoad is false")

      ac:hide()
    end)
  end)

  -- -----------------------------------------------------------------------
  -- 4. Adjustable.Container:load() chains to table.load() which uses dofile()
  -- -----------------------------------------------------------------------
  describe("Tests that Adjustable.Container:load() executes files via table.load/dofile", function()
    local originalIoExists, originalTableLoad
    local capturedLoadPath

    before_each(function()
      capturedLoadPath = nil

      originalIoExists = io.exists
      originalTableLoad = table.load

      -- Mock io.exists to return true so load() proceeds
      io.exists = function() return true end

      -- Mock table.load to capture the path it receives
      table.load = function(path, _tbl)
        capturedLoadPath = path
      end
    end)

    after_each(function()
      io.exists = originalIoExists
      table.load = originalTableLoad
    end)

    it("should call table.load with the constructed file path", function()
      local ac = Adjustable.Container:new({
        name = "testTableLoadPath",
        x = 0, y = 0,
        width = "100px", height = "100px",
        autoLoad = false,
        autoSave = false,
      })

      ac:load()

      -- Verify table.load was called and received the expected path
      assert.is_not_nil(capturedLoadPath,
        "table.load should have been called by Adjustable.Container:load()")
      assert.is_truthy(
        string.find(capturedLoadPath, "testTableLoadPath%.lua$"),
        "table.load should receive a path ending with the container name and .lua extension, got: " .. tostring(capturedLoadPath)
      )
    end)

    it("should be aware that table.load uses dofile, executing arbitrary Lua code", function()
      -- table.load (defined in Other.lua) uses dofile() to load the saved
      -- file. This means any Lua code in the file will be executed, not
      -- just table data. If an attacker can control the contents of the
      -- save file (e.g., via path traversal or file replacement), they
      -- can achieve arbitrary code execution.
      --
      -- This test documents the vulnerability by verifying that table.load
      -- is indeed the function called by Adjustable.Container:load().
      pending("table.load uses dofile() which executes arbitrary Lua code from the loaded file - " ..
              "a sandboxed loader (e.g., load() with restricted environment) should be used instead")
    end)
  end)
end)
