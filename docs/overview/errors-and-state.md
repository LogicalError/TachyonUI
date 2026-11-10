# Errors and saved state

## Errors

Functions that can fail return `Result<T>`:

- It holds either the value or an `Error`.
- An `Error` has an `ErrorCode` and a message.
- It converts to true when it holds the value.

```cpp
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

- Value access: `Value()`, `operator*`, `operator->`, `ValueOr(fallback)`.
- Error codes: `Failed`, `InvalidArgument`, `NotFound`, `ParseFailed`, `Unsupported`,
  `OutOfMemory`, `DeviceLost`.
- These return a `Result`:
  - `Initialize`
  - `Run`
  - `RecordFrame`
  - `ReadWindowPixels`
  - `RestoreUIState`
- `ThemeReloadResult()` gives the outcome of the last `RequestReloadTheme`:

  ```cpp
  if (const Result<void>& reload = ThemeReloadResult(); !reload)
      Text(reload.GetError().message);
  ```


## Saved UI state

The library saves UI state as JSON text, and you store that text where you like. Register
once what to keep:

- Each thing is registered under a key, a string you choose. Keys must stay the same
  between runs.
- The objects you name must outlive the call.

```cpp
PersistWindow("main", mainWindowID);         // position, size, mode and monitor
PersistDocking("dock", dockingLayout);       // panels by type, else title
PersistTable("assets", tableState, columns); // column widths and order
PersistValue("zoom", zoom);                  // an app setting (bool, int, float, string)
```

There is also:

- `PersistToolbars`
- `PersistThemeVariables`, for the changes made to each palette with `SetThemeVariable`:

  ```cpp
  PersistThemeVariables("themeVariables");
  ```

- `PersistShortcuts`, for the shortcuts the user changed. Register the commands before
  `RestoreUIState`; the others keep the shortcuts the app gives them:

  ```cpp
  PersistShortcuts("shortcuts", commandManager);
  ```

- `PersistDocuments`, for each main window's documents (see Documents in
  [Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md))
- `PersistLayoutPresets`, for the layout presets the user saved and the file types' presets
  (see Layout presets in [Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md)):

  ```cpp
  PersistLayoutPresets("layouts", presets);
  ```

Save and restore:

```cpp
std::string json = app.SaveUIState();                        // e.g. after Run()

if (tachyon::Result<void> restored = RestoreUIState(json); !restored)
    std::fprintf(stderr, "UI state not restored: %s\n", restored.GetError().message.c_str());
```

- `RestoreUIState` applies everything at once, except windows. Those apply the next time
  you declare them.
- Unknown keys, panels and columns are skipped.

Window placement on restore, in order:

1. The monitor it was saved on.
2. If that is gone, the same place on a monitor of the same size.
3. Otherwise the primary monitor, resized to fit its work area.
4. If the top edge would still be off every monitor, only the size is restored.

The demo calls the `Persist*` functions and `RestoreUIState` at the end of `Initialize`,
and `SaveUIState` after `Run()` returns.

---

[Previous: Return values, content and units](values.md) | [Overview](../overview.md)
