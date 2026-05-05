/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/config.c
 * ROL     : Implementación del TAD t_config_io
 * ============================================================
 *
 * BUENA PRÁCTICA — EL .c INCLUYE SU PROPIO .h:
 *   Así el compilador verifica que la implementación coincide
 *   con la declaración. Si cambiamos una firma en el .h y no
 *   en el .c, el compilador lo detecta.
 */
#include "config.h"

#include <stdio.h>   /* fprintf, stderr */
#include <stdlib.h>  /* abort()         */
#include <string.h>  /* strcmp, strdup  */

/* ══════════════════════════════════════════════════════════════
 * FUNCIÓN PRIVADA: _parsear_tipo
 *
 * BUENA PRÁCTICA — FUNCIONES QUE HACEN UNA SOLA TAREA:
 *   Esta función solo convierte un string a un enum.
 *   No loguea, no llama a abort, no abre archivos.
 *   Una función, una responsabilidad.
 *
 * BUENA PRÁCTICA — PREFIJO static:
 *   "static" limita la visibilidad a este archivo .c.
 *   Evita que otro .c acceda a ella accidentalmente.
 *   Convención del grupo: funciones privadas con prefijo _.
 * ══════════════════════════════════════════════════════════════ */
static t_tipo_io _parsear_tipo(const char *tipo_str) {
    /* strcmp retorna 0 cuando los strings son iguales */
    if (strcmp(tipo_str, "SLEEP")  == 0) return TIPO_IO_SLEEP;
    if (strcmp(tipo_str, "STDIN")  == 0) return TIPO_IO_STDIN;
    if (strcmp(tipo_str, "STDOUT") == 0) return TIPO_IO_STDOUT;
    return TIPO_IO_INVALIDO;
}

/* ══════════════════════════════════════════════════════════════
 * config_io_leer
 * ══════════════════════════════════════════════════════════════ */
t_config_io config_io_leer(const char *path_config,
                            const char *tipo_str,
                            t_log      *logger) {

    /*
     * BUENA PRÁCTICA — VERIFICAR RETORNO DE FUNCIONES DE BIBLIOTECA:
     *   La doc de la cátedra dice explícitamente: "recordá chequear
     *   los valores de retorno de las mismas para poder manejar los
     *   casos de error".
     *   config_create() retorna NULL si el archivo no existe.
     */
    t_config *archivo_cfg = config_create((char *) path_config);
    if (archivo_cfg == NULL) {
        /*
         * BUENA PRÁCTICA — abort() EN VEZ DE exit(-1):
         *   La guía UTN dice: "en vez de poner exit(-1), poner abort().
         *   Abort termina el programa con una señal que GDB frena,
         *   como Segmentation Fault, por lo que uno tiene posibilidad
         *   de mirar el estado de todo el sistema antes de abortar."
         *   Úsalo para errores irrecuperables en desarrollo.
         *   En producción, si se prefiere exit(EXIT_FAILURE) está bien,
         *   pero abort() facilita el debugging.
         */
        fprintf(stderr,
                "[IO][ERROR] No se pudo abrir el archivo de config: '%s'\n"
                "            Verificar que el path sea correcto.\n",
                path_config);
        abort();
    }

    t_config_io cfg;

    /*
     * BUENA PRÁCTICA — strdup() PARA CAMPOS STRING:
     *   config_get_string_value() retorna un puntero a la memoria
     *   INTERNA de la biblioteca. Cuando llamamos config_destroy(),
     *   esa memoria se libera.
     *
     *   PROBLEMA: si guardamos el puntero directo, después de
     *   config_destroy() tenemos un "dangling pointer" (puntero
     *   que apunta a memoria ya liberada). Acceder a él es
     *   Undefined Behavior — puede crashear o dar datos corruptos.
     *
     *   SOLUCIÓN: strdup() hace malloc + memcpy. Ahora el campo
     *   apunta a NUESTRA memoria, que nosotros controlamos.
     *   Costo: hay que hacer free() en config_io_destruir().
     *
     *   REFERENCIA: Guía de Serialización UTN — "problemas de padding
     *   y ownership de memoria".
     */
    cfg.log_level           = strdup(config_get_string_value(archivo_cfg, "LOG_LEVEL"));
    cfg.ip_kernel_scheduler = strdup(config_get_string_value(archivo_cfg, "IP_KERNEL_SCHEDULER"));
    cfg.puerto_scheduler    = strdup(config_get_string_value(archivo_cfg, "PUERTO_SCHEDULER"));

    /*
     * Liberar la config de la biblioteca ANTES de parsear el tipo.
     * Ya copiamos todo lo que necesitábamos con strdup().
     * No liberar sería un memory leak.
     */
    config_destroy(archivo_cfg);

    /* Convertir el string "SLEEP"/"STDIN"/"STDOUT" al enum */
    cfg.tipo = _parsear_tipo(tipo_str);

    if (cfg.tipo == TIPO_IO_INVALIDO) {
        /*
         * Error de uso: el operador pasó un tipo desconocido.
         * Liberamos lo que ya alojamos para no generar leak antes de abort().
         *
         * BUENA PRÁCTICA — LIBERAR ANTES DE ABORTAR:
         *   Aunque abort() termine el proceso (y el SO libere todo),
         *   liberar explícitamente permite que herramientas como
         *   Valgrind reporten leaks reales, no confundidos con los
         *   de un abort "sucio".
         */
        free(cfg.log_level);
        free(cfg.ip_kernel_scheduler);
        free(cfg.puerto_scheduler);

        fprintf(stderr,
                "[IO][ERROR] Tipo de IO inválido: '%s'\n"
                "            Los valores válidos son: SLEEP, STDIN, STDOUT\n"
                "            Uso: ./bin/io <config> <tipo>\n",
                tipo_str);
        abort();
    }

    return cfg;
}

/* ══════════════════════════════════════════════════════════════
 * config_io_destruir
 *
 * BUENA PRÁCTICA — FUNCIÓN DESTRUCTORA POR TAD:
 *   Cada TAD tiene su propia función de destrucción.
 *   Así el código que crea el struct no necesita saber qué
 *   campos son heap y cuáles son stack — solo llama a destruir.
 *   Patrón constructor/destructor que imitan las commons.
 * ══════════════════════════════════════════════════════════════ */
void config_io_destruir(t_config_io *cfg) {
    /*
     * BUENA PRÁCTICA — free() DEFENSIVO:
     *   Aunque aquí sabemos que los campos no son NULL (config_io_leer
     *   hace abort si algo falla), free(NULL) es válido en C estándar
     *   y no hace nada. En general, es buena idea verificarlo si el
     *   ciclo de vida del struct es más complejo.
     */
    free(cfg->log_level);
    free(cfg->ip_kernel_scheduler);
    free(cfg->puerto_scheduler);

    /*
     * BUENA PRÁCTICA — NO hacer free() del struct en sí:
     *   cfg es un puntero al struct que vive en el stack de main().
     *   Solo liberamos los campos heap que el struct "posee".
     *   Hacer free(cfg) sería un error si cfg no fue malloc'd.
     */
}
