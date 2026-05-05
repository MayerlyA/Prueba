/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/config.h
 * ROL     : Declaraciones del TAD de configuración del módulo IO
 * ============================================================
 *
 * BUENA PRÁCTICA — GUARDA DE INCLUSIÓN (#ifndef):
 *   Cuando dos archivos .c incluyen el mismo .h, el preprocesador
 *   pegaría el contenido dos veces → "redefinition of X".
 *   La guarda lo evita: la primera vez define IO_CONFIG_H_;
 *   la segunda vez, ya está definido y omite el bloque.
 *   Convención UTN: nombre del archivo en mayúsculas con _.
 */
#ifndef IO_CONFIG_H_
#define IO_CONFIG_H_

/*
 * BUENA PRÁCTICA — INCLUDES EN EL .h:
 *   Solo incluir lo estrictamente necesario para que los TIPOS
 *   del .h sean reconocibles. El resto va en el .c.
 *   Evitar incluir headers que no se usen: aumenta tiempos de compilación
 *   y genera acoplamiento innecesario.
 */
#include <stdint.h>    /* uint32_t — tamaño fijo en cualquier arquitectura */
#include <stdbool.h>   /* bool, true, false — legible y portable              */
#include <commons/log.h>    /* t_log — lo usan las funciones públicas          */
#include <commons/config.h> /* t_config — lo usa internamente config_io_leer() */

/* ══════════════════════════════════════════════════════════════
 * TAD: t_tipo_io
 *
 * BUENA PRÁCTICA — ENUM COMO DISCRIMINANTE (doc. UTN "Tipación Inteligente"):
 *   En vez de comparar strings con strcmp() cada vez que necesitamos
 *   saber el tipo, lo convertimos UNA SOLA VEZ al arrancar y después
 *   usamos switch/case con enteros.
 *   Beneficios:
 *     1. El compilador detecta valores no manejados en el switch.
 *     2. Comparaciones de enteros son O(1), strcmp es O(n).
 *     3. El código dice "TIPO_IO_SLEEP" y no "50" ni "sleep".
 * ══════════════════════════════════════════════════════════════ */
typedef enum {
    TIPO_IO_SLEEP,    /* ./bin/io io.config SLEEP  */
    TIPO_IO_STDIN,    /* ./bin/io io.config STDIN  */
    TIPO_IO_STDOUT,   /* ./bin/io io.config STDOUT */
    TIPO_IO_INVALIDO  /* string desconocido — se usa para detectar error al parsear */
} t_tipo_io;

/* ══════════════════════════════════════════════════════════════
 * TAD: t_config_io
 *
 * BUENA PRÁCTICA — ENCAPSULAMIENTO (doc. UTN "Encapsularidad"):
 *   Agrupamos toda la configuración en un struct. Así las funciones
 *   reciben UN parámetro en vez de cinco sueltos. Si mañana agregamos
 *   un campo nuevo, solo cambiamos el struct y quien lo inicializa,
 *   no todas las firmas de función.
 *
 * BUENA PRÁCTICA — TYPEDEF (doc. UTN "typedef es tu amigo"):
 *   typedef struct { ... } t_nombre; es la convención de las commons.
 *   El prefijo t_ indica que es un tipo de dato propio.
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    char      *log_level;           /* "INFO" o "DEBUG" — viene del .config     */
    char      *ip_kernel_scheduler; /* IP donde escucha el KS                   */
    char      *puerto_scheduler;    /* puerto donde escucha el KS               */
    t_tipo_io  tipo;                /* SLEEP / STDIN / STDOUT — parseado de argv */
} t_config_io;

/* ══════════════════════════════════════════════════════════════
 * INTERFAZ PÚBLICA DEL TAD t_config_io
 *
 * BUENA PRÁCTICA — SEPARAR INTERFAZ DE IMPLEMENTACIÓN:
 *   El .h declara QUÉ existe (firmas). El .c explica CÓMO funciona.
 *   Quien incluya este .h solo necesita saber cómo llamar a las funciones,
 *   no cómo están implementadas.
 * ══════════════════════════════════════════════════════════════ */

/*
 * config_io_leer — lee el archivo .config y parsea el tipo desde argv.
 *
 * Parámetros:
 *   path_config : ruta al archivo io.config (argv[1])
 *   tipo_str    : string del tipo de IO     (argv[2])
 *   logger      : para loguear si hay error (puede ser NULL)
 *
 * Retorna: struct t_config_io inicializado.
 * Efecto ante error irrecuperable: llama a abort() (ver implementación).
 */
t_config_io config_io_leer(const char *path_config,
                            const char *tipo_str,
                            t_log      *logger);

/*
 * config_io_destruir — libera toda la memoria dinámica del struct.
 *
 * IMPORTANTE: llamar SIEMPRE antes de que el proceso termine.
 * No recibe puntero a puntero porque el struct en sí vive en el
 * stack del llamador (no es heap); solo sus campos internos son heap.
 */
void config_io_destruir(t_config_io *cfg);

#endif /* IO_CONFIG_H_ */
