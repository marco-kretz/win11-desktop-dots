# DesktopDots

GNOME-artige Anzeige der virtuellen Desktops ganz links in der Windows-11-Taskleiste.

- **Punkt gefüllt**: auf dem Desktop ist mindestens ein Fenster offen
- **Punkt leer**: Desktop ist leer
- **Pille**: aktiver Desktop (mit kurzer Animation beim Wechsel)

Linksklick auf einen Punkt wechselt zu diesem Desktop, Rechtsklick öffnet ein Menü (Autostart, Beenden).

## Voraussetzungen

- Windows 11, Taskleiste zentriert, Widgets-Button deaktiviert (sonst liegt die Anzeige darüber)
- .NET 10 SDK zum Bauen

## Bauen und installieren

```powershell
dotnet publish -c Release -o $HOME\Tools\DesktopDots
& $HOME\Tools\DesktopDots\DesktopDots.exe
```

Danach per Rechtsklick „Mit Windows starten“ aktivieren. Der Autostart zeigt auf den Pfad der gerade laufenden `.exe`, daher vorher an einen festen Ort publishen.

## Funktionsweise

- Die App hängt ein transparentes Layered-Fenster als Kindfenster in die Taskleiste (`Shell_TrayWnd`). Es bewegt sich mit ihr und ist auf allen Desktops sichtbar.
- Desktops und aktiver Desktop kommen aus `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\VirtualDesktops`; Änderungen werden per `RegNotifyChangeKeyValue` sofort erkannt.
- Belegte Desktops werden jede Sekunde über `EnumWindows` und die öffentliche COM-API `IVirtualDesktopManager` ermittelt.
- Desktopwechsel per Klick simuliert `Ctrl+Win+←/→`, da es dafür keine öffentliche API gibt.
- Nach einem Explorer-Neustart hängt sich die App automatisch neu ein.

Nur dokumentierte APIs, keine undokumentierten Shell-Interfaces – sollte daher Windows-Updates überstehen.

## Einschränkungen

- Nur Hauptmonitor
- Belegung wird mit bis zu 1 s Verzögerung aktualisiert
