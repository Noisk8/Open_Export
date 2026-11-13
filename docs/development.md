# Desarrollo

## Compilar y probar

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Convenciones

- C++20, namespace raíz `opendeck` (subespacios `core`, `library`, `device`,
  `audio`, `devices`, `ui`). `export` es palabra reservada en C++20: el
  namespace del formato de exportación es `opendeck::device`.
- Formato con `clang-format` (reglas de la casa, ver `.clang-format`).
- Cada capa se prueba de forma aislada; la UI no se mezcla con la lógica.
- Migraciones SQLite versionadas: solo **añadir** migraciones, nunca editar una
  ya publicada.
- **Los contadores que consume QML son `Q_PROPERTY`, no `Q_INVOKABLE`.** Un
  método leído sin paréntesis en un binding devuelve un objeto `function`, no un
  número, y la comparación falla en silencio. `tests/unit/test_ui_controllers.cpp`
  lo comprueba evaluando QML real con `QQmlComponent`.
- La interfaz debe seguir siendo usable en ventanas pequeñas: el gestor de
  ventanas de Wayland puede imponer un tamaño menor que `minimumWidth`.

## Migraciones de la biblioteca

| Versión | Contenido |
|---|---|
| 1 | Esquema original: `tracks`, `playlists`, `playlist_entries`. |
| 2 | Metadatos extra en `tracks`, `track_beats`, `track_cues`, `track_waveform` e `is_folder` en `playlists`. |
| 3 | Reparación: una compilación anterior de la v2 omitió `is_folder` en las bases creadas con el esquema v1 original, por lo que las listas no se podían leer ni crear. Es idempotente: comprueba `PRAGMA table_info` antes de hacer `ALTER TABLE`. |

`tests/unit/test_database.cpp` reproduce ambos casos (actualización limpia
desde v1 y reparación de una v2 incompleta).

## Análisis musical

`AudioAnalysisService` necesita el ejecutable `ffmpeg` en el `PATH` (no hace
falta enlazar con libav). El análisis se ejecuta en un `QThread` aparte;
`AnalysisController` recibe los resultados y los persiste **en el hilo de la
interfaz**, porque la conexión SQLite no se comparte entre hilos.

`tests/unit/test_analysis.cpp` usa un WAV sintético con pulsos a 120 BPM y
comprueba tempo, beat grid, resolución de la waveform, notación Camelot y el
ciclo completo del worker. `tests/integration/test_export.cpp` verifica que el
análisis acaba dentro de los ficheros ANLZ (`PQTZ` con beats y `PWAV` con
datos).

## Hilos de trabajo

Tanto el análisis como la exportación copian/decodifican cantidades grandes de
datos y **nunca deben ejecutarse en el hilo de la interfaz**. El patrón es:

- `ExportManager::buildLibrary()` resuelve pistas, listas y datos de análisis
  en el hilo de la interfaz (es rápido y es lo único que toca SQLite).
- `ExportManager::write()` es estático y no accede a la base de datos: recibe
  una `ExportLibrary` ya resuelta y puede correr en cualquier hilo.
- `AnalysisController` / `ExportController` crean un `QThread` y un *worker*;
  los resultados se persisten o se muestran en el hilo de la interfaz mediante
  señales con `Qt::QueuedConnection`.

**Ojo con la cancelación**: el worker se destruye con `deleteLater` cuando el
hilo termina. Guardar un puntero crudo y llamar a `requestCancel()` desde el
destructor del controller produce un *use-after-free* (detectado con
AddressSanitizer). Por eso el flag de cancelación es un
`QAtomicInteger<bool>` propiedad del **controller**, y el worker sólo recibe un
puntero a él; el controller usa `QPointer` y nunca toca el worker después.

`tests/unit/test_export_controller.cpp` comprueba con un `QTimer` que el hilo
de la interfaz sigue procesando eventos mientras se copian datos, y que la
cancelación funciona. La suite completa se ha verificado con
`-fsanitize=address`.

## Detección de USB

`UsbDeviceManager` usa `QStorageInfo` para listar montajes y el sysfs
(`/sys/class/block/<dev>` resuelto) para distinguir un bus USB de un disco
interno: la partición EFI también es `vfat` y hay que excluirla.
`tests/unit/test_devices.cpp` cubre los filtros y `DeviceController` refresca
la lista cada tres segundos.
- La lógica de análisis y exportación debe poder ejecutarse **sin hardware
  Pioneer** (fixtures de audio sintético y datos binarios con licencia).

## Fixtures y referencias de formato

El proyecto NO distribuye bases de datos propietarias. Para los formatos
DeviceSQL/ANLZ se usan especificaciones públicas y fixtures con licencia:

- Deep Symmetry, *Rekordbox Export Structure Analysis*:
  <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/>
- Holzhaus/rekordcrate (MPL-2.0):
  <https://github.com/Holzhaus/rekordcrate>
- Mixxx `lib/rekordbox-metadata/*.ksy` (EPL-1.0).

Toda estructura binaria escrita debe estar respaldada por una especificación o
una prueba de ingeniería inversa verificable, y documentarse en `docs/`.


## OneLibrary, incremental y conversor

* `SqlCipherWriter` implementa el formato de página de SQLCipher 4 con
  `OpenSSL::Crypto` (sin enlazar `libsqlcipher`). `tests/unit/test_onelibrary.cpp`
  comprueba el round-trip cifrado/descifrado, la detección de HMAC alterado y
  el esquema de 22 tablas. El formato se ha verificado además con el CLI real
  de SQLCipher 4.13.
* `ExportManifest` guarda hashes por pista. `tests/integration/test_incremental.cpp`
  comprueba que la segunda exportación no recopia nada, que un cambio de BPM
  fuerza una sola reescritura y que las pistas eliminadas se borran.
* `ConversionController` / `ConversionWorker` convierten FLAC → WAV en un hilo
  aparte. `tests/unit/test_conversion.cpp` genera un FLAC real con ffmpeg,
  comprueba que el PCM decodificado de ambos ficheros tiene el mismo SHA-256
  y que los tags (incluido ISRC) sobreviven al WAV.
* Todos los workers usan el mismo patrón: flag de cancelación atómico en el
  controlador + `QPointer` al worker, para evitar el *use-after-free* de
  `deleteLater` (ver la sección «Hilos de trabajo»).
