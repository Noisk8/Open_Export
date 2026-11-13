# Open Deck Export

Aplicación de escritorio **open source** para analizar música y preparar
memorias USB compatibles con reproductores **Pioneer DJ** (formato rekordbox),
sin depender de rekordbox.

**Estado: Fase 4–6.** La biblioteca SQLite, la importación, el análisis musical
(BPM, tonalidad, beat grid y waveform), el escritor DeviceSQL con sus veinte
tablas, las listas de reproducción, la detección de memorias USB y la
validación de la exportación ya funcionan. Falta el soporte de la base de datos
*Device Library Plus*. Ver [docs/compatibility.md](docs/compatibility.md)
para el estado real de cada funcionalidad.

## Objetivo

Replicar el flujo del modo *EXPORT* de Rekordbox con código propio:

1. Arrastrar canciones o importar carpetas.
2. Analizar (BPM, tonalidad, beatgrid, waveform) — en un hilo de trabajo, con FFmpeg.
3. Crear y editar playlists, hot cues, memory cues y loops.
4. Exportar a una memoria USB detectada automáticamente, generando `export.pdb`
   (DeviceSQL) y los análisis `ANLZ`.
5. Validar las exportaciones antes de darlas por buenas.

> **Importante**: este proyecto **no afirma compatibilidad con hardware** salvo
> que una exportación haya sido **probada físicamente** en un reproductor. Se
> distingue entre *generada*, *validada estructuralmente* y *probada en hardware*.

## Requisitos

- Linux (inicialmente; Windows/macOS después).
- C++20, CMake ≥ 3.22, Ninja o Make.
- Qt 6.5+ (Core, Gui, Qml, Quick, QuickControls2, QuickLayouts, Sql, Dialogs).
- TagLib ≥ 1.12.
- SQLite3.
- **FFmpeg** (`ffmpeg` en el `PATH`) para el análisis musical.

En Ubuntu/Debian:

```sh
sudo apt install cmake ninja-build g++ ffmpeg \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
    libsqlite3-dev libtag1-dev
```

## Compilación

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/open-deck-export
```

## Pruebas

```sh
ctest --test-dir build --output-on-failure
```

O los ejecutables por separado: `open-deck-tests`, `open-deck-tests-devicesql`,
`open-deck-tests-anlz`, `open-deck-tests-ui`, `open-deck-tests-analysis`,
`open-deck-tests-devices`, `open-deck-tests-export-controller`,
`open-deck-tests-export`.

## Arquitectura (resumen)

- `src/core` — modelos de dominio (`Track`, `Playlist`).
- `src/library` — SQLite (`DatabaseManager`, `TrackRepository`,
  `PlaylistRepository`), importación (`AudioImportService`) y carátulas
  (`ArtworkExtractor`).
- `src/audio` — análisis musical: `AudioAnalysisService` (decodifica con
  FFmpeg y calcula BPM, beat grid, tonalidad Camelot y waveform).
- `src/devices` — `UsbDeviceManager`: detección de volúmenes extraíbles
  montados (excluye las particiones internas, aunque sean FAT32).
- `src/export/devicesql` — escritor y lector del formato DeviceSQL
  (`export.pdb`).
- `src/export/anlz` — escritor de los ficheros de análisis `.DAT`, `.EXT` y
  `.2EX`.
- `src/export` — `ExportManager` (escribe el volumen) y `ExportValidator`
  (lo comprueba con un lector independiente).
- `src/ui` — interfaz Qt Quick/QML: modelos de biblioteca, listas, dispositivos,
  análisis y exportación. `AnalysisController` y `ExportController` ejecutan el
  trabajo pesado en hilos aparte.

Ver [docs/architecture.md](docs/architecture.md).

## Flujo de datos

La biblioteca local es el **origen de verdad**; el USB es un *render* de esa
biblioteca. Nada se lee de rekordbox y nada se fusiona con una exportación
anterior.

```text
  ARCHIVOS DE AUDIO (disco local o USB)
        │
        ├── AudioImportService (TagLib) ── título, artista, álbum, género,
        │                                 año, duración, sample rate/depth,
        │                                 bitrate, canales + SHA-256
        ├── AudioAnalysisService (FFmpeg) ─ BPM, tonalidad Camelot,
        │                                 beat grid, waveform (150 col/s)
        ├── ArtworkExtractor (TagLib) ──── carátula JPEG embebida
        └── PlaylistRepository ─────────── listas, carpetas y su orden
                    │
                    ▼
  ┌───────────────────────────────────────────────┐
  │ SQLite local  ~/.local/share/OpenDeckExport/   │
  │               OpenDeckExport/library.db (v3)  │
  │                                               │
  │  tracks · playlists · playlist_entries        │
  │  track_beats · track_cues · track_waveform    │
  │  schema_version (migraciones 1 → 2 → 3)       │
  └───────────────────────────────────────────────┘
                    │
      ┌─────────────┴──────────────┐
      ▼                            ▼
  INTERFAZ QML              EXPORTACIÓN
  LibraryController         ExportManager::buildLibrary()
  PlaylistController          │  (hilo de la interfaz, ~50 ms:
  DeviceController            │   resuelve pistas, rutas, ids y
  AnalysisController          │   precarga el análisis de la BD)
  ExportController            ▼
                         ExportLibrary  (autocontenido, sin BD)
                                │
                                ▼
                         ExportWorker (QThread)
                                │
                                ▼
                    VOLUMEN USB  (FAT32 / MBR)
```

Detalle de las etapas:

| # | Etapa | Qué ocurre | Dónde |
|---|-------|------------|-------|
| 1 | **Importación** | Drag & drop → `AudioImportService::readMetadata` (TagLib) + hash SHA-256; se deduplica por hash o ruta y se inserta en `tracks`. | `LibraryController::importFiles` |
| 2 | **Análisis** | `ffmpeg` decodifica a PCM mono 22050 Hz; se calculan BPM, beat grid, tonalidad y waveform. Los resultados se guardan en `tracks`, `track_beats` y `track_waveform`. | `AnalysisController` → `AnalysisWorker` (hilo aparte) |
| 3 | **Listas** | El usuario crea listas/carpetas y añade pistas con `+`; se guardan en `playlists` (`parent_id`, `is_folder`, `sort_order`) y `playlist_entries` (`position`). | `PlaylistController` |
| 4 | **Detección USB** | `QStorageInfo` + sysfs: sólo volúmenes FAT32/exFAT cuyo bus sea USB o cuyo disco sea extraíble; se excluyen `/`, `/boot/efi`, loops. Refresco cada 3 s. | `DeviceController` |
| 5 | **Preparación** | Se unen las pistas de las listas, se asignan ids densos 1..N y se calculan las tres rutas de cada pista. | `ExportManager::buildLibrary` |
| 6 | **Escritura** | Copia de audio, ficheros ANLZ, carátulas y `export.pdb`, en segundo plano con progreso y cancelación. | `ExportWorker` → `ExportManager::write` |
| 7 | **Validación** | Un lector independiente vuelve a leer `export.pdb` y comprueba rutas, jerarquía, entradas y que existan audio y ANLZ. | `ExportValidator` |

> El análisis es **optativo**: si una pista no se ha analizado, se exporta igual,
> pero sus ANLZ no llevan `PQTZ` y la waveform sale plana.

## Cómo exporta los archivos

### 1. Las tres rutas de cada pista

`buildLibrary` decide, para cada pista, tres rutas **absolutas respecto a la
raíz del volumen** y con barras `/` (como hace rekordbox):

| Campo | Ejemplo | Uso |
|---|---|---|
| `devicePath` | `/Contents/Lauer/Lauer - Embalmed In Martino/Martino.flac` | Dónde se copia el audio; se guarda en la cadena 20 de la fila de pista. |
| `fileName` | `Martino.flac` | Cadena 19. |
| `analyzePath` | `/PIONEER/USBANLZ/P05A/0002ABCD/ANLZ0000.DAT` | Cadena 14; apunta al `.DAT`. |

- El audio se organiza en `/Contents/<artista>/<álbum>/<título>.<ext>`;
  los nombres se sanitizan (caracteres prohibidos en FAT32) y el título se
  trunca a 43 caracteres. Los duplicados reciben ` (2)`, ` (3)`…
- Si falta el tag de título se usa el nombre del archivo.
- El directorio del análisis es un **hash de la ruta del audio** sobre sus
  unidades UTF-16 (`h = h*0x34F5501D + c*0x93B6`, `h %= 0x30D43`, siete bits
  de `h` para el shard `P###`). El CDJ-3000 lo recalcula él mismo, así que no
  basta con que `analyze_path` apunte al sitio correcto: los ficheros tienen
  que estar ahí.
- `fileType` se deduce de la extensión: 1 MP3, 4 M4A, 5 FLAC, 11 WAV, 12 AIFF.

### 2. El volumen que se escribe

```text
/Contents/<artista>/<álbum>/<tema>.<ext>          audio copiado
/PIONEER/rekordbox/export.pdb                     base de datos DeviceSQL
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.DAT          beat grid, cues, previews
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.EXT          waveforms de detalle/color
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.2EX          waveforms de 3 bandas (CDJ-3000)
/PIONEER/Artwork/<carpeta>/a<n>.jpg                carátula 80×80
/PIONEER/Artwork/<carpeta>/a<n>_m.jpg              carátula 240×240
```

### 3. `export.pdb` (DeviceSQL, *little endian*)

- Páginas de **4096 bytes**. La página 0 lleva la cabecera
  (`len_page`, `num_tables`, `next_unused_page`, `sequence`) y el directorio de
  **20 tablas**, todas presentes y en orden, incluso las vacías.
- Cada tabla empieza por una página *sentinel* (índice impar `2n+1`) y sigue
  con páginas de datos (`0x24`). El índice de filas se construye **hacia atrás**
  desde el final de la página, en grupos de 16 huecos de 36 bytes.
- Las cadenas DeviceSQL son ASCII corto (byte impar), ASCII largo (`0x40`) o
  UTF-16LE (`0x90`); las largas se alinean a 4 bytes respecto al inicio de la
  fila.
- La **fila de pista** (tabla 0) tiene una cabecera fija de `0x88` bytes con
  BPM×100, bitrate, sample rate/depth, duración, año, rating, color, tipo de
  fichero y las referencias a las tablas pequeñas, más 21 desplazamientos a
  cadenas (`title`, `filename`, `file_path`, `analyze_path`, …).
- Las **listas** viven en dos tablas: `07 playlist_tree`
  (`parent_id`, `sort_order`, `id`, `raw_is_folder`, nombre) y
  `08 playlist_entries` (`entry_index`, `track_id`, `playlist_id`). Las
  listas de primer nivel tienen `parent_id = 0`; las carpetas se exportan como
  carpetas del menú.
- Las tablas pequeñas (`01`–`06`) internan géneros, artistas, álbumes, sellos,
  tonalidades y colores; `16`–`18` son los menús de navegación del reproductor.

### 4. ANLZ (*big endian*, lo contrario que `export.pdb`)

Cabecera `PMAI` de 28 bytes + secciones etiquetadas
(`fourcc`, `len_header`, `len_tag`). El reproductor deduce `.EXT` y `.2EX`
cambiando la extensión del `.DAT` al que apunta `analyze_path`.

| Fichero | Secciones en orden |
|---|---|
| `.DAT` | `PPTH`, `PVBR`, `PQTZ`, `PWAV`, `PWV2`, `PCOB` (hot), `PCOB` (memoria) |
| `.EXT` | `PPTH`, `PWV3`, `PCOB` ×2, `PCO2` ×2, `PWV5`, `PWV4` |
| `.2EX` | `PPTH`, `PWV7`, `PWV6` |

- `PQTZ` lleva cada beat: posición en el compás (1–4), tempo×100 y tiempo en ms.
- `PWAV`/`PWV2` son previews de 400/100 columnas; `PWV3`/`PWV5`/`PWV7` van a
  150 columnas por segundo; `PWV4`/`PWV6` a 1200 columnas.
- `PCOB`/`PCO2` guardan hot cues, memory cues y loops.

### 5. Concurrencia y validación

- `ExportManager::buildLibrary()` es lo único que toca SQLite y corre en el
  hilo de la interfaz (~50 ms). `ExportManager::write()` es estático y recibe
  una `ExportLibrary` ya resuelta: puede ejecutarse en cualquier hilo.
- `ExportController` lanza un `QThread`; la UI sigue respondiendo, muestra el
  progreso pista a pista y permite cancelar entre pistas.
- Al terminar, `ExportValidator` usa `DeviceSqlReader` (escrito aparte del
  escritor) para comprobar la cabecera, las 20 tablas, las rutas, la jerarquía
  de listas, la numeración de entradas y la existencia de audio y ANLZ.

El desglose byte a byte, con tablas de offsets y las referencias a las
especificaciones públicas, está en
[docs/rekordbox-export.md](docs/rekordbox-export.md).

## Uso

1. Arrastra canciones o carpetas al panel central.
2. Pulsa **«Analizar biblioteca»** en la barra izquierda. La app decodifica
   cada pista con FFmpeg en un hilo de trabajo y guarda BPM, tonalidad,
   beat grid y waveform en SQLite. Hay barra de progreso y estado.
3. En el panel derecho crea una lista (queda seleccionada sola) y añade pistas
   con el botón `+` de cada fila.
4. Pulsa **«Exportar a USB…»**: se abre un selector con las memorias USB
   conectadas (se excluyen los discos internos). Elige una y pulsa
   **«Exportar aquí»**. Si quedan pistas sin analizar, el propio diálogo
   avisa y ofrece analizarlas.
5. La copia del audio (que puede ser de varios GB a un pendrive FAT32) se hace
   **en segundo plano**: la ventana sigue respondiendo, muestra el progreso
   pista a pista y permite cancelar. Al terminar se valida lo escrito.
6. «Otra carpeta…» permite exportar a cualquier directorio, por ejemplo para
   probar sin hardware.

> Para probar en un CDJ real: formatea el USB en **FAT32/MBR**
> (`mkfs.fat -F 32 -S 512`) y selecciónalo en el diálogo.

## Análisis musical

`AudioAnalysisService` decodifica a PCM mono de 22050 Hz con `ffmpeg` y calcula:

- **BPM** por autocorrelación de una envolvente de onsets (con refuerzo de
  armónicos y corrección de octava);
- **beat grid** alineando la fase con los onsets más fuertes, con numeración
  de compás 1–4;
- **tonalidad** con un cromagrama (FFT de 4096 puntos) correlacionado con los
  perfiles de Krumhansl-Schmuckler, convertida a notación Camelot ("8A", …);
- **waveform** de picos a 150 columnas por segundo.

Es una **estimación propia**, no un análisis de rekordbox: suficiente para que
un reproductor muestre beat grid, tonalidad y forma de onda, pero no idéntica
a la de Pioneer.

## Limitaciones actuales

- Importa metadatos (título, artista, álbum, género, año, duración, sample
  rate, profundidad, bitrate, canales) y los guarda en SQLite con
  deduplicación por hash SHA-256.
- **No** escribe `exportLibrary.db` (Device Library Plus), que es donde
  rekordbox 7 guarda las listas. Las listas se escriben en `export.pdb`, que es
  lo que leen los CDJ-2000NXS2, CDJ-3000 y los XDJ-RX.
- Los hot cues, memory cues y loops se escriben en ANLZ si existen en la
  biblioteca (tabla `track_cues`), pero todavía no hay interfaz para crearlos.
- La importación es síncrona; el análisis y la exportación pesada van en hilos
  de trabajo, con progreso y cancelación.
- Una exportación a un USB lento puede tardar minutos: mientras tanto la
  interfaz sigue activa y muestra el avance.

## Roadmap

1. ✅ Fase 1 — proyecto, UI, drag & drop, SQLite, importación de metadatos.
2. ✅ Fase 2 — BPM, tonalidad, beatgrid y waveform (con FFmpeg).
3. ⏳ Fase 3 — cues y loops en la interfaz.
4. ✅ Fase 4 — escritor DeviceSQL (`export.pdb`) con las veinte tablas.
5. ✅ Fase 5 — escritor ANLZ (`.DAT`, `.EXT`, `.2EX`).
6. ✅ Fase 6 — detección de memorias USB y validación de la exportación.
7. ⏳ Fase 7 — Device Library Plus, exportación incremental, empaquetado.

## Licencia

Véase [LICENSE](LICENSE). El objetivo es GPL-3.0-or-later; revisar obligaciones
de Qt (LGPL), TagLib (LGPL/MPL) y FFmpeg (LGPL/GPL) antes de distribuir.
