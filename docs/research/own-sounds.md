# Making and keeping your own sounds

Preliminary research for TODO 24, 2026-11-20, for the user to discuss. How CoolSynth handles a player's own
sounds today, how other plugins (and briefly hardware) do it, and options to choose from. Nothing is decided.

## CoolSynth today

### Starting a sound

There is no blank or "Init" sound to start from.
- A new instance, the app's first start and Prefs' "Reset the sound..." all give Reese on layer A, layer B
  empty (`startSoundName`, D198, D199). Reset also sets the faders to the top and clears undo.
- The nearest thing to a plain start is the Workshop room: Basic Saw, Basic Pulse, Basic Sine, Basic Noise,
  Basic FM, Basic Wavetable, Basic Arp.
- "Empty layer A/B", first in the list of rooms, switches a layer's slots off.

### Saving and naming

"Save sound..." sits in Prefs (and the older Options menu). It asks for a name, offering the sound's own.
- One `.cspatch` file (XML, every parameter plus the macro names and engine pills) per sound, in
  `Documents\CoolSynth\My Sounds`, the file's name being the sound's name (D180).
- A name already there is replaced only after a Replace / Cancel question; characters Windows forbids are
  dropped; at most 60 letters.
- There is no plain "Save" that writes back to the sound being edited: every save asks for a name.
- Renaming and deleting happen in Windows Explorer only.
- Saving from the MiniLab is not possible (D180, "later").

### Finding and sorting

My Sounds is the first room in Home once it holds a sound, A to Z, with the pills and the playing mark.
- Only files directly in the folder count; subfolders are not read, so the player cannot sort into rooms.
- No category, tags, author, note, favourites or search.
- The folder is read again each time Home opens, so a file copied in shows up.

### Edited or not, and going back

The processor knows whether the sound was edited since it was loaded or saved (`isPatchDirty`), but the
panel does not show it.
- When another sound is loaded over an edit, the edit is kept once and offered back at the top of the rooms as
  "Back to your edit: name" (`keepEditForRecall`, D225). This is a safety net, not a Compare: it holds the
  edit, not the saved original, and is gone once used.
- Undo and redo cover single edits and whole loads.

### Sharing

A `.cspatch` file can be sent and dropped into the other player's My Sounds folder.
- No import (no drop onto the window, no open-file), no export, and no bank file for the player: the
  `.csbank` text format is for the factory rooms, compiled in.
- A sound that uses one's own sample files needs those files on the other computer too (to check how the path
  is stored).

### In a DAW and in the app

The panel, My Sounds and the folder are the same in the plugin and the app; all instances share the folder.
- The host saves the whole state in its project, the sound's name and file with it.
- The host's program list holds the 139 factory sounds only ("Room: Family: Name"); My Sounds is not in it.
- A DAW's own preset (a `.vstpreset`, a Bitwig or Reaper preset) works, as it is the same state, but it does
  not appear in My Sounds, and a My Sound does not appear in the DAW's browser.

A small drift seen on the way: `docs/architecture.md` gives the patch format as version 13; the code is at 15.

## What other plugins do

### Starting from scratch

Most offer an Init or "New" and let the player choose what a new instance opens with.
- Phase Plant has a New button and a right-click "set as default preset" for new instances
  ([Kilohearts docs](https://kilohearts.com/docs/presets)).
- Surge XT ships Init templates (Init Saw, Init Square, Init FM2) and "Set Current Patch As Default"
  ([manual](https://surge-synthesizer.github.io/manual-xt/), [changelog](https://surge-synthesizer.github.io/changelog)).
- Serum 2's preset name reads "- Init -" until a preset is chosen
  ([manual](https://xferrecords.com/web-manual/serum-2/loading-a-serum-preset)).
- Vital's Initialize resets the current preset; a user then lost an original because Ctrl-S saves over the last
  preset saved ([Vital forum](https://forum.vital.audio/t/anyway-to-create-a-new-preset-without-closing-and-reopening-vital/5031)).

### Saving, overwriting, renaming

Factory sounds are never overwritten: an edited one becomes a new user sound.
- Massive X: a name already taken asks to replace or rename; factory presets cannot be overwritten; a file
  dropped on the window is copied to the user folder ([NI manual](https://docs.native-instruments.com/online-guides/massive-x-manual/en/browser-and-presets)).
- Surge XT: Save (Ctrl+S) with name, category, author, licence and comment; an asterisk marks a changed patch;
  an option asks before loading over unsaved changes; rename and delete in the browser
  ([manual](https://surge-synthesizer.github.io/manual-xt/)).
- Pigments: Save As has a Bank field, and typing a new name makes a new bank; works on factory presets too
  ([Arturia forum](https://forum.arturia.com/t/how-can-i-change-the-default-bank-when-saving/1639)).
- Omnisphere: save opens Edit Tags right after the name; renaming and deleting are done in Explorer, and users
  complain of it ([manual](https://support.spectrasonics.net/manual/Omnisphere/browser/page02.html)).
- Vital always saves into its User folder, whatever folder is open; users ask to choose
  ([forum](https://forum.vital.audio/t/saving-presets-in-specified-folder-instead-of-default-user/9642)).

### Categories, tags, favourites, search

Folders as categories is the simple common ground; tags and favourites come on top.
- Surge XT and OB-Xf: the browser mirrors the folders on disk; search by name, `AUTHOR=`, `CATEGORY=`; a heart
  for favourites ([OB-Xf manual](https://surge-synth-team.org/ob-xf/manual/programmer/)).
- Phase Plant: `#words` in the description are tags, clickable and searchable; heart favourites; search
  `by:author` ([Kilohearts docs](https://kilohearts.com/docs/presets)).
- u-he: fixed tags plus favourites in 8 colours, each a smart folder; search with `#tag`, `author:`; refused
  user-defined tags because they break when presets are exchanged
  ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=6926180), [u-he FAQ](https://u-he.com/support/faq/how-to/moving-presets/)).
- NKS (Massive X, Komplete Kontrol): Sound Type and Character tags, a star, user presets filtered apart
  ([Komplete Kontrol manual](https://www.native-instruments.com/ni-tech-manuals/komplete-kontrol-manual/en/browser-and-presets)).
- Omnisphere: attribute columns, ratings as favourites, sort by rating
  ([manual](https://support.spectrasonics.net/manual/Omnisphere/browser/projects_ratings/page01.html)).
- Arturia: Types, Styles, Banks, "Liked" presets and colour tags
  ([Arturia support](https://support.arturia.com/hc/en-us/articles/10497306470940)).

### Compare

Plugins rarely have it; hardware and Logic do.
- Korg Kronos: COMPARE recalls the saved version, the LED shows which one plays, editing while it is lit drops
  the edits ([manual](https://www.manualslib.com/manual/523469/Korg-Kronos.html?page=48)).
- Logic's plug-in header has a Compare against the setting last saved with the project
  ([Apple](https://help.apple.com/logicpro/mac/10.2.1/en.lproj/lgcp4dcb0092.html)).

### Sharing, and the DAW's presets

One file per sound is the norm; a bank is a folder.
- u-he: favourites live in a database; moving them needs the same folder structure on both computers
  ([u-he FAQ](https://u-he.com/support/faq/how-to/moving-presets/)). Phase Plant's are in a config file
  ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8539407)). Serum's ratings are lost if the database is
  rebuilt ([Xfer forum](https://www.xferrecords.com/forums/posts/54345)).
- A plugin's own file works in every host and format; a `.vstpreset` is tied to one plugin and host folders
  ([SynthEdit](https://synthedit.com/guides/presets/), [Steinberg forum](https://forums.steinberg.net/t/vstpresets-not-available-to-load-for-updated-synth/838223)).
- CLAP's preset discovery lets a host such as Bitwig 5 list a plugin's presets in its own browser
  ([CLAP news](https://cleveraudio.org/news/2023)); its documentation is thin
  ([discussion](https://github.com/free-audio/clap/discussions/308)).
- Hardware moves banks as SysEx through general tools (MIDI-OX, SysEx Librarian), overwriting the user bank
  ([Sequential](https://sequential.com/support/download/prophet-12-sounds/)). Not a model to follow.

### Praise and complaints, in short

Praised: one click to load, quick search, a heart, folders that are just folders.
Complained of: browsers that feel "overly complex" (u-he), saving where one did not choose (Vital),
renaming only outside the plugin (Omnisphere), favourites lost when moving computers (u-he, Serum).

## Options

### A small, complete flow now, metadata in the file (recommended)

What it gives:
- An Init sound in the Workshop room (one saw, filter open, no effects), to choose, not to start in (D198 stands).
- Save writes back to the My Sound playing; Save as asks a name; factory sounds are never written.
- An edited mark beside the name, as Surge's asterisk.
- Compare: hear the loaded or saved version against the edit, back and forth, using the snapshot the processor
  already takes (`keepEditForRecall`).
- Subfolders of My Sounds as rooms, so sorting is done in Explorer; rename and delete (to the Recycle Bin)
  from the panel.
- Import: a `.cspatch` dropped on the window, or opened in the app, is copied into My Sounds.
- The file gains a category, author, a note and `#tags` now, even before anything shows them, so sounds saved
  from today on carry them.

Cost and side effects:
- Small to medium; most of it is file and processor work with tests, no new interface.
- Needs the real interface (UI last): where the edited mark, Compare, Save versus Save as and rename sit.
  Until then they can live in Prefs and the browser's list as throwaway rows.

### A, plus favourites and search

What it gives:
- A heart per sound, factory or own, and a Favourites room first in Home; search by name, `#tag` and author.
- Favourites kept as a small text file in `Documents\CoolSynth`, so a copy of the folder carries them, which
  avoids the database complaint.

Cost and side effects:
- Medium. The file and the search can be built and tested now; the view needs the real interface, and on the
  MiniLab a search needs a way to type, so there it would be the heart and the Favourites room only.

### A full browser in the plugin and the DAW

What it gives:
- Filter columns (room, category, tags), ratings, an audio preview, the DAW's program list and CLAP preset
  discovery listing My Sounds too.

Cost and side effects:
- Large; much of it is the real interface. CLAP discovery is poorly documented and only some hosts read it;
  the program list changing under a host needs care. Risks the "overly complex" complaint.

### Banks for sharing collections

What it gives:
- Export a room (or the favourites) as one file and import it as a room, samples it uses inside.

Cost and side effects:
- Medium. A zip of `.cspatch` files is simpler than a new format; the factory `.csbank` text stays internal.
  Can come after A or B; a folder sent as a zip does most of this today.
