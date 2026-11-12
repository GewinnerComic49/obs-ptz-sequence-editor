## obs-ptz 0.19.2 mit Sequenz- und Keyframe-Editor

Diese Veröffentlichung enthält die angepasste Windows-x64-Version aus diesem Fork. Grundlage ist obs-ptz 0.19.2; sie enthält die Erweiterungen aus dem hochgeladenen Sequenzeditor sowie den zusätzlichen Keyframe-Editor.

### Changelog

- Sequenzeditor zum Zusammenstellen von PTZ-Aktionen, Pausen, Kamera-Presets und OBS-Szenenwechseln.
- Sequenzstarts über Trigger, Wiederholungen und Verkettung von Sequenzen.
- Neue Keyframe-Clips für absolute Pan-, Tilt- und Zoom-Positionen.
- Blender-ähnliche Zeitleiste mit drei Spuren, verschiebbaren Keyframe-Diamanten, Abspielkopf und einem Zeitraster von 0,1 Sekunden. Jeder Keyframe enthält gemeinsam Pan, Tilt und Zoom.
- Lineare, weiche (Smoothstep) und konstante Interpolation zwischen Keyframes.
- Keyframes einfügen, aktualisieren und löschen; lokale Vorschau der interpolierten Werte ohne Kamerabewegung.
- Keyframe-Clips werden mit den Sequenzen gespeichert, dupliziert und über deren Wiederholungen und Trigger abgespielt.
- Windows-x64-Installer und portables Plugin-ZIP nach dem offiziellen Projektverfahren mit Visual Studio 2022 und Inno Setup erstellt.

### Download und Installation

- **`obs-ptz-0.19.2-windows-x64-Installer.exe`**: Windows-Installer. OBS vor der Installation schließen, anschließend den Installer ausführen.
- **`obs-ptz-0.19.2-windows-x64.zip`**: Portables Plugin-Paket für eine manuelle Installation.
- **`SHA256SUMS.txt`**: SHA-256-Prüfsummen der beiden Downloads.

Den Sequenzeditor in OBS über das Werkzeuge-Menü öffnen. Unter „Schritte“ mit „Keyframe-Clip hinzufügen“ einen Clip anlegen. Die englisch beschriftete lokale Vorschau bewegt die Kamera nicht; die Kamerafahrt startet erst über „Ausführen“ im Sequenzeditor.

### Voraussetzungen und bekannte Grenzen

- Für Keyframe-Fahrten muss der Kameratreiber absolute Pan/Tilt- und Zoom-Positionen unterstützen. Die Wiedergabe sendet alle 40 ms interpolierte Ziele; tatsächliche Bewegung und Zeitgenauigkeit hängen von Kamera, Treiber und Verbindung ab.
- Gemeinsame PTZ-Keyframes; keine separaten Achsen-Keyframes, Bézier-Griffe, Undo/Redo oder Übernahme der aktuellen Kameraposition.
- Dies ist ein angepasster Build dieses Forks und keine unveränderte Veröffentlichung des ursprünglichen Projekts.

### Validierung

Windows-Build, Inno-Setup-Verpackung und Prüfung auf erzeugte DLL/EXE erfolgreich. Die heruntergeladene Installer-Prüfsumme stimmt mit dem Build-Ergebnis überein. SDK-unabhängige C++-Tests prüfen Interpolation, Grenzwerte und Zeitberechnung. Ein interaktiver OBS-/Kameratest wurde nicht durchgeführt.

Build: https://github.com/GewinnerComic49/obs-ptz-sequence-editor/actions/runs/43226006689

Quellstand: `ad2dba94a0b03bc1c6eb25a3d8f75ab303dcd841` auf Branch `keyframe-editor-0.19.2`.
