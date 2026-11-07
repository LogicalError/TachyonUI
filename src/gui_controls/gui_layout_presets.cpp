#include <gui_controls/gui_layout_presets.h>
#include <gui_core/gui_json.h>

#include <algorithm>

namespace tachyon
{
    namespace
    {
        std::string Lowercase(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return out;
        }

        std::string StringOf(const JsonValue& object, const char* name)
        {
            const JsonValue* v = object.Find(name);
            return v && v->IsString() ? v->AsString() : std::string();
        }
    }

    void LayoutPresets::AddBuiltIn(std::string name, std::string tree)
    {
        for (auto it = m_presets.begin(); it != m_presets.end(); ++it)
            if (it->name == name)
            {
                if (it->builtIn) { it->tree = std::move(tree); return; }
                m_presets.erase(it);   // a saved one of that name gives way
                break;
            }
        const auto at = std::find_if(m_presets.begin(), m_presets.end(), [](const LayoutPreset& p) { return !p.builtIn; });
        m_presets.insert(at, LayoutPreset{ std::move(name), std::move(tree), true });
    }

    bool LayoutPresets::Save(std::string name, std::string tree)
    {
        if (name.empty())
            return false;
        for (LayoutPreset& preset : m_presets)
            if (preset.name == name)
            {
                if (preset.builtIn)
                    return false;
                preset.tree = std::move(tree);
                return true;
            }
        m_presets.push_back(LayoutPreset{ std::move(name), std::move(tree), false });
        return true;
    }

    bool LayoutPresets::Remove(std::string_view name)
    {
        const auto it = std::find_if(m_presets.begin(), m_presets.end(), [name](const LayoutPreset& p) { return p.name == name; });
        if (it == m_presets.end() || it->builtIn)
            return false;
        m_presets.erase(it);
        std::erase_if(m_fileTypes, [name](const auto& type) { return type.second == name; });
        return true;
    }

    const LayoutPreset* LayoutPresets::Find(std::string_view name) const
    {
        for (const LayoutPreset& preset : m_presets)
            if (preset.name == name)
                return &preset;
        return nullptr;
    }

    void LayoutPresets::SetForFileType(std::string_view extension, std::string name)
    {
        const std::string key = Lowercase(extension);
        const auto it = std::find_if(m_fileTypes.begin(), m_fileTypes.end(), [&key](const auto& type) { return type.first == key; });
        if (name.empty())
        {
            if (it != m_fileTypes.end())
                m_fileTypes.erase(it);
            return;
        }
        if (it != m_fileTypes.end())
            it->second = std::move(name);
        else
            m_fileTypes.emplace_back(key, std::move(name));
    }

    std::string_view LayoutPresets::NameForFileType(std::string_view extension) const
    {
        const std::string key = Lowercase(extension);
        for (const auto& [type, name] : m_fileTypes)
            if (type == key)
                return name;
        return {};
    }

    const LayoutPreset* LayoutPresets::ForFileType(std::string_view extension) const
    {
        const std::string_view name = NameForFileType(extension);
        return name.empty() ? nullptr : Find(name);
    }

    std::string LayoutPresets::ToJson() const
    {
        JsonValue doc = JsonValue::MakeObject();
        JsonValue saved = JsonValue::MakeArray();
        for (const LayoutPreset& preset : m_presets)
        {
            if (preset.builtIn)
                continue;
            JsonValue p = JsonValue::MakeObject();
            p.Set("name", preset.name);
            p.Set("layout", preset.tree);
            saved.Push(std::move(p));
        }
        doc.Set("presets", std::move(saved));
        JsonValue types = JsonValue::MakeArray();
        for (const auto& [extension, name] : m_fileTypes)
        {
            JsonValue t = JsonValue::MakeObject();
            t.Set("extension", extension);
            t.Set("layout", name);
            types.Push(std::move(t));
        }
        doc.Set("fileTypes", std::move(types));
        return doc.Write();
    }

    bool LayoutPresets::FromJson(std::string_view json)
    {
        Result<JsonValue> parsed = JsonValue::Parse(json);
        if (!parsed || !parsed->IsObject())
            return false;
        std::erase_if(m_presets, [](const LayoutPreset& p) { return !p.builtIn; });
        m_fileTypes.clear();
        if (const JsonValue* saved = parsed->Find("presets"); saved && saved->IsArray())
            for (const JsonValue& p : saved->Items())
                if (p.IsObject())
                    (void)Save(StringOf(p, "name"), StringOf(p, "layout"));
        if (const JsonValue* types = parsed->Find("fileTypes"); types && types->IsArray())
            for (const JsonValue& t : types->Items())
                if (t.IsObject() && !StringOf(t, "extension").empty())
                    SetForFileType(StringOf(t, "extension"), StringOf(t, "layout"));
        return true;
    }
}
