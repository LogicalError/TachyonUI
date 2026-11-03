# Hello world

This is the smallest complete TachyonUI program: one window with a text field and three
buttons.
Full source: [examples/hello_world/hello_world.cpp](https://github.com/LogicalError/TachyonUI/blob/main/examples/hello_world/hello_world.cpp).

## The application class

Your application derives from `tachyon::GUIApplication`. Anything that has to last
between frames gets set up in `Initialize`, after the base class has created the window
manager, the text system and the theme:

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

Most controls take their ID from the place they're called, so you don't have to give
them one. Windows are different. The library remembers a window's position and size by
its ID, so a window gets a static ID that you create once. [IDs](../overview/ids.md)
explains the different kinds.

## Describing the UI

`OnGUI` runs every frame and describes the whole UI. There's no tree of widget objects to
keep in sync. The app's own members (`name` and `greeting`) hold its state, and the
library keeps track of the rest, such as focus, the text cursor and the window's rect.

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

A few things worth noticing:

- `EndWindow` is called whether or not `BeginWindow` returned true. The return value
  only tells you whether to build the content. Every `Begin*` call works this way (see
  [Begin and End](../overview/begin-end.md)).
- `Field(name)` edits the string in place. The type of the value decides which editor
  you get, so a `bool` gets a toggle, a `float` gets a number field and a `float3` gets
  three of them.
- A button's callback runs after the frame, when input is handled. That means it can
  safely change anything the next frame reads.
- Sizes such as `Size(360, 160)` are in CSS px and scale with the UI.

## The program

`main` runs the app until the last window closes. You don't need any resource files,
because the shaders, the default theme and the fonts are built into the library.

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

`Initialize` and `Run` both return a `Result<void>`. If they fail, it tells you why, for
example when the GPU doesn't have the features the library needs. See
[Errors and saved state](../overview/errors-and-state.md) for the details.

---

[Examples](../examples.md) | [Next: Controls](controls.md)
