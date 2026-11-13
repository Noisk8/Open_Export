# Matriz de compatibilidad

> **Regla del proyecto**: no se declara compatibilidad con un reproductor sin
> haberla **probada físicamente** en ese modelo y firmware.

Estados posibles:

| Estado | Significado |
|--------|-------------|
| `NOT_IMPLEMENTED` | La función no existe todavía. |
| `EXPERIMENTAL` | Implementada, pero sin validación completa. |
| `STRUCTURALLY_VALIDATED` | Los archivos generados los lee un parser independiente. |
| `HARDWARE_TESTED` | Probado y verificado en un reproductor físico. |

## Funcionalidades

| Funcionalidad | Estado |
|---|---|
| Importar audio (drag & drop / carpetas) | `EXPERIMENTAL` (Fase 1) |
| Leer metadatos (TagLib) | `EXPERIMENTAL` |
| Biblioteca SQLite | `EXPERIMENTAL` |
| Análisis de BPM | `STRUCTURALLY_VALIDATED` (autocorrelación de onsets) |
| Detección de tonalidad | `STRUCTURALLY_VALIDATED` (cromagrama + Krumhansl-Schmuckler) |
| Generación de beatgrid | `STRUCTURALLY_VALIDATED` |
| Generación de waveform | `STRUCTURALLY_VALIDATED` (150 columnas/s) |
| Análisis en segundo plano | `STRUCTURALLY_VALIDATED` |
| Hot cues / memory cues / loops | `STRUCTURALLY_VALIDATED` (editor en UI + ANLZ + OneLibrary) |
| Playlists (modelo, repositorio, interfaz) | `EXPERIMENTAL` |
| `export.pdb` (DeviceSQL, veinte tablas) | `STRUCTURALLY_VALIDATED` |
| `export.pdb` — tabla de pistas con metadatos | `STRUCTURALLY_VALIDATED` |
| `export.pdb` — jerarquía de playlists y carpetas | `STRUCTURALLY_VALIDATED` |
| `export.pdb` — tablas de menú del reproductor | `STRUCTURALLY_VALIDATED` |
| ANLZ (`DAT`/`EXT`/`2EX`) | `STRUCTURALLY_VALIDATED` |
| ANLZ — beat grid (`PQTZ`) | `STRUCTURALLY_VALIDATED` |
| ANLZ — waveforms (`PWAV`/`PWV2`/`PWV3`/`PWV4`/`PWV5`/`PWV6`/`PWV7`) | `STRUCTURALLY_VALIDATED` |
| ANLZ — cues (`PCOB`/`PCO2`) | `STRUCTURALLY_VALIDATED` |
| ANLZ — directorio por hash de la ruta | `STRUCTURALLY_VALIDATED` |
| Gestión/exportación USB | `EXPERIMENTAL` |
| Detección de memorias USB conectadas | `STRUCTURALLY_VALIDATED` |
| Exportación en segundo plano (progreso y cancelación) | `STRUCTURALLY_VALIDATED` |
| Validación de exportaciones | `STRUCTURALLY_VALIDATED` |
| Exportación incremental | `STRUCTURALLY_VALIDATED` |
| Relocalización de pistas tras cambio de punto de montaje | `STRUCTURALLY_VALIDATED` |
| Exportación coherente con pistas de audio ausentes | `STRUCTURALLY_VALIDATED` |
| Conversión FLAC → WAV sin pérdida | `STRUCTURALLY_VALIDATED` |
| Empaquetado TGZ / DEB | `STRUCTURALLY_VALIDATED` |
| `exportLibrary.db` (OneLibrary / Device Library Plus) | `STRUCTURALLY_VALIDATED` (abierto con SQLCipher 4.13 real) |
| `exportExt.pdb` (My Tags) | `NOT_IMPLEMENTED` |
| `exportLibrary.db` (Device Library Plus) | `NOT_IMPLEMENTED` |

> `STRUCTURALLY_VALIDATED` significa que un lector independiente —`DeviceSqlReader`,
> escrito aparte del escritor, y la suite de pruebas— reconstruye lo que se
> ha generado y lo encuentra coherente. **No** significa que un reproductor
> lo haya leído.

## Reproductores objetivo (por definir)

| Modelo | Formato | Estado |
|---|---|---|
| CDJ-2000NXS2 | DeviceSQL + ANLZ | `NOT_IMPLEMENTED` |
| CDJ-3000 | DeviceSQL (+ `.2EX`, OneLibrary?) | `NOT_IMPLEMENTED` |
| XDJ-1000MK2 | DeviceSQL + ANLZ | `NOT_IMPLEMENTED` |
| XDJ-XZ | DeviceSQL + ANLZ | `NOT_IMPLEMENTED` |
| XDJ-RX3 | DeviceSQL + ANLZ | `NOT_IMPLEMENTED` |

Se completará a medida que se implemente y **se pruebe en hardware** cada
formato. No se rellena con suposiciones.

## Lo que falta para poder leer playlists

Para que un XDJ-RX o un CDJ muestre las listas de reproducción hacen falta
tres cosas, y las tres están implementadas:

1. `PIONEER/rekordbox/export.pdb` con las tablas `07` y `08` rellenadas —
   es lo que construye el menú de playlists.
2. El audio en `/Contents/...`, con la ruta exacta en la fila de la pista.
3. Los ficheros ANLZ en `PIONEER/USBANLZ/P###/<hash>/`, con el `hash` que
   calcula el firmware a partir de la ruta del audio.

Lo que **no** se ha probado todavía es si un reproductor real acepta lo que se
genera. Ese es el siguiente paso: exportar la biblioteca de pruebas a una
memoria FAT32 y probarlo en el hardware.
