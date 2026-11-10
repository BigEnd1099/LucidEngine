# Editor language catalogs

Lucid Engine editor UI translations are stored as UTF-8 JSON files in this directory.

## Files

- `en.json` — canonical English catalog and fallback.
- `pl.json` — Polish translation.

## Format

Each catalog contains locale metadata and a `translations` object:

```json
{
  "locale": "pl",
  "name": "Polski",
  "fallback": "en",
  "translations": {
    "File": "Plik",
    "Editor Preferences...": "Preferencje edytora..."
  }
}
```

The current migration layer uses the English source phrase as the lookup key so existing editor panels can be moved to external catalogs incrementally. New localization work should keep the English phrase as the key in every catalog and provide a translated value for each locale.

If a key is missing, the UI falls back to its English source string (or the legacy Polish fallback while older panel code is being migrated). If a locale file cannot be loaded, the editor falls back to English.

CMake copies this directory beside the engine executable during the post-build step. Keep the directory name `language` and the locale filename format `<locale>.json`.
