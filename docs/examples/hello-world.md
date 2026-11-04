# Hello world

One window with a text field and three buttons: the smallest complete TachyonUI program.
Full source: [examples/hello_world/hello_world.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/hello_world/hello_world.cpp).

## The application class

- A TachyonUI program is a class derived from `tachyon::GUIApplication`:
  - `GUIApplication` owns the windows and runs the frame loop
  - your class adds the UI and the values it shows
- Set up anything that lasts between frames in `Initialize`, after calling the base
  class's `Initialize`.

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

- Every control and window has an ID. The library uses it to find the control's state
  again in the next frame, such as whether it has focus.
- A control makes its ID from the place in your code it is called from, so you don't pass
  one.
- A window needs a static ID:
  - create it once with `idManager.CreateStaticID()` and keep it in a member
  - the library stores the window's position and size by that ID, so it must stay the same
    from frame to frame
- More about IDs: [IDs](../overview/ids.md).

## Describing the UI

- `OnGUI` runs every frame and describes the whole UI. The library lays out and draws what
  it describes.
- The values the UI shows live in your class. Here they are the members `name` and
  `greeting`.
- The library keeps the rest:
  - which control has focus
  - the text cursor
  - each window's position and size

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

- `BeginWindow` returns whether to build the window's content.
  - Call `EndWindow` whether it returned true or not.
  - The same goes for every `Begin*` function; see [Begin and End](../overview/begin-end.md).
- `Field(name)` edits the value in place. The value's type picks the editor:
  - `bool`: a toggle, which also takes the text to show beside it
  - `float`: a number field
  - `float3`: three number fields

  ```cpp
  // Members of the app
  bool            visible  = true;
  float           speed    = 1.0f;
  tachyon::float3 position { 0.0f, 0.0f, 0.0f };

  // In OnGUI
  Field(visible, "Visible");   // a toggle, with its label
  Field(speed);                // a number field
  Field(position);             // three number fields
  ```

- Button callbacks run after the frame, so they can change anything the next frame reads.
- Sizes such as `Size(360, 160)` are in CSS px:
  - they grow with the UI scale, which follows the OS display scale
  - at 150%, `Size(360, 160)` is 540 x 240 pixels
  - see [Return values, content and units](../overview/values.md)

## The program

- `main` creates the app, then calls `Initialize`, `Run` and `Cleanup`.
- `Run` returns when the last window closes.
- No resource files (themes, fonts) are needed next to the program.

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

- `Initialize` and `Run` return `Result<void>`:
  - it converts to true on success
  - on failure, `GetError().message` says why, for example that the GPU lacks a feature
  - see [Errors and saved state](../overview/errors-and-state.md)

---

[Examples](../examples.md) | [Next: Controls](controls.md)
