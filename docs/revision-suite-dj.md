# Revisión funcional de la suite DJ

Actualización: 13 de noviembre de 2026. Revisión del código local; no incluye
pruebas en reproductores físicos.

## Soluciones aplicadas

| Problema | Solución |
|---|---|
| Controles de ventanas poco visibles | Barra con minimizar, maximizar/restaurar y cerrar en exportador y conversor. |
| Toda la música aparecía junta | Vista de playlists, búsqueda, selección múltiple y edición por lote. |
| Falta de jerarquía | Carpetas anidadas, árbol indentado, creación dentro de carpetas y diálogo Mover con prevención de ciclos. |
| Orden de sets | Arrastre por el asa ⠿ para cambiar el orden de pistas y listas; zona para soltar al final del set. |
| Ordenar sin modificar el set | Selector de título, artista, BPM, tono y duración; el orden de presentación no modifica el orden guardado. |
| Exportación de toda la colección de listas | Selector de listas/carpetas; seleccionar una carpeta incluye sus descendientes. Se conservan los ancestros de las listas seleccionadas. |
| Contadores duplicados | Conteo de pistas únicas y resumen de audio, espacio libre, destino y archivos ausentes antes de exportar. |
| WAV sobrescritos | Nombres únicos frente al disco y la cola; conversión en temporal y publicación sin reemplazar destinos existentes. |
| WAV parcial o sin verificar | Solo se publica después de restaurar metadata y verificar el PCM. Temporales eliminados ante error/cancelación. |
| Verificación consumía mucha memoria | SHA-256 del PCM calculado por bloques. |
| Cancelación lenta de FFmpeg | Consulta de cancelación cada 50 ms durante conversión, verificación y decodificación de análisis. |
| Hilos destruidos antes de acabar | Señal de salida directa al hilo, cancelación y espera hasta su terminación; limpieza antes de reutilizar controladores. |
| Copia interrumpida al USB | Copia por bloques con cancelación y QSaveFile; conserva la versión anterior si esa copia no se publica. |
| Importación bloqueaba la interfaz | Descubrimiento, metadata, hash y escritura en un hilo con conexión SQLite propia; progreso, cancelación y botones de archivos/carpetas. |
| Ediciones de playlists parcialmente guardadas | Savepoints en el repositorio: actualización y reemplazo de entradas son atómicos y componibles con transacciones externas. |
| Sin preescucha | Reproductor Qt Multimedia desde tabla/cue; barra con pausa, reproducción, detener, posición, duración, volumen y silencio. |
| Lectura incorrecta de entradas ordenadas | Asociación por identificador en lugar de asumir que orden visual e id coinciden. |

## Uso

1. Importar archivos o una carpeta desde la barra inferior, o arrastrarlos.
2. Elegir una carpeta de destino para crear listas o subcarpetas.
3. Seleccionar una playlist y abrir «Toda la música / añadir pistas». Marcar
   temas y pulsar Añadir, o usar el + de cada fila.
4. Pulsar la playlist para ver sus temas; Quitar solo elimina pertenencias.
5. Para reordenar el set, seleccionar «Orden del set», sin Descendente, y
   arrastrar ⠿ sobre otra pista o sobre la zona del final. Las listas se
   reordenan antes de la fila de destino; Mover cambia su carpeta.
6. En Exportar, elegir todas las listas o marcar listas/carpetas y revisar
   el destino y los tamaños. El tamaño mostrado es el audio, no una garantía
   del espacio total requerido por bases, análisis y temporales.
7. En el conversor, los nombres ocupados reciben sufijos « (2)», « (3)»…;
   si otro proceso ocupa el destino durante la conversión, se informa del
   conflicto y se conserva el archivo existente.

## Alcance y límites comprobables

- Los archivos originales no se mueven al organizar playlists. El audio exportado
  conserva la organización por artista/álbum y la jerarquía vive en la base USB.
- La exportación reemplaza las bases del destino con la selección; no fusiona
  otras bibliotecas. La app valida una versión preparada en el USB antes de
  publicarla y conserva los archivos sustituidos mientras dura la publicación.
  Si se desconecta el dispositivo durante esa fase, la siguiente exportación
  al destino intenta restaurar primero la versión anterior. El sistema de
  archivos sigue sujeto a daños físicos y a retiradas sin expulsión.
- El análisis consulta cancelación durante FFmpeg y entre etapas; una etapa de
  cálculo CPU ya iniciada debe terminar. La escritura de bases/cifrado tampoco
  promete cancelación instantánea. Nunca se destruye un worker aún ejecutándose.
- La preescucha requiere Qt Multimedia y salida de audio disponible. La barra
  permanece visible y permite pausar/reanudar sin reiniciar la canción, buscar
  una posición, detener, ajustar volumen y silenciar. También está disponible
  en el editor de cues. No se ha comprobado el sonido físico en esta sesión.
- El cierre normal de la ventana principal se bloquea con trabajos activos y
  explica cómo cancelarlos; ocultar el conversor permite continuar su trabajo.
- Los nombres de salida se reservan para la cola y contra archivos existentes;
  no se sobrescriben archivos para resolver colisiones.

## Validación

Pruebas de repositorio/controladores e integración para persistencia, selección,
jerarquía, orden, importación, exportación, colisiones WAV, cancelación y PCM.
La prueba de interfaz carga Main.qml con todos los controladores reales y una
biblioteca temporal. Los resultados definitivos se obtienen con:

```sh
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

La compatibilidad en CDJ/XDJ requiere hardware. Véase la matriz y protocolo en
[compatibility.md](compatibility.md). No se ha elevado ningún modelo al estado
HARDWARE_TESTED.
