# Changelog

## 1.4.5

- Follows the program language (ten languages).
- Affix lines read as the game words them in each language, and the affix filter finds affixes by their translated text.

## 1.4.4

- Works with the separate menu window (POEFixer 366): the host hands the plugin the right drawing context for its settings tab and for its overlay.
- The overlay toggle key no longer fires while you type into a POEFixer text field in a separate menu window.

## 1.4.3

- Rebuilt with the updated plugin SDK headers (POEFixer 365 adds optional calls for UI control names, stash objects and tabs, wider entity scans and the host automation flag). No change in what the plugin does.

## 1.4.2

- The "?" next to a section title now sits in its own gap instead of on the section's line.
- Diagnostics now looks the same as in the other POEFixer plugins.
- The bottom of the settings tab names the plugin's original author, as in the other plugins maintained by POEFixer.

## 1.4.1

- Now maintained by POEFixer; the affix table is built in, so no data file is needed next to the plugin.
- Waystones are found and read in the background, off the game frame: no periodic stutter with a full stash tab, and no reading at all while you are away from the game.
- Badges update right after you use currency on a waystone, and follow stash tab switches faster.
- Map-stat badges keep working after a game patch: the plugin re-detects the stat numbers from your waystones.
- Fixed a possible crash when turning "Read item mods" off while a stash was open.
- Badges stay inside their waystone, scale with the overlay font and sit behind the Fixer menu while it is open.
- Border groups can be moved up and down (the first matching group wins) and show a summary of their conditions.
- Redesigned settings tab and hover panel; settings are saved as soon as you change them, and safely; a damaged or locked settings file is kept instead of being replaced by the defaults.

## 1.4.0 and earlier

Released by the original author, Ömer Faruk ARPA
(https://github.com/omrfarukarpa/poefixer-waystone-helper).
