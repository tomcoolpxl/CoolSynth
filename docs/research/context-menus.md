# Context menus

Preliminary research for TODO 29, 2026-11-20, for the user to discuss.
The item: design right-click menus for the parts of the panel that can be clicked, with what each item can do; check how other synths do it, then options to choose from.
It belongs to the real interface (UI last), but some of it is model work that can come first.
Line numbers are those of 2026-11-20.

## What CoolSynth has today

Two menus exist.
- The layer menu, `showLayerMenu` (`src/plugin/PanelEditor.cpp` 2905): the Layers page's actions for that layer, from `InterfaceModel::layerActionNames` and `doLayerAction` (`src/interface/InterfaceModel.cpp` 547-572).
  - Copy A to B, Copy B to A, Swap A and B; Double, an octave lower, an octave higher on the other layer; Solo (ticked); Empty the other layer.
  - Each is one undo step in `SynthAudioProcessor`.
- The Learn menu, `showLearnMenu` (2140): the controller's place, Learn for that place, Learn for the setting a knob shows "on every page", Forget, and Controller setup.

What each part does now, left click and right click.
- Knobs 1-4 and 6-8 (`Knob`, 145): drag, wheel or click (loads an instrument a card shows); right click opens Learn.
- Knob 5 and the main knob: turn pages, or the list; right click opens Learn (the main knob's adds its press).
- Faders 1-4 (`PanelFader`, 113): drag; right click opens Learn.
- Pads 1-8 (`Pad`, 395): press; right click opens Learn.
- The screen (`Screen::mouseDown`, 517): a click runs a hit, the cards drag like knobs; any right click anywhere on it goes back one level (D181).
  - Hits: the chip (back), "Layer A" (switch layer), each trail item, page tabs, list rows (`clickListRow`, 933), slots of the section map (`clickSlot`, 1030).
- The layer screen (`Content::mouseDown`, 2873): a chain block opens that layer's section on that slot (`openSlot`); the layer title opens the layer menu; the panel elsewhere opens Home for that layer; a right click on a panel opens the layer menu; the CPU corner opens the preferences.
- Options, Hold, Oct- and Oct+ are plain `TextButton`s with a left click only.

What the model can already do that menus would reuse.
- `chooseType` (InterfaceModel.cpp 1308) sets a slot's type and resets its generic parameters to the new type's defaults (`GenericSlotParameter::currentTypeDefault`), as one undo step through `performUndoableEdit`.
- Nothing copies, pastes, clears or swaps a single slot yet, and nothing resets one setting to its default.
- `knobParameter(k)` gives the parameter a knob moves on the page in view.

## How others do it

### Knobs and sliders

Serum 2: right click gives Reset Control, MIDI Learn, Lock Parameter (kept through preset changes), modulation sources to assign, and remove or bypass a modulator; double click types a value; Ctrl-click resets ([Xfer, Using knobs and sliders](https://xferrecords.com/web-manual/serum-2/using-knobs-and-sliders)).

Vital, from its source: Set to Default Value, Learn MIDI Assignment, Clear MIDI Assignment, Enter Value, then "Disconnect from" each modulation source ([vital synth_slider.cpp](https://github.com/mtytel/vital/blob/master/src/interface/editor_components/synth_slider.cpp)).
- No host items and no copy or paste of a value.

u-he Diva and Zebra2: right click on a knob gives Lock; double click resets; a value can be typed after a click; right click on the background picks the GUI size; right click on the data display gives init ([Diva user guide](https://u-he.com/downloads/manuals/plugins/diva/Diva-user-guide.pdf), [Zebra2 user guide](https://u-he.com/downloads/manuals/plugins/zebra2/Zebra2-user-guide.pdf)).
- Older u-he versions put MidiUnLearn on the knob's menu ([u-he FAQ](https://u-he.com/support/faq/troubleshooting/unwanted-midi/)).

Kontakt: right click gives "Learn MIDI CC# Automation" and "Remove MIDI Automation" ([NI, MIDI learn](https://native-instruments.com/ni-tech-manuals/kontakt-manual/en/midi-learn)).

Bitwig: right click on a parameter assigns or removes a controller; double click restores the default ([Bitwig, manual controller assignment](https://www.bitwig.com/userguide/latest/manual_controller_assignment/), [user interfacing](https://www.bitwig.com/userguide/latest/user_interfacing)).
- Ableton keeps automation items on the parameter's context menu, such as Re-Enable Automation ([Live 12 manual, automation](https://www.ableton.com/en/live-manual/12/automation/)).

The common core is small: reset to default, type a value, learn and forget a controller.
Modulation and lock come next where a synth has them.

### Modules and slots

Serum 2: right click an oscillator's label for Copy, Copy (w/mods) and Paste; the clipboard lasts across presets but not across instances; dragging a module onto another swaps them ([Xfer, Copying a module](https://xferrecords.com/web-manual/serum-2/copying-a-module)).

Pigments: an engine menu with Copy engine, Copy to engine x, Copy to engine x with modulations, Reset engine ([Pigments manual, contents](https://mans.io/files/viewer/2161698/193)).

Zebra2's grid is the closest to CoolSynth's chain blocks: click an empty cell to choose a module from a list; right click a module for its input lane, active, and remove ([Zebra2 user guide](https://u-he.com/downloads/manuals/plugins/zebra2/Zebra2-user-guide.pdf), page 13).

Diva changes a module's type by a left click on its label (same guide set).

Phase Plant copies a module by Ctrl-drag and removes it with an X by its name ([Kilohearts, Phase Plant](https://kilohearts.com/docs/phase_plant)).

So for slots the usual set is choose a type, copy, paste, swap, reset or init, remove.

### The host's own items

JUCE: `AudioProcessorEditor::getHostContext()` returns an `AudioProcessorEditorHostContext`; its `getContextMenuForParameter()` gives a `HostProvidedContextMenu`, shown natively or as a `PopupMenu` from `getEquivalentPopupMenu()` to merge with the plugin's own items ([JUCE docs](https://docs.juce.com/master/structAudioProcessorEditorHostContext.html), [HostProvidedContextMenu](https://docs.juce.com/master/structHostProvidedContextMenu.html), [JUCE forum](https://forum.juce.com/t/show-automation-track-cubase-right-click-menu/55004)).

VST3: `IComponentHandler3::createContextMenu` gives a menu the host pre-fills for a parameter, with items such as show automation or MIDI learn; the plugin adds its own ([Steinberg, IComponentHandler3](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Change+History/3.5.0/IComponentHandler3.html)).

CLAP: the context-menu extension lets host and plugin exchange menu entries for a parameter ([clap-sys header](https://docs.rs/clap-sys/0.4.0/src/clap_sys/ext/draft/context_menu.rs.html)).
- The pinned `external/clap-juce-extensions` already wires this: its `EditorHostContext` (`src/wrapper/clap-juce-wrapper.cpp` 323) is given to the editor, so the same JUCE call works in the CLAP build.
- The standalone app has no host and gets nothing; the menu simply leaves the host items out.

### Touch, pen and keyboard

Windows treats press-and-hold as the right click for touch and pen; Microsoft also advises short menus, few submenus, and no right click that can be confused with another action ([Microsoft, using context menus](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/mpc/using-context-menus), [touch interactions](https://learn.microsoft.com/en-us/windows/apps/develop/input/touch-interactions)).

JUCE does not turn a long press into `isPopupMenu()` while multi-touch is on; plugins time it themselves ([JUCE forum](https://forum.juce.com/t/e-mods-isrightbuttondown-never-set-after-long-press-on-touch-screen/16905)).

From the keyboard, Shift+F10 and the Menu key open a context menu ([Old New Thing](https://devblogs.microsoft.com/oldnewthing/20110809-00/?p=9943)); JUCE's accessibility handlers have a showMenu action, which fits the handlers added in D227.

## Points to settle whatever the option

A right click on the screen goes back today (D181).
- Menus on slots and list rows would take that away there; Back could stay on the screen's empty space and the chip, or move to a menu item.

Knobs 1-4 follow the page, so a knob's menu is built when it opens, for the setting it shows then.
- Knob 5 and the main knob move no setting; they keep Learn only.

Each item that changes the sound is one undo step, as the layer actions are.

Copy and paste: a clipboard inside the plugin, as Serum's, or the system clipboard as text, which would also work between instances and into a chat.

Whether a copy takes the slot's modulation with it; Serum and Pigments offer both.

## Options

### A shared menu for controls and slots (recommended)

One menu builder, used by right click, a long press and Shift+F10 alike.

On knobs 1-4 and 6-8 and on faders:
- Reset to default, Enter value, the Learn items there now, then the host's items (automation and the like) under a separator.

On a slot, from a chain block or the section map:
- Choose type (a submenu), Copy, Paste, Clear (the slot's Off), Swap with (a submenu of the slots of its kind), Reset to the type's defaults.

Pads and the layer panels keep their menus; the layer menu gains nothing for now.

It gives what users of Serum, Vital and Pigments expect, short enough for Microsoft's advice.
Cost: a slot clipboard, swap and reset in the model, a menu builder, the host call, a touch timer; about a week with tests.
Side effects: right click no longer goes back on slots; Reset and Paste land as one undo step each.

### Controls only

The common control menu on knobs and faders (reset, enter value, learn, host items); slots stay as they are, changed by a click and the type list.

Cheapest, and it closes the gap with Serum, Vital and Bitwig on knobs.
Side effect: slot copy and swap still go through whole layers only.

### The full set

Option A, plus menus on list rows and more.
- A sound row: Load into A, Load into B; a My Sounds row also Rename, Delete, Show in folder.
- Lock a setting through sound changes, as Serum and Diva do.
- Copy with or without modulation; Copy layer and Paste layer between instances.

Most complete; also the longest menus, and Delete on a row needs a confirmation.
Cost: about twice Option A, and Lock touches how presets load.

### The host's items now, the rest with the real interface

Only add the host's items and Reset to default to the Learn menu that exists, now; design the rest with the real interface.

Small and useful in Cubase, Bitwig and other hosts today; nothing is thrown away later, since the builder carries over.
Side effect: slots wait for the real interface.

## What can be built before the real interface

Model-level, testable without a screen:
- Reset a setting to its default, as one undo step (`getDefaultValue` on the parameter).
- Copy a slot to a clipboard and paste it into a slot of its kind: the type and its generic parameters, as `chooseType` walks them.
- Clear a slot (its Off type) and swap two slots of a kind, each one undo step.
- Enter a value from text, through the parameter's own text-to-value.
- The MCP server's tools (D227) could call the same functions, which tests them from outside too.

Interface-level, best left for the real interface:
- Which parts get a menu, the wording, the order, the long-press timing and where Back goes.

The host's items sit between the two: a small change in the editor, testable now in Cubase or Bitwig, VST3 and CLAP alike.
