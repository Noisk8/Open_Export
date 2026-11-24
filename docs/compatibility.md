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
| `exportExt.pdb` (My Tags) | `EXPERIMENTAL` (opcional por JSON) |
| Bancos de hot cues en OneLibrary | `EXPERIMENTAL` (opcional por JSON) |

> `STRUCTURALLY_VALIDATED` significa que un lector independiente —`DeviceSqlReader`,
> escrito aparte del escritor, y la suite de pruebas— reconstruye lo que se
> ha generado y lo encuentra coherente. **No** significa que un reproductor
> lo haya leído.

## Registro de validación física

Ninguno de estos casos se ha ejecutado en hardware en esta revisión. El formato
indicado es el candidato a probar, no una declaración de compatibilidad.

| Modelo objetivo | Firmware | USB / particiones / sistema | Formato candidato | Navegación / carga / cues / waveform | Evidencia | Estado físico |
|---|---|---|---|---|---|---|
| CDJ-2000NXS2 | Pendiente | Pendiente | DeviceSQL + ANLZ | Pendiente | Sin prueba | NO PROBADO |
| CDJ-3000 | Pendiente | Pendiente | DeviceSQL + ANLZ / OneLibrary | Pendiente | Sin prueba | NO PROBADO |
| XDJ-1000MK2 | Pendiente | Pendiente | DeviceSQL + ANLZ | Pendiente | Sin prueba | NO PROBADO |
| XDJ-XZ | Pendiente | Pendiente | DeviceSQL + ANLZ | Pendiente | Sin prueba | NO PROBADO |
| XDJ-RX original | Pendiente | FAT/FAT32 (requisito del perfil) | DeviceSQL + ANLZ | Pendiente | Sin prueba | NO PROBADO |
| XDJ-RX3 | Pendiente | Pendiente | DeviceSQL + ANLZ | Pendiente | Sin prueba | NO PROBADO |

### Perfil preliminar XDJ-RX original

El perfil de exportación usa Device Library, requiere una copia WAV verificada
para cada FLAC del set y comprueba los archivos WAV y AIFF a 44,1/48 kHz y
16/24 bits. En un volumen raíz exige FAT o FAT32. El perfil se basa en las
[especificaciones del fabricante](https://www.pioneerdj.com/es/news/2015/xdj-rx/)
y en sus [límites documentados para WAV](https://community.pioneerdj.com/hc/en-us/community/posts/22980281102233-XDJ-RX-WAV-limits).

### Protocolo de prueba

1. Registrar commit/build, modelo, firmware, marca/capacidad del USB, esquema de
   particiones y sistema de archivos; guardar el informe del validador.
2. Exportar una biblioteca de prueba: dos carpetas anidadas, varias playlists,
   un tema compartido, WAV y FLAC, metadata con acentos, cues y loops.
3. Comprobar en el equipo navegación y orden de listas, metadatos, carga y audio
   de cada formato, waveform, beatgrid, cues y loops.
4. Repetir con exportación selectiva e incremental; comprobar que las listas y
   pistas resultantes coinciden con lo seleccionado.
5. Anotar cada resultado, anomalía y evidencia (captura/vídeo/log), con fecha.
   Solo entonces marcar HARDWARE_TESTED para esa combinación concreta.


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
