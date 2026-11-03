# Hello world

One window with a text field and three buttons.
Full source: [examples/hello_world/hello_world.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/hello_world/hello_world.cpp).

## The application class

Derive from `tachyon::GUIApplication`. Set up anything that lasts between frames in
`Initialize`, after calling the base class:

```cpp
class HelloApp : public tachyon::GUIApplication
{
public:
    tachyon::Result<void> Initialize(const InitializeOptions& options = {}) override
    {
        if (tachyon::Result<void> result = GUIApplication::Initialize(options); !result)
            return result;
        // Windows need IDs that stay the same from frame to frame.
        windowID = idManager.CreateStaticID();
        return {};
    }
```

Controls take their ID from the call site. Windows need a static ID, created once,
because the library stores their position and size by ID. See [IDs](../overview/ids.md).

## Describing the UI

`OnGUI` runs every frame and describes the whole UI. The app's members (`name`,
`greeting`) hold its state; the library keeps focus, text cursor and window rect.

```cpp
void OnGUI() override
{
    if (BeginWindow(windowID, "Hello", tachyon::LayoutOverride{}.Size(360, 160)))
    {
        // A window lays its children out in wrapping rows; stack them instead.
        BeginVertical(tachyon::LayoutOverride{}.FlexGrow(1));
        Text("Hello from TachyonUI!");

        BeginLabelGroup();
        Label("Name");
        Field(name);
        EndLabelGroup();

        BeginHorizontal();
        Button("Greet",  [this] { greeting = "Hello, " + name + "!"; });
        Button("Clear",  [this] { greeting.clear(); });
        Button("Quit",   [this] { applicationQuit = true; });
        EndHorizontal();

        Text(greeting);
        EndVertical();
    }
    EndWindow();
}
```

- Call `EndWindow` whether or not `BeginWindow` returned true; the return value only says
  whether to build the content. Same for every `Begin*` (see
  [Begin and End](../overview/begin-end.md)).
- `Field(name)` edits in place. The value's type picks the editor: `bool` a toggle,
  `float` a number field, `float3` three.
- Button callbacks run after the frame, so they can change anything the next frame reads.
- Sizes such as `Size(360, 160)` are CSS px and scale with the UI.

## The program

`Run` returns when the last window closes. No resource files are needed.

```cpp
int main()
{
    HelloApp app;
    tachyon::Result<void> result = app.Initialize();
    if (result)
        result = app.Run();   // until the last window closes
    app.Cleanup();
    if (!result)
    {
        std::fprintf(stderr, "%s\n", result.GetError().message.c_str());
        return 1;
    }
    return 0;
}
```

`Initialize` and `Run` return `Result<void>`; on failure it says why (for example, missing
GPU features). See [Errors and saved state](../overview/errors-and-state.md).

---

[Examples](../examples.md) | [Next: Controls](controls.md)
