/*
 * ============================================================
 * ARCHIVO : io/src/main.c
 * ROL     : Punto de entrada del módulo IO
 *
 * EJECUCIÓN:
 *   ./bin/io io.config SLEEP
 *   ./bin/io io.config STDIN
 *   ./bin/io io.config STDOUT
 *
 * ARQUITECTURA:
 *   IO es un CLIENTE PURO del Kernel Scheduler.
 *   No escucha conexiones entrantes. Solo se conecta al KS,
 *   espera pedidos y los procesa en un loop bloqueante.
 *
 * FLUJO DE VIDA:
 *   1. Leer config + parsear tipo
 *   2. Crear logger
 *   3. Conectarse al KS (handshake con MOD_IO)
 *   4. Loop: recibir paquete → despachar → responder → repetir
 *   5. Salir si el KS se desconecta (paquete_recibir retorna NULL)
 *   6. Liberar recursos
 * ============================================================
 */

/*
 * BUENA PRÁCTICA — INCLUDES ORDENADOS POR CATEGORÍA:
 *   1. Headers del sistema (corchetes angulares)
 *   2. Headers de bibliotecas externas
 *   3. Headers propios del proyecto (comillas)
 *   Facilita detectar dependencias y evita conflictos de nombres.
 */

/* ── Sistema ──────────────────────────────────────────────── */
#include <stdio.h>     /* fprintf, stderr              */
#include <stdlib.h>    /* EXIT_SUCCESS, EXIT_FAILURE   */
#include <stdbool.h>   /* bool, true, false            */

/* ── Commons de la cátedra ───────────────────────────────── */
#include <commons/log.h>

/* ── UTILS compartidos del proyecto (módulo UTILS) ────────── */
#include "utils/sockets.h"
#include "utils/protocolo.h"
#include "utils/paquete.h"
#include "utils/op_codes.h"

/* ── Código propio del módulo IO (carpeta io_utils) ───────── */
#include "io_utils/config.h"
#include "io_utils/operaciones.h"

/*
 * ══════════════════════════════════════════════════════════════
 * FUNCIÓN PRINCIPAL
 *
 * CONCEPTO: argc y argv (Guía "Argumentos para el main" — UTN):
 *   Cuando el SO ejecuta el binario, llama a main().
 *   argc = cantidad de argumentos (siempre >= 1)
 *   argv = array de strings, terminado en NULL
 *
 *   Para: ./bin/io io.config SLEEP
 *     argv[0] = "./bin/io"    (nombre del programa)
 *     argv[1] = "io.config"   (path al archivo de config)
 *     argv[2] = "SLEEP"       (tipo de IO)
 *     argv[3] = NULL          (siempre termina en NULL)
 *     argc    = 3
 * ══════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {

    /* ── 1. Validar argumentos ────────────────────────────────
     * BUENA PRÁCTICA — VALIDAR ANTES DE USAR:
     *   Si argc < 3, argv[1] y argv[2] no existen.
     *   Acceder a argv[2] sin verificar argc es Undefined Behavior
     *   y puede causar Segmentation Fault o leer basura.
     *   Verificamos PRIMERO, usamos DESPUÉS.
     */
    if (argc < 3) {
        fprintf(stderr,
                "Uso: %s <archivo_config> <tipo>\n"
                "     tipo puede ser: SLEEP, STDIN, STDOUT\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *path_config = argv[1];
    const char *tipo_str    = argv[2];

    /* ── 2. Leer configuración ───────────────────────────────
     * Pasamos NULL como logger porque aún no lo creamos.
     * config_io_leer llama a abort() si hay error irrecuperable.
     */
    t_config_io cfg = config_io_leer(path_config, tipo_str, NULL);

    /* ── 3. Crear el logger ──────────────────────────────────
     * CONCEPTO: Logger de la cátedra (so-commons-library):
     *   log_create() crea un logger que escribe en:
     *     - Archivo "io.log"
     *     - Consola (true = sí mostrar en pantalla)
     *   log_level_from_string("INFO") convierte el string al enum.
     *
     * Logs INFO  → siempre se muestran con LOG_LEVEL=INFO
     * Logs DEBUG → solo se muestran con LOG_LEVEL=DEBUG
     *
     * IMPORTANTE: los logs OBLIGATORIOS del enunciado deben ser
     * log_info(). Los extras del grupo van en log_debug().
     */
    t_log *logger = log_create("io.log",
                               "IO",
                               true,
                               log_level_from_string(cfg.log_level));

    if (logger == NULL) {
        fprintf(stderr, "[IO][ERROR] No se pudo crear el logger\n");
        config_io_destruir(&cfg);
        return EXIT_FAILURE;
    }

    log_debug(logger, "## Módulo IO iniciando — tipo: %s", tipo_str);

    /* ── 4. Conectarse al Kernel Scheduler ───────────────────
     * CONCEPTO: Handshake (protocolo.c):
     *   conectar_a_modulo() hace dos cosas internamente:
     *     1. socket_conectar(ip, puerto) → abre el TCP
     *     2. Envía OP_HANDSHAKE + MOD_IO y espera OP_OK
     *   El KS lee MOD_IO y sabe que tiene que lanzar hilo_io().
     *
     * Si falla, retorna -1. Verificamos SIEMPRE el retorno.
     * Sin conexión, el módulo IO no puede hacer nada.
     */
    int fd_scheduler = conectar_a_modulo(
        cfg.ip_kernel_scheduler,
        cfg.puerto_scheduler,
        MOD_IO,
        logger,
        "Kernel Scheduler"
    );

    if (fd_scheduler == -1) {
        log_error(logger, "## No se pudo conectar al Kernel Scheduler. Abortando.");
        /*
         * BUENA PRÁCTICA — LIBERAR RECURSOS ANTES DE SALIR:
         *   Aunque EXIT_FAILURE termina el proceso (y el SO libera todo),
         *   liberar explícitamente permite que Valgrind informe solo
         *   los leaks reales y no los del exit "sucio".
         *   La cátedra recomienda usar Valgrind para validar el TP.
         */
        config_io_destruir(&cfg);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    /* Log obligatorio del enunciado */
    log_info(logger, "## Conectado a Kernel Scheduler");

    /* ── 5. Loop principal ───────────────────────────────────
     * CONCEPTO: Event Loop (loop bloqueante de atención):
     *   paquete_recibir() es una llamada BLOQUEANTE.
     *   El proceso queda suspendido (estado "sleeping" del SO)
     *   hasta que el KS mande algo. No consume CPU mientras espera.
     *   Cuando llega un paquete, el SO despierta el proceso.
     *
     *   Este patrón se llama "event loop" o "reactor pattern".
     *   Es el mismo modelo que usan servidores web, GUIs, etc.
     *
     * CONCEPTO: switch como dispatcher:
     *   Un único punto que enruta cada tipo de mensaje a su función.
     *   Evita encadenar if-else que crece sin control (código espagueti).
     *   El compilador puede optimizarlo como jump table (O(1)).
     *
     * BUENA PRÁCTICA — VARIABLE bool EN VEZ DE while(1):
     *   while(conectado) es más legible que while(1) con break dentro.
     *   Queda claro cuál es la condición de salida del loop.
     */
    bool conectado = true;

    while (conectado) {

        /*
         * Esperar el próximo paquete del KS.
         * paquete_recibir() retorna NULL cuando:
         *   - El KS cerró la conexión (EOF en el socket)
         *   - Hubo un error de red
         * En ambos casos debemos salir del loop limpiamente.
         */
        t_paquete *paq = paquete_recibir(fd_scheduler);

        if (paq == NULL) {
            log_warning(logger, "## Kernel Scheduler desconectado. Terminando.");
            conectado = false;
            break;
        }

        /*
         * Despachar según el código de operación.
         *
         * IMPORTANTE — POR QUÉ PASAMOS EL PAQUETE A LAS FUNCIONES:
         *   El paquete fue leído UNA SOLA VEZ acá.
         *   Sus datos están en paq->buffer esperando ser leídos.
         *   Las funciones de operación los extraen con buffer_leer().
         *   Si las funciones llamaran a paquete_recibir() internamente,
         *   esperarían un SEGUNDO paquete que nunca llega → deadlock.
         *
         * BUENA PRÁCTICA — VERIFICAR TIPO DE IO ANTES DE DESPACHAR:
         *   Un módulo SLEEP no debería recibir OP_IO_STDOUT.
         *   Si ocurre es un bug del KS. Lo logueamos y continuamos
         *   en vez de crashear — facilita el debugging sin interrumpir.
         */
        switch (paq->codigo) {

            case OP_IO_SLEEP:
                if (cfg.tipo != TIPO_IO_SLEEP) {
                    log_error(logger,
                              "## Recibí OP_IO_SLEEP pero soy tipo %s. "
                              "Revisar configuración del KS.", tipo_str);
                    break;
                }
                io_ejecutar_sleep(paq, fd_scheduler, logger);
                break;

            case OP_IO_STDOUT:
                if (cfg.tipo != TIPO_IO_STDOUT) {
                    log_error(logger,
                              "## Recibí OP_IO_STDOUT pero soy tipo %s. "
                              "Revisar configuración del KS.", tipo_str);
                    break;
                }
                io_ejecutar_stdout(paq, fd_scheduler, logger);
                break;

            case OP_IO_STDIN:
                if (cfg.tipo != TIPO_IO_STDIN) {
                    log_error(logger,
                              "## Recibí OP_IO_STDIN pero soy tipo %s. "
                              "Revisar configuración del KS.", tipo_str);
                    break;
                }
                io_ejecutar_stdin(paq, fd_scheduler, logger);
                break;

            default:
                log_warning(logger,
                            "## Código de operación desconocido: %d. "
                            "Ignorando.", paq->codigo);
                break;
        }

        /*
         * CICLO DE VIDA DEL PAQUETE:
         *   main() es el dueño del paquete: lo crea (recibe) y lo destruye.
         *   Las funciones de operación solo LEEN del buffer — no destruyen.
         *   Centralizamos el free acá para que sea claro y no se duplique.
         */
        paquete_destruir(paq);
    }

    /* ── 6. Limpieza de recursos ─────────────────────────────
     * BUENA PRÁCTICA — LIBERAR EN ORDEN INVERSO DE CREACIÓN:
     *   Primero lo más reciente, último lo más antiguo.
     *   Evita usar recursos ya liberados.
     *
     * BUENA PRÁCTICA — NECESARIA PARA VALGRIND (herramienta UTN):
     *   Valgrind detecta memory leaks. Si no liberamos, reporta
     *   "definitely lost" o "still reachable". El TP debe pasar
     *   Valgrind sin leaks para ser considerado correcto.
     */
    socket_cerrar(fd_scheduler);
    config_io_destruir(&cfg);
    log_destroy(logger);

    return EXIT_SUCCESS;
}
