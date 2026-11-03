# Errors and saved state

## Errors

Functions that can fail return a `Result<T>`. It holds either the value or an `Error`,
which has an `ErrorCode` and a message. A `Result<T>` converts to true when it holds the
value. `Result<void>` just tells you whether the call succeeded, and holds the error if
it didn't.

```cpp
if (tachyon::Result<void> result = app.Initialize(); !result)
    std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
```

To get at the value, use `Value()`, `operator*`, `operator->` or `ValueOr(fallback)`.
The possible error codes are `Failed`, `InvalidArgument`, `NotFound`, `ParseFailed`,
`Unsupported`, `OutOfMemory` and `DeviceLost`.

`Initialize`, `Run`, `RecordFrame`, `ReadWindowPixels` and `RestoreUIState` all return a
`Result`. To find out how the last `RequestReloadTheme` went, call `ThemeReloadResult()`.


## Saved UI state

The library can save the state of your UI as JSON text, and you store that text
wherever you like. First you tell it, once, what to keep. Each item gets a key that
stays the same between runs, and the objects you name have to outlive that call:

```cpp
PersistWindow("main", mainWindowID);         // position, size, mode and monitor
PersistDocking("dock", dockingLayout);       // panels by type, else title
PersistTable("assets", tableState, columns); // column widths and order
PersistValue("zoom", zoom);                  // an app setting (bool, int, float, string)
```

You can also keep the toolbars with `PersistToolbars`, and the documents of each main
window with `PersistDocuments` (see Documents in
[Tabs, docking and toolbars](../controls/tabs-docking-toolbars.md)). Then save and
restore:

```cpp
std::string json = app.SaveUIState();                        // e.g. after Run()

if (tachyon::Result<void> restored = RestoreUIState(json); !restored)
    std::fprintf(stderr, "UI state not restored: %s\n", restored.GetError().message.c_str());
```

`RestoreUIState` applies everything right away, except windows, which are applied the
next time you declare them. It skips any keys, panels and columns it doesn't recognize.

A window comes back on the monitor it was saved on. If that monitor is gone, the window
goes to the same place on a monitor of the same size, in case the displays were just
rearranged. Failing that, it goes on the primary monitor, resized to fit inside its work
area. If the window's top edge would still be off every monitor, so you couldn't grab or
close it, only its size is restored and the app decides where to put it.

The demo shows the whole thing in use: it calls the `Persist*` functions and
`RestoreUIState` at the end of `Initialize`, and `SaveUIState` after `Run()` returns.

---

[Previous: Return values, content and units](values.md) | [Overview](../overview.md)
