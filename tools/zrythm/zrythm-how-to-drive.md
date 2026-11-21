# Driving Zrythm 1.0.0 trial on this PC (2026-11-20)

Results are in docs/test-pass.md (commit 3d7ccac). This note is only how to get Zrythm usable again.

Starting it:
- double-clicking zrythm.exe quits at once: "SVG loader was not found"
- its lib\gdk-pixbuf-2.0\2.10.0\loaders.cache leaves out pixbufloader_svg.dll
- fix without touching the install: set GDK_PIXBUF_MODULE_FILE to tools/zrythm/zrythm-loaders.cache
- also set GSK_RENDERER=gl (Vulkan showed stale frames) and GDK_SCALE=2 (readable at 175 %)
- launch from PowerShell with those variables set, then Start-Process on E:\Program Files\Zrythm-trial\bin\zrythm.exe

Clicking in it:
- maximized (it opens projects maximized), clicks on the right part of the window are lost; hover still works
- un-maximize, then size the window with tools/zrythm/move-window.ps1, e.g.
  -ProcessId <zrythm pid> -Title 'Zrythm' -X -24 -Y -24 -W 2650 -H 2124
- the plugin windows (titles start "CoolSynth (") can be moved the same way; they cover the transport otherwise

Using it:
- plugin list: search CoolSynth; entry 1 is the CLAP, entries 2 to 5 the VST3; double-click adds an instrument track
- notes: pencil tool, drag on the track lane for a clip, select it, drag notes in the piano roll, then Play
- Ctrl+S saves, Ctrl+Q quits; projects live in %LOCALAPPDATA%\zrythm\projects
- the first close crashed inside zrythm.exe (piano roll ruler redrawn after the project was freed); the second was clean
