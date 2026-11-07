#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tachyon
{
    /// @addtogroup controls_docking
    /// @{

    /// A named arrangement of a document's panels: a dock tree as
    /// GUIApplication::SaveDockLayout writes it.
    struct LayoutPreset
    {
        std::string name;              ///< "Modeling"
        std::string tree;              ///< SaveDockLayout's text
        bool        builtIn = false;   ///< the app's own: not saved, not deleted or replaced
    };

    /// The layouts a document can be given: the app's built-in ones, then the ones the user
    /// saved, and which one each file type opens with.
    ///
    ///     presets.AddBuiltIn("Default", app.SaveDockLayout(defaultLayout));
    ///     presets.Save("My layout", app.SaveDockLayout(document.layout));
    ///     presets.SetForFileType(".mat", "My layout");
    ///     if (const LayoutPreset* preset = presets.ForFileType(document.extension))
    ///         app.ApplyDockLayout(document.layout, preset->tree, makePanel);
    ///
    /// GUIApplication::PersistLayoutPresets keeps the saved ones and the file types.
    class LayoutPresets
    {
    public:
        /// Adds one of the app's own layouts, after the other built-in ones; one of the
        /// same name is replaced.
        void AddBuiltIn(std::string name, std::string tree);
        /// Saves a user layout, after the others; one of the same name is replaced. False
        /// for an empty name or a built-in one's.
        bool Save(std::string name, std::string tree);
        /// Deletes a user layout, and the file types that opened with it. False when there's
        /// none of that name, or it's built in.
        bool Remove(std::string_view name);
        /// The layout of that name; null when there's none.
        const LayoutPreset* Find(std::string_view name) const;
        /// All of them: the built-in ones, then the saved ones, each in the order added.
        const std::vector<LayoutPreset>& All() const { return m_presets; }

        /// Files of that type (".mat", any case) open with the layout of that name; an empty
        /// name stops them.
        void SetForFileType(std::string_view extension, std::string name);
        /// The name of the layout files of that type open with; empty when none is set.
        std::string_view NameForFileType(std::string_view extension) const;
        /// The layout files of that type open with; null when none is set, or it's gone.
        const LayoutPreset* ForFileType(std::string_view extension) const;
        /// Each file type with a layout, as (extension, layout name), in the order set.
        const std::vector<std::pair<std::string, std::string>>& FileTypes() const { return m_fileTypes; }

        /// The saved layouts and the file types as JSON text; the built-in ones aren't in it.
        std::string ToJson() const;
        /// Takes the saved layouts and the file types from ToJson's text, in place of the
        /// ones there are; the built-in ones stay, and saved ones of their names are skipped.
        /// False (nothing changes) when the text isn't a JSON object.
        bool FromJson(std::string_view json);

    private:
        std::vector<LayoutPreset>                        m_presets;     // built-in ones first
        std::vector<std::pair<std::string, std::string>> m_fileTypes;   // extension lowercased
    };

    /// @}
}
