# Exportación a Pioneer DJ: cómo lee el reproductor las listas de reproducción

Este documento recoge la investigación que hay detrás del escritor de
`export.pdb` y de los ficheros ANLZ, y explica cómo está implementado en este
proyecto.

> **Aviso**: el proyecto **no** afirma compatibilidad con hardware. Una
> exportación se considera *generada* y *validada estructuralmente*; sólo
> pasa a ser *probada en hardware* cuando alguien la ha probado en un
> reproductor concreto.

## 1. Qué lee un CDJ o un XDJ-RX de una memoria USB

Cuando se inserta una memoria en un reproductor, el firmware busca el
sistema de archivos de la Pioneer en la raíz del volumen:

```text
/Contents/<artista>/<álbum>/<archivo>        el audio
/PIONEER/rekordbox/export.pdb                la base de datos DeviceSQL
/PIONEER/rekordbox/exportExt.pdb             las etiquetas (My Tags)
/PIONEER/rekordbox/exportLibrary.db          Device Library Plus (SQLCipher)
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.DAT    el análisis de cada pista
/PIONEER/Artwork/<carpeta>/a<n>.jpg          las carátulas
/PIONEER/{DEVSETTING,MYSETTING,…}.DAT        ajustes del reproductor
```

El reproductor **no** lee un M3U ni una carpeta de archivos: lo que lee es
`export.pdb`, una base de datos relacional (DeviceSQL) con veinte tablas. Las
listas de reproducción viven en dos de esas tablas:

| Tipo | Tabla | Contenido |
|------|-------|-----------|
| `07` | `playlist_tree` | La jerarquía del menú: playlists y carpetas. |
| `08` | `playlist_entries` | Las pistas de cada lista, en orden. |

La tabla de pistas es la `00`, y sus filas referencian a las tablas pequeñas
(`01` géneros, `02` artistas, `03` álbumes, `04` sellos, `05` tonalidades,
`06` colores, `0d` carátulas) por identificador.

**Consecuencia práctica**: para que un reproductor muestre una lista con todos
los metadatos hay que escribir, como mínimo, `export.pdb` con las tablas `00`,
`01`, `02`, `03`, `05`, `06`, `07` y `08` bien rellenadas. Los datos que no
quepan en la base de datos (beat grid, waveform, cues) van en los ficheros
ANLZ, y la fila de la pista apunta a ellos con la cadena `analyze_path`.

## 2. Formato DeviceSQL (`export.pdb`)

### 2.1 Cabecera del fichero

Todo es *little endian*. La primera página (4096 bytes) lleva la cabecera:

| Offset | Tipo | Contenido |
|--------|------|-----------|
| `00` | u32 | Siempre 0. |
| `04` | u32 | Tamaño de página: 4096. |
| `08` | u32 | Número de tablas: 20. |
| `0c` | u32 | Primera página libre. |
| `10` | u32 | Desconocido (4 o 5; los reproductores aceptan ambos). |
| `14` | u32 | Contador de secuencia global. |
| `18` | u32 | Relleno a cero. |
| `1c` | 20×16 | Directorio de tablas. |

Cada entrada del directorio es `u32 type, u32 empty_candidate, u32 first_page,
u32 last_page`.

**Las veinte tablas tienen que estar presentes**, aunque estén vacías, y en
orden. Esto importa: el reproductor recorre el directorio por posición.

### 2.2 Páginas

Una tabla es una lista enlazada de páginas. La primera es una página *sentinel*
(índice de página `0x64`) que no contiene filas; encadena hacia la primera
página de datos (`0x24`).

rekordbox reserva las páginas *sentinel* en los índices impares (1, 3, 5, …,
39): la tabla *n* usa la página `2n+1`. Las páginas de datos se asignan a
partir de ahí.

Cabecera de página de datos (40 bytes = `0x28`):

| Offset | Contenido |
|--------|-----------|
| `00` u32 | 0. |
| `04` u32 | Índice de la página. |
| `08` u32 | Tipo de tabla. |
| `0c` u32 | Página siguiente de la cadena. |
| `10` u32 | Identificador de transacción de la última escritura. |
| `14` u32 | 0. |
| `18` | 13 bits de huecos asignados, 11 bits de filas presentes. |
| `1b` | Flags: `0x24` datos, `0x34` datos con borrados, `0x64` sentinel. |
| `1c` u16 | Espacio libre del montón. |
| `1e` u16 | Espacio usado del montón. |
| `20` u16 | Filas escritas en la última transacción. |
| `22` u16 | Primera fila de esa transacción. |
| `24`, `26` u16 | 0. |

El montón de filas empieza en `0x28` y crece hacia arriba. El índice que
localiza las filas se construye **hacia atrás** desde el final de la página,
en grupos de dieciséis huecos. Cada grupo ocupa 36 bytes:

```text
block_end-2   máscara de la última transacción
block_end-4   máscara de presencia (bit i = el hueco 16g+i existe)
block_end-6   desplazamiento del montón del hueco 16g+0
block_end-8   el hueco 16g+1 … y así hacia abajo
```

### 2.3 Cadenas DeviceSQL

Las cadenas variables se codifican con un primer byte que indica el formato:

* **ASCII corto**: byte impar. La longitud de todo el campo es el byte
  desplazado una posición a la derecha. Máximo 126 caracteres. La cadena
  vacía es el byte `0x03`.
* **ASCII largo**: byte `0x40`, seguido de una longitud de 16 bits (el tamaño
  total del campo, cabecera incluida), un byte de relleno y el texto.
* **UTF-16LE largo**: byte `0x90`, con la misma cabecera de longitud.

rekordbox **alinea a cuatro bytes** las cadenas largas respecto al inicio de la
fila; las cortas se empaquetan justo detrás de lo anterior. Esta alineación se
ha relacionado con un bloqueo del CDJ-2000NXS, así que la implementación la
respeta.

El ISRC es un caso aparte: usa el formato `0x90`, pero el cuerpo empieza por
`0x03` y sigue con ASCII terminado en NUL.

### 2.4 La fila de pista (tabla `00`)

Cabecera fija de `0x88` bytes y 21 desplazamientos de 16 bits a partir de
`0x5e`:

| Offset | Campo |
|--------|-------|
| `00` u16 | Siempre `0x0024`. |
| `02` u16 | *Index shift*: 32 × número de hueco en la página. |
| `04` u32 | `0x000c0700`. |
| `08` u32 | Frecuencia de muestreo. |
| `0c` u32 | Id del compositor (tabla de artistas). |
| `10` u32 | Tamaño del fichero de audio. |
| `14` u32 | Identificador del contenido en la base maestra. |
| `18` u16 | `0x2fdb`. |
| `1a` u16 | `0x8f45`. |
| `1c` u32 | Id de la carátula. |
| `20` u32 | Id de la tonalidad. |
| `24` u32 | Id del artista original. |
| `28` u32 | Id del sello. |
| `2c` u32 | Id del remixer. |
| `30` u32 | Bitrate en kbps. |
| `34` u32 | Número de pista dentro del álbum. |
| `38` u32 | Tempo = BPM × 100. |
| `3c` u32 | Id del género. |
| `40` u32 | Id del álbum. |
| `44` u32 | Id del artista. |
| `48` u32 | Id de la pista (1, 2, 3…). |
| `4c` u16 | Número de disco. |
| `4e` u16 | Veces reproducida. |
| `50` u16 | Año. |
| `52` u16 | Profundidad de bits. |
| `54` u16 | Duración en segundos. |
| `56` u16 | 41. |
| `58` u8 | Id del color. |
| `59` u8 | Valoración (0–5). |
| `5a` u16 | Tipo de fichero: 1 MP3, 4 M4A, 5 FLAC, 11 WAV, 12 AIFF. |
| `5c` u16 | 3. |

Las 21 cadenas, en orden: `isrc`, `texter`, contador de información, contador
de análisis, contador de cues, mensaje, `kuvo_public` ("ON"),
`autoload_hotcues` ("ON"), dos desconocidas, `date_added`, `release_date`,
`mix_name`, desconocida, **`analyze_path`**, `analyze_date`, `comment`,
**`title`**, desconocida, **`filename`** y **`file_path`**.

Las tres últimas en negrita son las que el reproductor usa para encontrar los
ficheros: la ruta del audio y la del análisis.

### 2.5 Las tablas pequeñas

* **género (01) y sello (04)**: `u32 id`, cadena con el nombre.
* **tonalidad (05)**: `u32 id`, `u32 id` (copia), cadena con el nombre en
  notación Camelot ("8A", "12B"…).
* **artista (02)**: `u16 subtype` (`0x60`), `u16 index shift`, `u32 id`,
  `u8 0x03`, `u8` desplazamiento del nombre; el nombre va detrás.
* **álbum (03)**: `u16 0x80`, `u16 index shift`, `u32 0`, `u32 id del artista`,
  `u32 id`, `u32 0`, `u8 0x03`, `u8` desplazamiento del nombre.
* **color (06)**: `u32 0`, `u8 id`, `u16 id`, `u8 0`, cadena. Siempre hay
  ocho: Pink, Red, Orange, Yellow, Green, Aqua, Blue, Purple.

### 2.6 Las listas de reproducción

La tabla `07` (`playlist_tree`) es la jerarquía del menú:

```text
u32 parent_id   la carpeta que contiene esta entrada (0 = raíz)
u32 desconocido
u32 sort_order  el orden dentro de su padre
u32 id          identificador de la fila
u32 raw_is_folder  distinto de cero si es una carpeta
cadena name
```

rekordbox **no** escribe una fila de raíz: las listas de primer nivel
simplemente tienen `parent_id = 0`. Las carpetas sí se exportan, y el
reproductor las muestra como carpetas del menú.

La tabla `08` (`playlist_entries`) es plana:

```text
u32 entry_index  la posición dentro de la lista (1, 2, 3…)
u32 track_id     la pista
u32 playlist_id  la lista
```

Una pista puede repetirse en la misma lista.

### 2.7 Las tablas de menú (16, 17, 18)

El reproductor usa estas tablas para construir el menú de navegación: qué
columnas se pueden recorrer (16), el árbol de navegación (17) y el menú de
ordenación (18). Son datos estáticos que rekordbox escribe siempre, y sin ellos
el reproductor puede mostrar la biblioteca sin opciones de navegación.

* **16**: `u16 id`, `u16 kind`, cadena con el nombre. El nombre va envuelto en
  `U+FFFA` … `U+FFFB`. Hay 27 filas: GENRE, ARTIST, ALBUM, TRACK, BPM,
  RATING, YEAR, REMIXER, LABEL, ORIGINAL ARTIST, KEY, CUE, COLOR, TIME,
  BITRATE, FILE NAME, PLAYLIST, HOT CUE BANK, HISTORY, SEARCH, COMMENTS,
  DATE ADDED, DJ PLAY COUNT, FOLDER, DEFAULT, ALPHABET y MATCHING.
* **17**: `u16 id de menú`, `u16 secuencia`, `u32 flags`. 22 filas.
* **18**: `u16 id de menú`, `u16 secuencia`, `u32 visible`. 17 filas.

## 3. Los ficheros de análisis (ANLZ)

El `export.pdb` no cabe la información musical: el beat grid, la waveform y los
cues viven en ficheros aparte, en
`PIONEER/USBANLZ/P###/<hash>/ANLZ0000.{DAT,EXT,2EX}`. La fila de la pista
apunta al `.DAT` con `analyze_path`; el reproductor deduce el `.EXT` y el
`.2EX` cambiando la extensión.

**Todos los enteros de estos ficheros son *big endian***, al contrario que en
`export.pdb`.

Cabecera de fichero (28 bytes): `"PMAI"`, `u32 0x1c`, `u32` longitud del
fichero, y cuatro palabras constantes (`0x00010000`, `0x00010000`,
`0x00010000`, `0x00000000`). A partir de ahí van secciones etiquetadas:
`fourcc` (4), `u32` tamaño de la cabecera, `u32` tamaño total de la sección.

| Sección | Fichero | Contenido |
|---------|---------|-----------|
| `PPTH` | los tres | Ruta del audio, en UTF-16 **big endian** con NUL final. |
| `PVBR` | `.DAT` | Índice de búsqueda para MP3 de bitrate variable. |
| `PQTZ` | `.DAT` | Beat grid: posición en el compás, tempo y tiempo en ms. |
| `PWAV` | `.DAT` | Preview monocromo de 400 columnas. |
| `PWV2` | `.DAT` | Preview monocromo pequeño de 100 columnas. |
| `PCOB` | `.DAT` | Lista clásica de cues (`PCPT`). |
| `PWV3` | `.EXT` | Waveform monocroma que se desplaza, 150 columnas/s. |
| `PCO2` | `.EXT` | Lista extendida de cues (`PCP2`), con color y comentario. |
| `PWV4` | `.EXT` | Preview de color, 1200 columnas × 6 bytes. |
| `PWV5` | `.EXT` | Waveform de color, 150 columnas/s × 2 bytes. |
| `PWV6` | `.2EX` | Preview de tres bandas del CDJ-3000. |
| `PWV7` | `.2EX` | Waveform de tres bandas, 150 columnas/s × 3 bytes. |

El orden de las secciones es fijo en los ficheros que escribe rekordbox.

### 3.1 El nombre del directorio

El directorio `P###/<hash>` **no** es arbitrario: es un hash de la ruta del
audio. El firmware del CDJ-3000 lo recalcula y busca el análisis ahí, en vez
de fiarse de `analyze_path`. El algoritmo, sobre las unidades de código UTF-16
de la ruta:

```text
h = 0
para cada carácter c de la ruta:
    h = (h * 0x34F5501D + c * 0x93B6) mod 2^32
h = h mod 0x30D43
shard = bits 0, 2, 6, 7, 9, 13 y 16 de h, empaquetados en siete bits
directorio = "P%03X/%08X" % (shard, h)
```

## 4. Qué hace esta implementación

### 4.1 Escritores (`src/export/`)

| Fichero | Responsabilidad |
|---------|-----------------|
| `devicesql/DeviceSqlString.*` | Codificación de las cadenas DeviceSQL. |
| `devicesql/DeviceSqlRow.*` | Fila con cabecera fija y montón de cadenas. |
| `devicesql/DeviceSqlPage.*` | Página de datos y página *sentinel*. |
| `devicesql/DeviceSqlDatabase.*` | Las veinte tablas y la asignación de páginas. |
| `devicesql/DeviceSqlWriter.*` | Las filas de cada tabla. |
| `devicesql/DeviceSqlReader.*` | Lector independiente, para validar. |
| `anlz/AnlzWriter.*` | Los ficheros `.DAT`, `.EXT` y `.2EX`. |
| `ExportManager.*` | Escribe el volumen completo. |
| `ExportValidator.*` | Comprueba lo escrito. |

El escritor produce:

* las veinte tablas, con sus páginas *sentinel* en los índices impares;
* las tablas pequeñas con los valores internacionalizados en el orden en que
  aparecen por primera vez;
* las filas de pista con **todos** los metadatos: artista, álbum, género,
  sello, tonalidad, color, valoración, BPM, bitrate, frecuencia y profundidad
  de muestreo, duración, año, número de pista y disco, ISRC, comentario,
  nombre de la mezcla, `kuvo_public`, `autoload_hotcues`, `date_added`, la
  ruta del audio y la del análisis;
* la jerarquía completa de listas y carpetas, con `parent_id`, `sort_order` y
  `raw_is_folder`;
* las entradas de las listas, con su orden;
* las tablas estáticas de menú (16, 17, 18) y el resumen de la exportación
  (19).

### 4.2 El volumen

`ExportManager` escribe:

```text
/Contents/<artista>/<álbum>/<título>.<ext>
/PIONEER/rekordbox/export.pdb
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.DAT
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.EXT
/PIONEER/USBANLZ/P###/<hash>/ANLZ0000.2EX
/PIONEER/Artwork/<carpeta>/a<n>.jpg
```

El audio se coloca en una carpeta por artista y otra por álbum, como hace
rekordbox, y el nombre del fichero se limita a 43 caracteres.

### 4.3 La validación

`ExportValidator` vuelve a leer lo escrito **con un lector independiente** (no
reutiliza el escritor) y comprueba:

* que la cabecera y el directorio de las veinte tablas son coherentes;
* que cada fila de pista tiene título y una ruta válida;
* que los padres de la jerarquía de listas existen;
* que las entradas apuntan a pistas y listas que existen, y que el índice de
  posición es consecutivo;
* que existen los ficheros de audio y de análisis que la base de datos
  referencia.

### 4.4 El análisis

El `beat grid`, la waveform y los cues salen de las tablas `track_beats`,
`track_waveform` y `track_cues` de la biblioteca local. Esas tablas se rellenan
con `AudioAnalysisService` (`src/audio/`), que decodifica con FFmpeg a PCM mono
de 22050 Hz y calcula:

* el **tempo** por autocorrelación de una envolvente de onsets;
* el **beat grid** alineando la fase con los onsets más fuertes;
* la **tonalidad** con un cromagrama y los perfiles de Krumhansl-Schmuckler,
  en notación Camelot;
* la **waveform** de picos a 150 columnas por segundo.

`AnalysisController` ejecuta el trabajo en un hilo aparte y persiste los
resultados en el hilo de la interfaz. Es una estimación propia, no el análisis
de rekordbox. Si una pista no se ha analizado, los ANLZ se escriben igualmente,
con `PPTH` y waveforms planas y sin `PQTZ`.

### Extensiones opcionales

`exportExt.pdb` usa el contenedor DeviceSQL de nueve tablas. Open Deck Export
escribe las categorías y etiquetas en la tabla 3 y las asociaciones a pistas
en la tabla 4, según el [esquema de Crate Digger](https://github.com/Deep-Symmetry/crate-digger/blob/main/src/main/kaitai/rekordbox_pdb.ksy).
Los bancos referencian los cues de OneLibrary mediante `hotCueBankList` y
`hotCueBankList_cue`. Ambos cambios se publican dentro de la operación
recuperable del USB. Las pruebas físicas siguen pendientes.

## 5. Lo que todavía no se hace

* ~~**Device Library Plus** (`exportLibrary.db`)~~ — **implementado**. Se
  escriben los dos formatos: `export.pdb` para los reproductores clásicos y
  `exportLibrary.db` (SQLCipher 4, 22 tablas) para los de 2023 en adelante.
  El cifrado se hace con OpenSSL y se ha verificado abriéndolo con el CLI real
  de SQLCipher 4.13. Ver la sección «OneLibrary» más abajo.
* **Ajustes del dispositivo** (`MYSETTING.DAT`, etc.): los reproductores los
  leen, pero funcionan con sus valores por defecto si no están.
* **Estructura de frases** (`PSSI`): opcional, sólo afecta al modo
  iluminación del CDJ-3000.
* **`PQT2`**: la variante extendida del beat grid; la implementation escribe
  el `PQTZ` de toda la vida, que es lo que usan todos los reproductores.

## 6. Referencias

* Deep Symmetry, *Rekordbox Export Structure Analysis*:
  <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/>>
  — el formato de `export.pdb` y de los ficheros ANLZ, con la especificación
  Kaitai de crate-digger.
* <https://github.com/Deep-Symmetry/crate-digger/blob/master/src/main/kaitai/rekordbox_pdb.ksy>
  y <https://github.com/Deep-Symmetry/crate-digger/blob/master/src/main/kaitai/rekordbox_anlz.ksy>
  — las estructuras exactas.
* Holzhaus/rekordcrate (MPL-2.0): <https://github.com/Holzhaus/rekordcrate>
  — un lector independiente escrito en Rust.
* Mixxx, `lib/rekordbox-metadata/*.ksy` (EPL-1.0) — las mismas estructuras
  Kaitai, en la versión que usa el proyecto hermano `rex`.
* Kailazy/Ord, *rekordbox USB export — byte-level structure reference*:
  <https://github.com/Kailazy/Ord/blob/main/docs/rekordbox-export-structure.md>
  — el desglose byte a byte de una exportación real de rekordbox 7.2.2 que
  motivó several correcciones (la alineación a cuatro bytes de las cadenas
  largas, el esquema de páginas *sentinel* y la tabla 19).


## 7. OneLibrary / Device Library Plus (`exportLibrary.db`)

Además del formato DeviceSQL se genera una base SQLite cifrada con **SQLCipher 4**
en `/PIONEER/rekordbox/exportLibrary.db`. La leen los CDJ-3000X, XDJ-AZ,
OPUS-QUAD, OMNIS-DUO, djay Pro y Traktor.

### Esquema

22 tablas: `content` (metadatos y rutas de cada pista), `artist`, `album`,
`genre`, `label`, `key`, `color`, `image`, `cue`, `playlist`,
`playlist_content`, `history`, `history_content`, `hotCueBankList`,
`hotCueBankList_cue`, `myTag`, `myTag_content`, `menuItem`, `category`,
`sort`, `property` y `recommendedLike`. Los ids son enteros secuenciales, el
BPM se guarda ×100 y las rutas son relativas a la raíz del volumen.
`property.dbVersion` vale `"1000"`.

### Cifrado SQLCipher 4 (implementado con OpenSSL)

| Parámetro | Valor |
|---|---|
| Página | 4096 bytes, 80 reservados |
| Salt | 16 bytes aleatorios (primeros 16 bytes del fichero) |
| Clave de cifrado | `PBKDF2-HMAC-SHA512(passphrase, salt, 256000, 32)` |
| Salt HMAC | cada byte del salt XOR `0x3a` |
| Clave HMAC | `PBKDF2-HMAC-SHA512(enc_key, hmac_salt, 2, 32)` |
| Cifrado | AES-256-CBC sin padding, IV aleatorio de 16 bytes por página |
| HMAC | `HMAC-SHA512(ct || IV || nº_página_u32_LE)` (64 bytes) |
| Página 1 | los 16 primeros bytes son el salt; se cifran 4000 bytes |

La passphrase es pública y está documentada por `pyrekordbox`:
`r8gddnr4k847830ar6cqzbkk0el6qytmb3trbbx805jm74vez64i5o8fnrqryqls`.

Para que SQLite deje 80 bytes libres por página se parchea el byte 20 de la
cabecera del fichero plano y se ejecuta `VACUUM` antes de cifrar (`PRAGMA
reserve_size` se ignora desde SQL). El resultado se ha validado con el CLI
`sqlcipher` 4.13: `PRAGMA integrity_check` → `ok` y consultas sobre
`content`, `playlist`, `menuItem` y `property`.

## 8. Exportación incremental

`/PIONEER/rekordbox/.open-deck-manifest.json` (JSON, ignorado por los
reproductores) guarda la versión, la fecha, un hash de la estructura de listas
y un hash por pista que cubre metadatos, carátula, beat grid, cues y waveform.

En una exportación incremental posterior:

* las pistas cuyo hash no cambió (y cuyos ficheros siguen existiendo) **no se
  vuelven a copiar ni a reanalizar**;
* las cambiadas se reescriben;
* las que ya no están en la biblioteca se borran del volumen;
* `export.pdb` y `exportLibrary.db` se regeneran siempre.
