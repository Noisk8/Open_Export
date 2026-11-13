# Open Deck Export

Aplicación de escritorio **open source** para analizar música y preparar
memorias USB compatibles con reproductores **Pioneer DJ** (formato rekordbox),
sin depender de rekordbox.

**Estado: Fases 1–7 completadas.** Biblioteca SQLite, importación, análisis
musical (BPM, tonalidad, beat grid y waveform), editor de cues y loops,
`export.pdb` (DeviceSQL), `exportLibrary.db` (OneLibrary / Device Library Plus
cifrado con SQLCipher 4), ANLZ, detección de memorias USB, exportación
incremental, conversor FLAC → WAV sin pérdida y empaquetado (TGZ/DEB).
Pendiente únicamente la **validación en hardware real** y `exportExt.pdb`.
Ver [docs/compatibility.md](docs/compatibility.md) para el estado exacto.

## Objetivo

Replicar el flujo del modo *EXPORT* de Rekordbox con código propio:

1. Arrastrar canciones o importar carpetas.
2. Analizar (BPM, tonalidad, beatgrid, waveform) — en un hilo de trabajo, con FFmpeg.
3. Crear y editar playlists, hot cues, memory cues y loops.
4. Exportar a una memoria USB detectada automáticamente, generando `export.pdb`
   (DeviceSQL) y los análisis `ANLZ`.
5. Validar las exportaciones antes de darlas por buenas.
6. Convertir FLAC a WAV sin pérdida conservando toda la metadata.

> **Importante**: este proyecto **no afirma compatibilidad con hardware** salvo
> que una exportación haya sido **probada físicamente** en un reproductor. Se
> distingue entre *generada*, *validada estructuralmente* y *probada en hardware*.

## Requisitos

- Linux (inicialmente; Windows/macOS después).
- C++20, CMake ≥ 3.22, Ninja o Make.
- Qt 6.5+ (Core, Gui, Qml, Quick, QuickControls2, QuickLayouts, Sql, Dialogs).
- TagLib ≥ 1.12.
- SQLite3.
- **FFmpeg** (`ffmpeg` en el `PATH`) para el análisis musical y el conversor.
- **OpenSSL / libcrypto** (`libssl-dev`) para el cifrado SQLCipher 4.

En Ubuntu/Debian:

```sh
sudo apt install cmake ninja-build g++ ffmpeg \
    qt6-base-dev qt6-declarative-dev qt6-tools-dev \
    qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
    libsqlite3-dev libtag1-dev libssl-dev
```

## Compilación

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/open-deck-export
```

## Instalación y comando `open-deck-export`

Para poder lanzar la aplicación con el comando `open-deck-export` desde
cualquier directorio (y tener un acceso directo en el menú de aplicaciones):

```sh
cmake --install build --prefix "$HOME/.local"
open-deck-export
```

Esto instala en `~/.local`:

| Ruta | Contenido |
|---|---|
| `bin/open-deck-export` | el ejecutable |
| `bin/OpenDeckExport/` | el módulo QML (`qmldir` + `.qml`) que el motor necesita en el import path |
| `share/applications/open-deck-export.desktop` | acceso directo del menú, con `Exec=open-deck-export` |
| `share/icons/hicolor/scalable/apps/open-deck-export.svg` | icono |
| `share/doc/open-deck-export/` | README, licencia y `docs/` |

`~/.local/bin` debe estar en el `PATH` (lo está por defecto en la mayoría de
las distribuciones). Para un icono en el escritorio, copia el `.desktop` a
`~/Desktop/` y márcalo como confiable:

```sh
cp ~/.local/share/applications/open-deck-export.desktop ~/Desktop/
chmod +x ~/Desktop/open-deck-export.desktop
gio set ~/Desktop/open-deck-export.desktop metadata::trusted true
```

> El directorio `bin/OpenDeckExport/` no es opcional: `loadFromModule()` de Qt
> necesita el `qmldir` en el import path. Sin él, un binario instalado fuera del
> directorio de compilación falla con *«Module OpenDeckExport contains no type
> named Main»* aunque el QML también esté compilado dentro del `qrc`.

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
- `src/export/onelibrary` — `DeviceLibraryWriter` (esquema de 22 tablas de
  OneLibrary) y `SqlCipherWriter` (cifrado SQLCipher 4 con OpenSSL).
- `src/export` — `ExportManager`, `ExportManifest` (exportación incremental) y
  `ExportValidator` (relectura independiente).
- `src/ui` — interfaz Qt Quick/QML: `LibraryController`, `PlaylistController`,
  `CueController`, `DeviceController`, `AnalysisController`, `ExportController`
  y `ConversionController`. El análisis, la exportación y la conversión corren
  en hilos aparte; la ventana del conversor es independiente.

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
| 7 | **Validación** | Un lector independiente vuelve a leer `export.pdb` y comprueba rutas, jerarquía, entradas y que existan audio y ANLZ; además descifra y consulta `exportLibrary.db`. | `ExportValidator` |
| 8 | **Incremental** | Un manifiesto JSON con el hash de cada pista permite reutilizar audio y ANLZ sin cambios y borrar los huérfanos. | `ExportManifest` |

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

### 5. `exportLibrary.db` (OneLibrary / Device Library Plus)

Para los reproductores de 2023 en adelante (CDJ-3000X, XDJ-AZ, OPUS-QUAD,
OMNIS-DUO) se escribe además una base SQLite **cifrada con SQLCipher 4** con
22 tablas (`content`, `playlist`, `playlist_content`, `cue`, `artist`, `album`,
`genre`, `label`, `key`, `color`, `image`, `menuItem`, `category`, `sort`,
`property`, …). La clave es pública y está documentada por `pyrekordbox`.

El cifrado se implementa con **OpenSSL** (sin depender de la librería
SQLCipher): página de 4096 bytes con 80 reservados, `PBKDF2-HMAC-SHA512`
(256000 iteraciones) para la clave AES-256-CBC, una segunda derivación para la
clave HMAC, IV por página y `HMAC-SHA512(ciphertext || IV || nº de página)`.
El formato se ha **verificado abriendo el resultado con el CLI real de
SQLCipher 4.13** (`PRAGMA integrity_check` → `ok`).

### 6. Exportación incremental

`/PIONEER/rekordbox/.open-deck-manifest.json` guarda un hash por pista
(metadatos + beat grid + cues + waveform + carátula). En la siguiente
exportación se comparan: las pistas sin cambios no se vuelven a copiar ni a
analizar, las cambiadas se reescriben y las que desaparecieron se borran.
`export.pdb` y `exportLibrary.db` se regeneran siempre (son baratos).
Se activa con la casilla «Exportación incremental» del diálogo.

### 7. Concurrencia y validación

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
2. En cuanto importas pistas nuevas, el contador **«N pistas pendientes»** se
   actualiza al instante y se habilita **«Analizar biblioteca»** (barra
   izquierda). La app decodifica cada pista con FFmpeg en un hilo de trabajo y
   guarda BPM, tonalidad, beat grid y waveform en SQLite, con progreso y estado.
   También puedes analizar desde el propio diálogo de exportación.
3. En el panel derecho crea una lista (queda seleccionada sola) y añade pistas
   con el botón `+` de cada fila. El botón **«Cues»** de cada pista abre el
   editor de hot cues, memory cues y loops (posición, duración del loop,
   color, nombre y comentario).
4. Pulsa **«Exportar a USB…»**: se abre un selector con las memorias USB
   conectadas (se excluyen los discos internos). Elige una y pulsa
   **«Exportar aquí»**. Si quedan pistas sin analizar, el propio diálogo
   avisa y ofrece analizarlas.
5. La copia del audio (que puede ser de varios GB a un pendrive FAT32) se hace
   **en segundo plano**: la ventana sigue respondiendo, muestra el progreso
   pista a pista y permite cancelar. Al terminar se valida lo escrito.
6. En el diálogo, si quedan pistas sin analizar, aparece un aviso con
   **«Analizar ahora»** y, en cada dispositivo, **«Analizar + exportar»**:
   analiza en segundo plano y exporta automáticamente al terminar. También
   puedes activar/desactivar **Device Library Plus** y la **exportación
   incremental**. «Otra carpeta…» permite exportar a cualquier directorio.

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

## Conversión FLAC → WAV

El botón **«Convertir FLAC → WAV»** abre una **ventana independiente** con su
propia cola de archivos (arrastrar, añadir archivos o una carpeta) y carpeta de
destino. Cada conversión:

1. Lee con TagLib el *sample rate*, los canales y la **profundidad de bits**
   del FLAC, y decide el codec PCM (`pcm_s16le` / `pcm_s24le` / `pcm_s32le`).
2. Ejecuta `ffmpeg` con `-map_metadata 0`, sin resampleo, sin dithering y sin
   paso con pérdida.
3. Reescribe **todos** los tags (incluidas claves Vorbis arbitrarias e ISRC)
   y la carátula embebida en el WAV mediante TagLib (ID3v2 / INFO).
4. **Verifica que es bit-exact**: decodifica el FLAC y el WAV a PCM `f64le` y
   compara sus SHA-256. Si no coinciden, marca el archivo como error.

Todo corre en un hilo aparte, con progreso, cancelación y estado por archivo.
Verificado en `tests/unit/test_conversion.cpp` con un FLAC real con metadata.

## Empaquetado

```sh
./scripts/release.sh                 # build Release + tests + TGZ y DEB
./scripts/release.sh build-release   # directorio de build alternativo
```

`cmake/Packaging.cmake` instala el binario, el `.desktop`, el icono SVG y la
documentación, y configura CPack (TGZ + DEB). `include(cmake/Packaging.cmake)`
se ejecuta al final del `CMakeLists.txt`.

## Limitaciones actuales

- Importa metadatos (título, artista, álbum, género, año, duración, sample
  rate, profundidad, bitrate, canales) y los guarda en SQLite con
  deduplicación por hash SHA-256.
- Se escriben **los dos formatos**: `export.pdb` (CDJ-2000NXS2, CDJ-3000,
  XDJ-RX) y `exportLibrary.db` (CDJ-3000X, XDJ-AZ, OPUS-QUAD, OMNIS-DUO).
- `exportExt.pdb` (My Tags / menús extendidos) todavía no se genera; ningún
  reproductor lo necesita para leer las listas.
- La importación es síncrona; el análisis, la exportación y la conversión van
  en hilos de trabajo con progreso y cancelación.
- Una exportación a un USB lento puede tardar minutos: mientras tanto la
  interfaz sigue activa y muestra el avance.

## Roadmap

1. ✅ Fase 1 — proyecto, UI, drag & drop, SQLite, importación de metadatos.
2. ✅ Fase 2 — BPM, tonalidad, beatgrid y waveform (con FFmpeg).
3. ✅ Fase 3 — editor de hot cues, memory cues y loops en la interfaz.
4. ✅ Fase 4 — escritor DeviceSQL (`export.pdb`) con las veinte tablas.
5. ✅ Fase 5 — escritor ANLZ (`.DAT`, `.EXT`, `.2EX`).
6. ✅ Fase 6 — detección de memorias USB y validación de la exportación.
7. ✅ Fase 7 — Device Library Plus (SQLCipher 4), exportación incremental,
   empaquetado TGZ/DEB y conversor FLAC → WAV sin pérdida.
8. ⏳ Fase 8 — `exportExt.pdb`, hot cue banks y **pruebas en hardware real**.

## Licencia

Véase [LICENSE](LICENSE). El objetivo es GPL-3.0-or-later; revisar obligaciones
de Qt (LGPL), TagLib (LGPL/MPL) y FFmpeg (LGPL/GPL) antes de distribuir.
