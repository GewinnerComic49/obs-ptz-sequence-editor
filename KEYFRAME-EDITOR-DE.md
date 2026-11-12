# PTZ-Keyframe-Editor

Erweiterung des mitgelieferten obs-ptz-0.19.2-Quellstands mit Sequenzeditor.

## Bedienung

1. In OBS über das Werkzeuge-Menü den Sequenzeditor öffnen und eine Sequenz wählen.
2. Unter „Schritte“ auf „Keyframe-Clip hinzufügen“ klicken und die Kamera wählen.
3. Die beiden zunächst neutralen Keyframes bei 0 und 5 Sekunden auswählen. Pan, Tilt und Zoom einstellen und mit „Insert / Update key“ übernehmen.
4. Die Zeitleiste anklicken, Werte einstellen und mit der Schaltfläche oder **I** einen weiteren Keyframe einfügen. Ein vorhandener Keyframe zur gleichen Zeit wird ersetzt.
5. Diamanten ziehen, um Keyframes zeitlich zu verschieben. Das Raster beträgt 0,1 Sekunden. Ein Diamant steht für einen gemeinsamen Pan/Tilt/Zoom-Keyframe; alle drei Spuren bleiben synchron.
6. „Linear“, „Smooth“ oder „Constant“ bestimmt den Übergang vom ausgewählten Keyframe zum nächsten. Smooth verwendet eine weiche Smoothstep-Kurve, Constant hält den Wert bis zum nächsten Keyframe.
7. Ausgewählte Keyframes mit „Delete key“ oder **Entf** entfernen. „Play / Pause preview“ zeigt lokal den Abspielkopf und interpolierte Werte; dabei werden keine Kamerabefehle gesendet.
8. Mit OK speichern. Die eigentliche Kamerafahrt startet über „Ausführen“ im Sequenzeditor. Vorhandene Wiederholungen und Trigger gelten auch für Keyframe-Clips. Doppelklick auf den Clip öffnet ihn erneut.

Pan/Tilt liegen im normierten Bereich −1 bis +1, Zoom zwischen 0 und 1. Nach Ändern eines Werts muss „Insert / Update key“ gedrückt werden. Die Clipdauer ergibt sich aus dem letzten Keyframe. Vor dem ersten Keyframe wird dessen Position gehalten, falls dieser später als 0 liegt. Die lokale Vorschau berücksichtigt nur den Clip, keine Schleifen oder Folgeaktionen.

## Technik und Grenzen

- Neue Aktion `keyframes`, zusätzliche JSON-Einträge unter `keyframes`; bestehende Schritte bleiben erhalten. Keyframes werden mit den Sequenzen gespeichert und dupliziert. Während einer laufenden Sequenz wirken Änderungen erst beim nächsten Start.
- Die Wiedergabe sendet alle 40 ms interpolierte absolute Positionen über die vorhandene `ptz_move_abs`-Schnittstelle. Stop/Stop All verwenden die vorhandene Stoppfunktion.
- Erforderlich ist eine Kamera bzw. ein Treiber mit absoluter Pan/Tilt- und Zoom-Steuerung. Dies ist keine Positionsregelung mit Kamerarückmeldung. Die tatsächlich erreichbare Bewegung und Zeitgenauigkeit hängen von Kamera, Treiber und Verbindung ab; viele Kameras können häufige Zieländerungen nur eingeschränkt verarbeiten.
- Es handelt sich um einen kompakten Dope-Sheet-Editor mit gemeinsamen PTZ-Keyframes. Separate Achsen-Keyframes, Bézier-Griffe, Undo/Redo und Übernahme der aktuellen Kameraposition sind nicht enthalten.
- Die neuen Dialogtexte sind Englisch, die Clip-Aktion und die Hinzufügen-Schaltfläche sind Deutsch/Englisch lokalisiert.

## Prüfung

`python3 tests/check-keyframes.py` kompiliert die tatsächlichen Interpolations- und Dauermethoden mit einem kleinen SDK-unabhängigen C++-Harness. Geprüft: linear, smooth, constant, exakte Grenzen, Zeitpunkte außerhalb des Clips, leere Clips, einzelne Keys, Clipdauer einschließlich Pause sowie bestehende Wait-/Ramp-Dauern.

Ein vollständiger Plugin-Build und interaktiver OBS-/Kameratest wurden in dieser Umgebung nicht durchgeführt: Qt-/OBS-Entwicklungsbibliotheken und CMake fehlen, das vorgesehene Paketinstallationswerkzeug ist nicht verfügbar. Die Build-Anleitung des ursprünglichen Projekts gilt weiterhin. Diese Lieferung enthält Quellcode, keine fertig kompilierte Plugin-Datei.

## Dateien

Das ZIP enthält den vollständigen erweiterten Quellstand. Der zusätzliche Patch ist ausschließlich gegen den hochgeladenen Sequenzeditor-Quellstand anzuwenden (`patch -p1 < obs-ptz-0.19.2-keyframe-editor.patch`). Den alten Sequenzeditor-Patch nicht zusätzlich auf das ZIP anwenden.

## Windows-Installer nach offiziellem Build-Verfahren

Der manuelle GitHub-Actions-Workflow `.github/workflows/keyframe-windows-installer.yaml` verwendet einen Windows-2022-Runner und die vorhandenen offiziellen Build-/Package-Aktionen des Projekts. Nach erfolgreichem Build enthält das Artefakt `obs-ptz-keyframes-windows-x64` den EXE-Installer, das portable ZIP und eine SHA-256-Prüfsumme. Es wird keine GitHub-Veröffentlichung angelegt.

Alternativ `BUILD-WINDOWS-INSTALLER.cmd` in einer x64 Native Tools Command Prompt for VS 2022 starten. Benötigt werden Visual Studio 2022 mit C++-Werkzeugen/Windows SDK, Git, CMake ab 3.28, PowerShell ab 7.2 und Inno Setup 6 am Standardpfad. Der Quellordner muss ein Git-Checkout mit Versions-Tag sein (die Projekt-Buildskripte nutzen `git describe`). Das Skript ruft unverändert `.github/scripts/Build-Windows.ps1` und anschließend `.github/scripts/Package-Windows.ps1` auf. Das erwartete Ergebnis ist `release/obs-ptz-0.19.2-windows-x64-Installer.exe`.

Status: Workflow und Startskript vorbereitet; Windows-Build noch nicht gestartet. Die GitHub-Verbindung darf kein Repository anlegen und liefert kein zugängliches Repository.
