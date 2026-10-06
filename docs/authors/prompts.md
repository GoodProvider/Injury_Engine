# Prompts

Edit the SkyrimNet prompt files shipped with this mod.

Contracts: [../reference/json-keys.md](../reference/json-keys.md).

## Paths

```
SKSE/Plugins/SkyrimNet/external/goodprovider.whipped/
  manifest.json            (id must equal the folder name)
  prompts/
    submodules/
      character_bio/
        0420_injury_looks.prompt
      user_final_instructions/
        0420_injury_feels.prompt
```

| File | Role |
|------|------|
| `character_bio/0420_injury_looks.prompt` | What each wounded part looks like to an observer, for every injury source |
| `user_final_instructions/0420_injury_feels.prompt` | How the speaker's own wounds feel, for every injury source |

## Injury text lives in JSON, not in prompts

The two prompts are generic: they loop over `skyrimnet_whipped_injuries(uuid)` (`sources[]`, each with `looks_heading`, `feels_intro`, `feels_outro` and `parts[]` with ready-to-print `look` / `feel` strings, already resolved for the actor's name, pronouns and part). The wording, the stage tables and all narration (hit text, healing text, healing-source text, Whip_Target summary) are in `SKSE/Plugins/SkyrimNet_Whipped/injuries/<source>.json` and rendered by `SkyrimNet_Whipped.dll`; the JSON schema and writing rules are in `SKSE/Plugins/SkyrimNet_Whipped/injuries/guide.md`. Add a new injury type by adding a JSON file; the prompts need no change. Narration is no longer rendered through `RenderTemplate` / `ParseString`, so the inja limits noted in [KNOWLEDGEBASE.md](../../KNOWLEDGEBASE.md) apply only to these two small prompts.

## No numbers

LLM-facing text — prompts, DirectNarration, events — uses descriptive stages ("light welts", "about half way healed"), never digits or percentages. Numeric JSON fields exist only for templates to choose words.

## Checklist

- [ ] Keys bare lowercase
- [ ] No digits / percentages reach the LLM
- [ ] Every `looks` / `feels` region array in the JSON has one entry per stage of that source
- [ ] Narration-enabled smoke test in game (check `Papyrus.0.log` DirectNarration lines and `SkyrimNet_Whipped.log` `injuries:` lines)
