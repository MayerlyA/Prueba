/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/config.c
 * ROL     : Implementación del TAD t_config_io
 *
 * UBICACIÓN EN EL PROYECTO:
 *   io/
 *   └── src/
 *       └── io_utils/      ← carpeta PRIVADA del módulo IO
 *           └── config.c   ← este archivo
 *
 * REGLA: este .c incluye PRIMERO su propio .h. Eso hace que el
 * compilador verifique que la implementación coincide con la
 * declaración. Si cambiamos una firma en config.h y nos olvidamos
 * de actualizarla acá, el compilador lo detecta en el acto.
 * ============================================================
 */
#include "config.h"

/* Includes del sistema — solo los que ESTE .c necesita */
#include <stdio.h>   /* fprintf, stderr */
#include <stdlib.h>  /* abort(), free() */
#include <string.h>  /* strcmp(), strdup() */

/* ══════════════════════════════════════════════════════════════
 * FUNCIÓN PRIVADA: _parsear_tipo
 *
 * VISIBILIDAD: static
 * ────────────────────
 * La palabra "static" antes de una función en C tiene un significado
 * muy específico cuando se usa a nivel de archivo (no dentro de una
 * función): limita la visibilidad de esa función a ESTE archivo .c.
 * Ningún otro .c puede llamarla, aunque la conozca.
 *
 * ¿Por qué hacer una función static?
 * Porque es un detalle de implementación que no le interesa a nadie
 * de afuera. Es como los métodos "private" en Java/C++.
 * Si otro .c intentara llamar a _parsear_tipo(), el linker daría error.
 *
 * CONVENCIÓN DEL GRUPO: prefijo _ para funciones privadas/internas.
 * Así quien lee el código sabe al instante que es una función auxiliar
 * no exportada.
 *
 * ROL DE ESTA FUNCIÓN:
 * Convertir el string que el operador pasó por línea de comandos
 * ("SLEEP", "STDIN" o "STDOUT") al enum t_tipo_io correspondiente.
 * Esta conversión ocurre UNA SOLA VEZ al arrancar. El resto del
 * programa usa el enum, que es más eficiente y más seguro.
 *
 * PARÁMETRO:
 *   tipo_str → el string a convertir, ej: "SLEEP"
 *
 * RETORNA:
 *   TIPO_IO_SLEEP, TIPO_IO_STDIN o TIPO_IO_STDOUT si el string es válido.
 *   TIPO_IO_INVALIDO si no coincide con ninguno.
 *   Nota: la función NO llama a abort() ante un tipo inválido.
 *   Esa decisión la toma quien la llama (config_io_leer), que tiene
 *   más contexto para loguear y liberar memoria antes de abortar.
 *
 * ¿POR QUÉ strcmp() Y NO ==?
 * En C, los strings son arreglos de chars. El operador == compara
 * direcciones de memoria (punteros), no el contenido. Si hacemos
 * (tipo_str == "SLEEP"), comparamos si tipo_str y "SLEEP" apuntan
 * a la MISMA dirección de memoria, que casi nunca es verdad aunque
 * el contenido sea idéntico. strcmp() compara caracter por caracter
 * y retorna 0 cuando los strings son idénticos.
 * ══════════════════════════════════════════════════════════════ */
static t_tipo_io _parsear_tipo(const char *tipo_str) {
    if (strcmp(tipo_str, "SLEEP")  == 0) return TIPO_IO_SLEEP;
    if (strcmp(tipo_str, "STDIN")  == 0) return TIPO_IO_STDIN;
    if (strcmp(tipo_str, "STDOUT") == 0) return TIPO_IO_STDOUT;
    return TIPO_IO_INVALIDO;
}

/* ══════════════════════════════════════════════════════════════
 * FUNCIÓN PÚBLICA: config_io_leer
 *
 * ROL GENERAL:
 * Es el "constructor" del TAD t_config_io. Se encarga de:
 *   1. Abrir y parsear el archivo io.config
 *   2. Copiar los valores de string al struct con strdup()
 *   3. Cerrar el archivo (liberar la estructura de la biblioteca)
 *   4. Convertir el tipo de string a enum
 *   5. Retornar el struct completamente inicializado
 *
 * FLUJO DE DATOS:
 *   io.config (archivo) → config_create() → t_config de la biblioteca
 *   → config_get_string_value() → puntero INTERNO de la biblioteca
 *   → strdup() → copia en NUESTRA memoria → campo del struct
 *   → config_destroy() → la biblioteca libera SU memoria
 *
 * PARÁMETROS:
 *   path_config → ruta al archivo (argv[1] en main)
 *   tipo_str    → "SLEEP", "STDIN" o "STDOUT" (argv[2] en main)
 *   logger      → para logging; puede ser NULL en esta etapa porque
 *                 main() crea el logger DESPUÉS de leer la config
 * ══════════════════════════════════════════════════════════════ */
t_config_io config_io_leer(const char *path_config,
                           const char *tipo_str,
                           t_log      *logger) {

    /* ── PASO 1: Abrir el archivo de configuración ────────────
     *
     * config_create() es una función de la biblioteca de la cátedra
     * (commons-library). Abre el archivo, lee todas las líneas con
     * formato CLAVE=VALOR y las guarda en memoria internamente.
     *
     * Retorna NULL si el archivo no existe o no se puede leer.
     *
     * NOTA: el cast (char*) es necesario porque la firma de
     * config_create() espera char* (no const char*), aunque
     * internamente no modifica el string del path. Es una
     * imperfección de la biblioteca que debemos tolerar.
     */
    t_config *archivo_cfg = config_create((char *) path_config);

    if (archivo_cfg == NULL) {
        /*
         * DECISIÓN DE DISEÑO — abort() EN VEZ DE exit(-1):
         * La guía UTN explica que abort() termina el proceso con
         * una señal SIGABRT, que:
         *   - GDB intercepta automáticamente → podemos inspeccionar
         *     el estado del programa en el momento del fallo.
         *   - Genera un core dump → análisis post-mortem posible.
         *   - exit(-1) en cambio termina "limpiamente" sin dar esa
         *     oportunidad de debugging.
         *
         * Usamos abort() para errores IRRECUPERABLES en desarrollo.
         * Si el archivo de config no existe, no hay nada que hacer.
         */
        fprintf(stderr,
                "[IO][ERROR] No se pudo abrir el archivo de config: '%s'\n"
                "           Verificar que el path sea correcto.\n",
                path_config);
        abort();
    }

    /* ── PASO 2: Copiar los valores al struct con strdup() ────
     *
     * PROBLEMA CRÍTICO DE MEMORIA — "dangling pointer":
     * config_get_string_value() retorna un puntero a la memoria
     * INTERNA de la biblioteca. Esa memoria le PERTENECE a la
     * biblioteca, no a nosotros.
     *
     * Si hacemos:
     *   cfg.log_level = config_get_string_value(archivo_cfg, "LOG_LEVEL");
     *   config_destroy(archivo_cfg);  ← la biblioteca libera SU memoria
     *   // cfg.log_level ahora apunta a memoria liberada = PELIGRO
     *   printf("%s", cfg.log_level);  ← Undefined Behavior! puede crashear
     *                                    o imprimir basura aleatoria
     *
     * SOLUCIÓN — strdup():
     * strdup(s) equivale a: malloc(strlen(s)+1) + memcpy + return puntero.
     * Es decir: crea una NUEVA copia del string en NUESTRA memoria (heap).
     * Ahora cfg.log_level apunta a nuestra propia copia, que persiste
     * aunque la biblioteca libere la suya.
     *
     * COSTO: hay que hacer free() de cada campo en config_io_destruir().
     * Precio justo por tener memoria segura.
     */
    t_config_io cfg;
    cfg.log_level           = strdup(config_get_string_value(archivo_cfg, "LOG_LEVEL"));
    cfg.ip_kernel_scheduler = strdup(config_get_string_value(archivo_cfg, "IP_KERNEL_SCHEDULER"));
    cfg.puerto_scheduler    = strdup(config_get_string_value(archivo_cfg, "PUERTO_SCHEDULER"));

    /* ── PASO 3: Liberar la estructura de la biblioteca ───────
     *
     * Ya copiamos todo lo que necesitábamos con strdup().
     * La biblioteca puede liberar su memoria interna.
     * Si no llamáramos a config_destroy() acá, tendríamos un
     * memory leak: la memoria de la biblioteca quedaría ocupada
     * hasta que el proceso termine.
     *
     * ORDEN IMPORTANTE: primero copiamos con strdup(), DESPUÉS
     * liberamos con config_destroy(). Al revés sería un bug.
     */
    config_destroy(archivo_cfg);

    /* ── PASO 4: Convertir el tipo de string a enum ───────────
     *
     * Llamamos a la función privada _parsear_tipo() definida arriba.
     * Si retorna TIPO_IO_INVALIDO, el operador pasó un string inválido
     * como "SLEEEEP" o "stdout" (en minúsculas, que no coincide).
     */
    cfg.tipo = _parsear_tipo(tipo_str);

    if (cfg.tipo == TIPO_IO_INVALIDO) {
        /*
         * Antes de abort(), liberamos lo que ya alojamos.
         * Aunque abort() termina el proceso (y el SO libera todo),
         * liberar explícitamente hace que Valgrind reporte leaks
         * reales, no confundidos con los de este exit "sucio".
         * La cátedra usa Valgrind para validar el TP → esto importa.
         */
        free(cfg.log_level);
        free(cfg.ip_kernel_scheduler);
        free(cfg.puerto_scheduler);

        fprintf(stderr,
                "[IO][ERROR] Tipo de IO inválido: '%s'\n"
                "           Los valores válidos son: SLEEP, STDIN, STDOUT\n"
                "           Uso: ./bin/io <config> <tipo>\n",
                tipo_str);
        abort();
    }

    /* ── PASO 5: Retornar el struct completo ──────────────────
     *
     * PREGUNTA FRECUENTE: ¿Retornar un struct por valor no copia
     * todos sus bytes? ¿No es ineficiente?
     *
     * Respuesta: sí, se copian los bytes del struct. Pero el struct
     * t_config_io es pequeño: tres punteros (8 bytes c/u en 64 bits)
     * y un enum (4 bytes) = 28 bytes. Esa copia es insignificante.
     * Lo que NO se copia son los strings apuntados por esos punteros
     * (ellos siguen en el heap). La copia del struct solo copia
     * las DIRECCIONES, no el contenido de los strings.
     */
    return cfg;
}

/* ══════════════════════════════════════════════════════════════
 * FUNCIÓN PÚBLICA: config_io_destruir
 *
 * ROL GENERAL:
 * Es el "destructor" del TAD t_config_io. Libera toda la memoria
 * dinámica que los campos del struct apuntan.
 *
 * PATRÓN CONSTRUCTOR/DESTRUCTOR:
 * Cada TAD que aloja memoria dinámica debe tener su par:
 *   - Una función que crea/inicializa → config_io_leer()
 *   - Una función que destruye/libera → config_io_destruir()
 * Quien llama a la primera es responsable de llamar a la segunda.
 * Este patrón es el mismo que siguen las commons-library de la cátedra.
 *
 * PARÁMETRO:
 *   cfg → puntero al struct. Pasamos puntero (y no valor) porque
 *         necesitamos acceder a los campos para hacer free().
 *         Si pasáramos por valor, free() liberaría una copia local
 *         y la original quedaría sin liberar.
 * ══════════════════════════════════════════════════════════════ */
void config_io_destruir(t_config_io *cfg) {
    /*
     * free() libera la memoria heap que apuntan estos campos.
     * Recordar: esos char* fueron creados con strdup() (= malloc + memcpy)
     * en config_io_leer(), así que son de nuestra responsabilidad.
     *
     * NOTA: free(NULL) es válido en C estándar (ISO/IEC 9899) y no hace
     * nada. Así que aunque algún campo fuera NULL por alguna razón,
     * no crashearíamos. Es una pequeña red de seguridad.
     */
    free(cfg->log_level);
    free(cfg->ip_kernel_scheduler);
    free(cfg->puerto_scheduler);

    /*
     * IMPORTANTE — NO hacemos free(cfg):
     * cfg es un puntero al struct t_config_io que vive en el STACK
     * de main() (fue declarado como "t_config_io cfg" sin malloc).
     * Hacer free() de algo que no fue creado con malloc() es
     * Undefined Behavior → crash seguro o corrupción de memoria.
     * Solo liberamos los CAMPOS internos que sí son heap.
     */
}
