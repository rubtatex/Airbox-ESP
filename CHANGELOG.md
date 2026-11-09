# Changelog

Firmware changes, newest first. Each version has an English and a Français list. The release workflow copies the
section of the tagged version into the GitHub release, and the USB installer page shows it (see `scripts/changelog.py`).

## 1.0.4 - 2026-11-09

### English
- Board diagnostic over USB: the install page reads the firmware version, Wi-Fi and Bluetooth state (new read-only `airbox.info` serial command).
- The install page shows the changelog and is available in English and French.

### Français
- Diagnostic de la carte par USB : la page d'installation lit la version du firmware et l'état du Wi-Fi et du Bluetooth (nouvelle commande série `airbox.info`, en lecture seule).
- La page d'installation affiche le journal des modifications et existe en anglais et en français.

## 1.0.3 - 2026-11-09

### English
- USB install page, published on GitHub Pages: install or repair an Airbox from the browser.

### Français
- Page d'installation par USB, publiée sur GitHub Pages : installer ou réparer une Airbox depuis le navigateur.

## 1.0.2 - 2026-10-24

### English
- First published release: the OTA image and its SHA-256 are attached to the GitHub release.
- `GET /api/system/info` reports the firmware `version`, so the app can offer updates.

### Français
- Première version publiée : l'image OTA et sa somme SHA-256 sont jointes à la release GitHub.
- `GET /api/system/info` renvoie la `version` du firmware, pour que l'app puisse proposer les mises à jour.
