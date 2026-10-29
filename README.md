# Waystone Helper

Current version: 1.4.5

Requires POE2Fixer 364 or newer.

Waystone Helper marks **waystones** in every open item window (inventory, stash,
guild stash) so you can judge a map at a glance without hovering each one.

## What you see

- **Affix-count badge** (top left): the number of explicit affixes, shown once the
  waystone has at least the count you choose (0-8). Set it to 0-2 to also mark
  normal and magic waystones.
- **Map-stat badges** (top right): the waystone's totals of the map stats you
  tick, each in its own colour with a short code:
  `E` Monster Effectiveness, `R` Item Rarity, `P` Monster Pack Size,
  `MR` Monster Rarity, `W` Waystone Drop Chance. `R65` = 65% more Rarity of Items.
- **Affix-group badges** (left side): your own sets of waystone affixes. A group
  matches when a waystone has at least the chosen number of them and shows
  `matched/selected`, e.g. `2/3`, in the group's colour.
- **Coloured borders**: border groups with conditions on map-stat totals
  (e.g. Item Rarity at least 60), tier, affix count, corruption and rarity. A
  waystone gets the border of the first group whose conditions all hold.
- **Hover breakdown**: hover a marked waystone to see its affix count, map stats,
  matching affix groups and the border group that coloured it.
- **Overlay hotkey**: one key hides or shows every badge at once, for example to
  see the game's own highlight while using currency on maps.

Badges always stay inside their waystone's cell and drop behind the Fixer menu
while it is open.

## Settings

Install and update Waystone Helper from the **Plugin Manager** (the first sub-tab of the
Plugins tab), then tick **Enabled** in its tab.

The plugin's tab has a status card on top, three sub-tabs and a Diagnostics card.

**Display**
- *Toggle hotkey*: click, then press the key or mouse button you want (Esc cancels);
  *Clear* removes it.
- *Badge size*: badge text size relative to the overlay font.
- *Badge background*: the fill behind every badge (lower the alpha to see more of the item).
- *Refresh every*: how often the open windows are checked (lower follows tab
  switches faster, higher does less work).
- *Hide badges on the hovered waystone*: keeps the item under the cursor clean.
- *Show the breakdown panel on hover*.
- *Affix count*: show the badge, from which count, and its colour.
- *Map stats*: show the badges, which stats, and the colour of each.

**Border groups**: turn borders on or off, set the thickness, and add groups. Each
group has an on/off switch, a colour, a name, its conditions (anything left at
*Any* is ignored), and buttons to move it up or down (order decides which group
wins) or delete it. A group without conditions never matches.

**Affix groups**: turn matching and the badges on or off, and add groups. Pick the
affixes of a group from the Prefix and Suffix lists (the filter box searches the
full affix text) and choose how many of them a waystone needs.

**Diagnostics** (collapsed): what the last refresh found and how long it took,
the *Read item mods* switch (off: only tier, rarity and corruption conditions
work), the map-stat keys, and *Write dump of visible waystones*, which writes
every mod and stat of the waystones on screen to `debug\waystone-dump.txt` for
bug reports.

**Reset display** (in the status card) restores badge, colour and overlay defaults and
keeps your groups and hotkey.

## Good to know

- Map-stat totals come from the game's aggregated item stats. A game patch can
  move the numbers the game uses for these stats; the plugin then finds the new
  ones from the waystones you look at and switches to them by itself (it needs a
  few waystones carrying each stat). The Diagnostics card shows the state.
- The affix table is built into the plugin. The `config\waystone_data.json` file
  older versions needed is no longer read and can be deleted.
- Settings live in `config\settings.json` next to the plugin; settings from
  Waystone Helper 1.x are kept. A damaged file is set aside as
  `settings.json.bad` and defaults are used.
- The plugin's own *Enable* switch and the *Debug mode* option of older versions
  are gone: use the Enabled checkbox of the plugin tab, and the dump button in
  Diagnostics.
- Languages: the plugin follows the program language. A group added with the
  *Add border group* or *Add affix group* button is named in the language of
  that moment ("Group 2", "Группа 2") and keeps that name after a language
  switch, like any name you type.

## Install

Install it from POE2Fixer → Plugins → Plugin Manager (one click, updates included), or download `WaystoneHelper.zip` from [Releases](https://github.com/POEFixer/WaystoneHelper/releases/latest) and extract it into the POE2Fixer folder (it creates `Plugins\WaystoneHelper\`). Then enable the plugin in the Plugins tab.

## Build from source

This repository is the plugin folder from the POE2Fixer source tree (`Plugins/WaystoneHelper/`) together with the host files it compiles against, kept at the same relative paths:

- `POEFixer/plugin_sdk/` - the POE2Fixer Plugin SDK v6 headers (`PluginAbi.h`, `PluginSDK.h`)
- `POEFixer/imgui/` - the Dear ImGui sources the DLL compiles against
- `POEFixer/core/` - small self-contained helper headers used by the plugin
- `tools/plugin_i18n/gen.py` - the translation tool: after editing `Plugins/WaystoneHelper/lang/en.json` run `python tools/plugin_i18n/gen.py Plugins/WaystoneHelper` (it rewrites the embedded English header); `--gate` checks every language file and the keys the sources use

1. Install Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v143, Windows 10/11 SDK).
2. Open `WaystoneHelper.sln`, select **Release | x64** and build the solution.
3. The plugin is written to `x64\Release\Plugins\WaystoneHelper\WaystoneHelper.dll` (Release builds carry no debug information, see `Directory.Build.targets`).
4. Copy `WaystoneHelper.dll` into `Plugins\WaystoneHelper\` next to the POE2Fixer executable, together with `Plugins\WaystoneHelper\lang` from this repository (same folder names).

## Credits

Originally created by Ömer Faruk ARPA
(https://github.com/omrfarukarpa/poefixer-waystone-helper).
Maintained by POEFixer since October 2026.

Licensed under GPL-3.0, see LICENSE.
