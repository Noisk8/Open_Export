# Arquitectura

## Principios

- **Modular**: cada capa (UI, biblioteca, análisis, exportación, dispositivos)
  es independiente y comprobable sin GUI ni hardware.
- **Backend en C++20, UI en Qt Quick/QML**: la lógica vive en clases C++
  testables; QML solo presenta y delega.
- **Sin dependencias exclusivas de una distribución**: TagLib, SQLite y FFmpeg
  son multiplataforma; la detección de USB usa APIs por SO detrás de una interfaz.
- **Honestidad**: una exportación "generada" no se marca como "compatible" sin
  validación estructural ni, menos aún, sin prueba en hardware.
- **Un lector por cada escritor**: el formato DeviceSQL se escribe en
  `DeviceSqlWriter` y se comprueba con `DeviceSqlReader`, que está escrito
  aparte. Si los dos compartieran código, la validación no valdría nada.

## Capas

```text
┌─────────────────────────────────────────────┐
│  src/ui        Qt Quick / QML               │
│                LibraryController (modelo)   │
│                PlaylistController (modelo)  │
├─────────────────────────────────────────────┤
│  src/library   SQLite (DatabaseManager,     │
│                TrackRepository,             │
│                PlaylistRepository), import  │
│                (AudioImportService),        │
│                carátulas (ArtworkExtractor) │
│  src/audio    AudioAnalysisService (FFmpeg → │
│               BPM, beat grid, tono, waveform)│
│  src/devices  UsbDeviceManager (sysfs +      │
│               QStorageInfo: volúmenes FAT32) │
├──────────────┴──────────────────────────────┤
│  src/export   ExportManager, ExportValidator│
│               devicesql/ (DeviceSQL),       │
│               anlz/ (ANLZ)                  │
└─────────────────────────────────────────────┘
```

## Decisiones clave

- **Identidad persistente**: cada pista se identifica por su **hash SHA-256**
  (`file_hash`), no por su ruta. Una pista movida sigue siendo la misma.
- **Base de datos propia** (SQLite con migraciones versionadas) separada del
  formato de exportación DeviceSQL: la biblioteca local es el origen de verdad;
  el escritor DeviceSQL es un "render" de esa biblioteca.
- **Precisión temporal**: el beat grid se guarda en *frames* (o *seconds* de
  doble precisión), no en milisegundos enteros, para no perder precisión al
  alinear compases.
- **El reproductor es el destino, no rekordbox**: la biblioteca se puede
  exportar a un volumen tal cual; `export.pdb` se genera desde cero, con sus
  veinte tablas y su paginación, para que un CDJ o un XDJ-RX puedan recorrer
  las listas y los metadatos.
- **Las listas se exportan con su jerarquía**: la tabla `07` lleva `parent_id`,
  `sort_order` y `raw_is_folder`, de modo que las carpetas de rekordbox
  aparecen como carpetas en el menú del reproductor.

## Estado de implementación

| Capa | Estado |
|---|---|
| `core` (modelos) | ✅ `Track`, `Playlist` |
| `library` (SQLite + importación) | ✅ Fase 1 + carátulas |
| `audio` (análisis) | ✅ Fase 2 (FFmpeg + FFT propia) |
| `cues` | ⏳ Fase 3 (el ANLZ ya los sabe escribir) |
| `export` (DeviceSQL + ANLZ) | ✅ Fases 4–5 |
| `export` (volumen USB + validación) | ✅ Fase 6 |
| `devices` (detección USB) | ✅ Fase 6 |
| `ui` | ✅ biblioteca, playlists, análisis y selector USB |

## Formato de exportación

El detalle byte a byte del formato está en
[rekordbox-export.md](rekordbox-export.md). En resumen:

```text
src/export/devicesql/    export.pdb: veinte tablas, páginas de 4096 bytes
src/export/anlz/         ANLZ0000.DAT / .EXT / .2EX
src/export/ExportManager escribe el volumen completo
src/export/ExportValidator lo vuelve a leer y lo comprueba
```
