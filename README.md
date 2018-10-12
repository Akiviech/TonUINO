# TonUINO
Die DIY Musikbox (nicht nur) für Kinder


# Change Log
## Christian Hermes Version 1.2 (12.10.2018)
General:
- fixed some small bugs I introduced myself...

Feature:
- Volume up only in single steps possible. Volume Down also in bigger steps.
- External power switch feature added. Can be used with "Pololu Pushbutton Power Switch" to switch off power after predefined maximum time or time after paused/stopped.

## Christian Hermes Version 1.1 (05.10.2018)
General:
- Fixed some small bugs (I am using Eclipse IDE for programming, so I had to change some stuff).
- Read and Write Card functions do not need parameters -> removed them.
- Added Error output to serial console for DFPlayer.
- Encapsulatet serial console output with DEBUG precompile switch.

Feature:
- Added INITIAL_VOLUME and MAX_VOLUME. Easier configuration per #define and maximum volume for Kids (DFMP3 modules 3W of power is a little bit too much).

## Version 2.0 (26.08.2018)

- Lautstärke wird nun über einen langen Tastendruck geändert
- bei kurzem Tastendruck wird der nächste / vorherige Track abgespielt (je nach Wiedergabemodus nicht verfügbar)
- Während der Wiedergabe wird bei langem Tastendruck auf Play/Pause die Nummer des aktuellen Tracks angesagt
- Neuer Wiedergabemodus: **Einzelmodus**
  Eine Karte kann mit einer einzelnen Datei aus einem Ordner verknüpft werden. Dadurch sind theoretisch 25000 verschiedene Karten für je eine Datei möglich
- Neuer Wiedergabemodus: **Hörbuch-Modus**
  Funktioniert genau wie der Album-Modus. Zusätzlich wir der Fortschritt im EEPROM des Arduinos gespeichert und beim nächsten mal wird bei der jeweils letzten Datei neu gestartet. Leider kann nur der Track, nicht die Stelle im Track gespeichert werden
- Um mehr als 100 Karten zu unterstützen wird die Konfiguration der Karten nicht mehr im EEPROM gespeichert sondern direkt auf den Karten - die Karte muss daher beim Anlernen aufgelegt bleiben!
- Durch einen langen Druck auf Play/Pause kann **eine Karte neu konfiguriert** werden
- In den Auswahldialogen kann durch langen Druck auf die Lautstärketasten jeweils um 10 Ordner oder Dateien vor und zurück gesprungen werden
- Reset des MP3 Moduls beim Start entfernt - war nicht nötig und hat "Krach" gemacht
