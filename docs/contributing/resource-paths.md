# Resource paths

How to name a theme, font, icon or shader file so it loads on every platform.

## Use the exact case

- Windows and macOS find a file whatever the case of its name; Linux doesn't.
  - For example, `fonts/inter/Inter-Bold.ttf` loads on Windows, but on Linux the folder is
    `fonts/Inter/` and the font is missing.
- So every path names its file exactly as it is spelled in the repository.
  - `git ls-files` lists the names as the repository keeps them:

    ```text
    git ls-files workdirectory/fonts/Inter
    ```

- This holds wherever a path is written down:
  - code: `ResourceFile("icons/lucide/x.svg")`, `RegisterIconFromSVGFile`, font lists
  - themes: `@import` and `url()` in `workdirectory/themes/*.css`
  - CMake: the list of files built into the library in `src/CMakeLists.txt`
  - the guides and code comments
- Don't add a lookup that ignores case on Linux to make a wrong name work. Fix the name.

## What catches a wrong case

- **The path case check.** Before the library opens a theme, font, icon or shader file, it
  compares the path with the names on disk.
  - It's set with `SetPathCaseCheck` (in `gui_core/gui_resources.h`):
    - `Warn`, the default in debug builds, logs an error naming both spellings and opens
      the file.
    - `Fail` logs the error and refuses the file, as Linux would. Both test programs use it,
      so a wrong case fails the tests on Windows and macOS too.
    - `Off`, the default in release builds, doesn't compare, so opening a file costs
      nothing extra.
  - For example, on Windows with `Fail`, `ReadResource("Themes/dark.css")` returns an error:

    ```text
    [resources] Themes/dark.css is spelled themes/dark.css on disk; a file system that
    tells case apart won't find it
    ```

  - The built-in copies of the themes and fonts are found by their exact name on every
    platform too.
- **The source scan.** `PathCase.EveryResourcePathInTheSourceMatchesTheFileOnDisk` in
  `tests/resource_path_case_test.cpp` reads `src/`, `examples/`, `tests/`, `docs/`, `cmake/`
  and the themes.
  - Every path in them that ends in a resource extension (`.css`, `.ttf`, `.otf`, `.svg`,
    `.spv`, `.json` and others) and names a file in `workdirectory/`, ignoring case, must
    name it in its exact case.
  - A path to a file that doesn't exist, such as a test's `icons/no-such-icon.svg`, is left
    alone.
- **No two names that differ only in case.** `PathCase.NoTwoFilesDifferOnlyInCase` fails on
  Linux when, say, `icons/Box.svg` and `icons/box.svg` are both added: a checkout on Windows
  or macOS would keep only one of them.

## Checking an app's own files

- An app that opens files of its own can check them the same way:

  ```cpp
  if (tachyon::CheckPathCase(path))
      LoadMyFile(path);
  ```

- `FindPathCaseMismatch(path)` gives the spelling on disk when only the case differs, and
  is empty otherwise.
