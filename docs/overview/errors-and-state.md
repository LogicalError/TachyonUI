# Errors and saved state

## Errors

Functions that can fail return `Result<T>`: the value or an `Error` (an `ErrorCode` and
a message). It converts to true when it holds the value.

```cpp
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

- Value access: `Value()`, `operator*`, `operator->`, `ValueOr(fallback)`.
- Error codes: `Failed`, `InvalidArgument`, `NotFound`, `ParseFailed`, `Unsupported`,
  `OutOfMemory`, `DeviceLost`.
- Return a `Result`: `Initialize`, `Run`, `RecordFrame`, `ReadWindowPixels`,
  `RestoreUIState`.
- `ThemeReloadResult()` gives the outcome of the last `RequestReloadTheme`.


## Saved UI state

UI state is saved as JSON text that you store. Register once what to keep. Keys must stay
the same between runs; the named objects must outlive the call.

```cpp
PersistWindow("main", mainWindowID);         // position, size, mode and monitor
PersistDocking("dock", dockingLayout);       // panels by type, else title
PersistTable("assets", tableState, columns); // column widths and order
PersistValue("zoom", zoom);                  // an app setting (bool, int, float, string)
```

Also `PersistToolbars`, and `PersistDocuments` for each main window's documents (see
Documents in [Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md)). Save
and restore:

```cpp
std::string json = app.SaveUIState();                        // e.g. after Run()

if (tachyon::Result<void> restored = RestoreUIState(json); !restored)
    std::fprintf(stderr, "UI state not restored: %s\n", restored.GetError().message.c_str());
```

`RestoreUIState` applies everything at once, except windows, which apply the next time
you declare them. Unknown keys, panels and columns are skipped.

Window placement on restore, in order:

1. The monitor it was saved on.
2. If that is gone, the same place on a monitor of the same size.
3. Otherwise the primary monitor, resized to fit its work area.
4. If the top edge would still be off every monitor, only the size is restored.

The demo calls the `Persist*` functions and `RestoreUIState` at the end of `Initialize`,
and `SaveUIState` after `Run()` returns.

---

[Previous: Return values, content and units](values.md) | [Overview](../overview.md)
