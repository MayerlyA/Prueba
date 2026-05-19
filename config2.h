/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/config.h
 * ROL     : Declaraciones del TAD de configuración del módulo IO
 *
 * UBICACIÓN EN EL PROYECTO:
 *   io/
 *   └── src/
 *       └── io_utils/      ← carpeta PRIVADA del módulo IO
 *           └── config.h   ← este archivo
 *
 * NOTA IMPORTANTE — DIFERENCIA ENTRE CARPETAS utils:
 *   "io_utils/" → código privado del módulo IO. Solo lo usa IO.
 *   "utils/"    → módulo UTILS compartido. Lo usan TODOS los módulos
 *                 (IO, CPU, Kernel, etc.). Es como una biblioteca común.
 * ============================================================
 */

/*
 * GUARDA DE INCLUSIÓN (#ifndef / #define / #endif)
 * ─────────────────────────────────────────────────
 * PROBLEMA que resuelve: cuando dos archivos .c incluyen el mismo .h,
 * el preprocesador pegaría su contenido DOS VECES en el mismo archivo
 * objeto, lo que provoca errores de "redefinition of X".
 *
 * SOLUCIÓN: la guarda funciona así:
 *   - La primera vez que el preprocesador ve este .h, IO_CONFIG_H_ no
 *     está definido, entra al bloque y lo define.
 *   - La segunda vez que lo encuentre (por otro #include), IO_CONFIG_H_
 *     YA está definido, el #ifndef falla y omite todo el bloque.
 *
 * CONVENCIÓN UTN: nombre del archivo en MAYÚSCULAS reemplazando "." y "/"
 * por "_". Así "io_utils/config.h" → IO_CONFIG_H_
 */
#ifndef IO_CONFIG_H_
#define IO_CONFIG_H_

/*
 * INCLUDES DEL HEADER
 * ────────────────────
 * Regla de oro: en un .h solo incluir lo MÍNIMO necesario para que
 * los tipos que declaramos acá sean reconocibles por quien nos incluya.
 * El resto de includes va en el .c, no acá.
 *
 * ¿Por qué esta regla? Porque cada vez que alguien escribe:
 *   #include "io_utils/config.h"
 * el compilador carga TODOS los includes que config.h tenga adentro.
 * Si metemos headers innecesarios, aumentamos el tiempo de compilación
 * y creamos dependencias ocultas (un .c parece no depender de stdio.h,
 * pero lo termina incluyendo indirectamente por nuestro .h).
 */
#include <stdint.h>        /* uint32_t — entero de 32 bits de tamaño fijo */
#include <stdbool.h>       /* bool, true, false — legible y portable en C99 */
#include <commons/log.h>   /* t_log — lo usan las firmas de las funciones */
#include <commons/config.h>/* t_config — lo usa internamente config_io_leer() */

/* ══════════════════════════════════════════════════════════════
 * TAD: t_tipo_io
 *
 * ¿QUÉ ES UN ENUM?
 * Un enum es simplemente una lista de constantes enteras con nombres
 * legibles. El compilador los convierte a números (0, 1, 2, ...) pero
 * en el código se usan los nombres, que son más claros.
 *
 * ¿POR QUÉ USARLO EN VEZ DE STRINGS?
 * El módulo IO puede ser de tres tipos: SLEEP, STDIN, STDOUT.
 * Podríamos guardar el tipo como un string ("SLEEP", "STDIN", etc.)
 * pero eso tiene problemas:
 *
 *   PROBLEMA 1 - Velocidad: comparar strings con strcmp() recorre cada
 *   carácter uno por uno → O(n). Comparar enteros es O(1).
 *
 *   PROBLEMA 2 - Seguridad: si escribimos "sleeP" por error en el código,
 *   el compilador no lo detecta. Si escribimos TIPO_IO_SLEEEP, el
 *   compilador dice "ese símbolo no existe" y falla al compilar.
 *   Los errores detectados en compilación son mucho mejor que los
 *   que aparecen en tiempo de ejecución.
 *
 *   PROBLEMA 3 - Switch incompleto: si tenemos un switch(tipo) con
 *   enum, el compilador puede avisarnos "falta el case TIPO_IO_STDOUT".
 *   Con strings no puede hacer ese análisis.
 *
 * ESTRATEGIA: convertir el string UNA SOLA VEZ al arrancar (en
 * config_io_leer) y después usar el enum en todo el código.
 * ══════════════════════════════════════════════════════════════ */
typedef enum {
    TIPO_IO_SLEEP,    /* El módulo hace usleep() cuando recibe OP_IO_SLEEP  */
    TIPO_IO_STDIN,    /* El módulo lee del teclado cuando recibe OP_IO_STDIN */
    TIPO_IO_STDOUT,   /* El módulo imprime en pantalla cuando recibe OP_IO_STDOUT */
    TIPO_IO_INVALIDO  /* Valor centinela: se retorna cuando el string no es ninguno
                         de los anteriores. Permite detectar el error sin abortar
                         dentro de _parsear_tipo() — la decisión de abortar queda
                         en quien llama, que tiene más contexto para loguear bien. */
} t_tipo_io;

/* ══════════════════════════════════════════════════════════════
 * TAD: t_config_io
 *
 * ¿QUÉ ES UN STRUCT EN C?
 * Un struct agrupa varios campos bajo un único tipo. Es la forma
 * que tiene C de crear "objetos" simples (sin métodos, claro).
 *
 * ¿POR QUÉ AGRUPAR EN UN STRUCT EN VEZ DE VARIABLES SUELTAS?
 * Imaginate tener que pasar la configuración a 10 funciones diferentes.
 * Sin struct, cada función necesitaría 4 parámetros:
 *   void alguna_funcion(char *log_level, char *ip, char *puerto, t_tipo_io tipo)
 * Con el struct, todas reciben un único parámetro:
 *   void alguna_funcion(t_config_io cfg)
 * Y si mañana agregamos un campo nuevo al struct, SOLO cambia quien
 * inicializa el struct — no todas las firmas de función.
 *
 * ¿QUÉ ES typedef?
 * Sin typedef, cada vez que usamos el struct tendríamos que escribir:
 *   struct t_config_io cfg;
 * Con typedef, podemos escribir simplemente:
 *   t_config_io cfg;
 * Es solo azúcar sintáctica para no repetir "struct" todo el tiempo.
 * La convención de las commons-library de la cátedra usa t_ como prefijo.
 *
 * ¿POR QUÉ char* Y NO char[]?
 * Los campos de texto son punteros (char*), no arreglos (char[]).
 * Esto significa que apuntan a memoria dinámica reservada con malloc()
 * (via strdup() en la implementación). La consecuencia importante es
 * que cuando terminemos de usar el struct, debemos hacer free() de cada
 * campo. Por eso existe config_io_destruir().
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    char     *log_level;           /* "INFO" o "DEBUG" — leído del archivo .config */
    char     *ip_kernel_scheduler; /* IP donde está escuchando el Kernel Scheduler */
    char     *puerto_scheduler;    /* Puerto donde está escuchando el Kernel Scheduler */
    t_tipo_io tipo;                /* SLEEP / STDIN / STDOUT — parseado del argumento argv[2] */
} t_config_io;

/* ══════════════════════════════════════════════════════════════
 * DECLARACIÓN DE FUNCIONES PÚBLICAS
 *
 * CONCEPTO IMPORTANTE: en C, separamos DECLARACIÓN de IMPLEMENTACIÓN.
 *
 *   DECLARACIÓN (va en el .h): le dice al compilador que la función
 *   existe, cuántos parámetros recibe y qué retorna. Es como una firma
 *   de contrato: "prometo que esta función va a existir".
 *
 *   IMPLEMENTACIÓN (va en el .c): el código real de la función.
 *
 * ¿Por qué esta separación?
 * Cuando main.c incluye config.h, puede llamar a config_io_leer()
 * aunque config.c aún no haya sido compilado. El compilador sabe
 * la firma (gracias al .h) y el linker une todo al final.
 * ══════════════════════════════════════════════════════════════ */

/*
 * config_io_leer
 * ──────────────
 * Lee el archivo .config usando la biblioteca de la cátedra y
 * parsea el tipo de IO desde el argumento pasado por línea de comandos.
 *
 * Parámetros:
 *   path_config → ruta al archivo io.config (viene de argv[1])
 *   tipo_str    → string del tipo de IO como "SLEEP" (viene de argv[2])
 *   logger      → logger para mensajes de error; puede ser NULL si
 *                 aún no fue creado (la función usa fprintf(stderr) como fallback)
 *
 * Retorna:
 *   Un struct t_config_io completamente inicializado y listo para usar.
 *
 * Errores:
 *   Si el archivo no existe → llama a abort() (error irrecuperable)
 *   Si el tipo es inválido  → llama a abort() (error de uso del operador)
 *
 * IMPORTANTE sobre la memoria:
 *   Los campos char* del struct retornado son memoria dinámica (heap).
 *   El llamador es responsable de liberar esa memoria llamando a
 *   config_io_destruir() cuando ya no la necesite.
 */
t_config_io config_io_leer(const char *path_config,
                           const char *tipo_str,
                           t_log      *logger);

/*
 * config_io_destruir
 * ──────────────────
 * Libera toda la memoria dinámica que el struct posee internamente.
 *
 * Parámetro:
 *   cfg → puntero al struct a destruir. NO se hace free() del struct
 *         en sí porque generalmente vive en el stack de main().
 *         Solo se liberan los campos heap (log_level, ip, puerto).
 *
 * CUÁNDO LLAMARLA:
 *   Siempre antes de que el proceso termine. También antes de salir
 *   ante cualquier error, para que Valgrind no reporte leaks falsos.
 */
void config_io_destruir(t_config_io *cfg);

#endif /* IO_CONFIG_H_ */
