# Editor language catalogs

Lucid Engine editor translations are stored as UTF-8 JSON catalogs in this directory. The editor loads the selected locale at runtime; English is the canonical source language and fallback.

## Catalogs

- `en.json` — English catalog and fallback.
- `pl.json` — Polish catalog.

## Structure

Each catalog contains locale metadata and a `translations` object keyed by stable identifiers:

```json
{
  "locale": "pl",
  "name": "Polski",
  "fallback": "en",
  "translations": {
    "ui.file": "Plik",
    "ui.editor_preferences": "Preferencje edytora..."
  }
}
```

Use stable, namespaced keys such as `ui.*`, `settings.*`, `script.*`, and `details.*`. Keys must be identical across all locale catalogs. Never use translated wording as a key: changing a translation must not require editing C++.

C++ UI code should use `editorText("stable.key", "English fallback")`. If a key is absent from the selected catalog, the English fallback passed by the call site is displayed. If the selected catalog cannot be loaded, the localization system attempts to load `en.json`.

## Runtime deployment

CMake copies the complete `language` directory beside the engine executable as part of the normal build. Keep the directory name `language` and use the locale ID as the filename, for example `fr.json` for French.
