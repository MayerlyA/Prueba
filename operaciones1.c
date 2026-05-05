/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/operaciones.c
 * ROL     : Implementación de las tres operaciones del módulo IO
 * ============================================================
 */
#include "operaciones.h"

#include <stdio.h>    /* printf, fgets, fflush */
#include <stdlib.h>   /* malloc, free, abort   */
#include <string.h>   /* memset, memcpy, strlen */
#include <unistd.h>   /* usleep()              */

#include "utils/op_codes.h"

/*
 * ══════════════════════════════════════════════════════════════
 * FUNCIONES AUXILIARES PRIVADAS
 *
 * BUENA PRÁCTICA — EXTRAER LÓGICA REPETIDA:
 *   Construir y enviar la respuesta al KS es siempre igual.
 *   En vez de duplicar el código en las tres operaciones, lo
 *   extraemos. Una línea en cada función es más fácil de leer
 *   y si el protocolo cambia, solo cambia en un lugar.
 * ══════════════════════════════════════════════════════════════ */

/*
 * _enviar_fin_simple — envía un paquete de fin que solo contiene el PID.
 * Usado por SLEEP y STDOUT, cuyas respuestas no tienen datos extra.
 *
 * BUENA PRÁCTICA — NOMBRE CON PREFIJO _ PARA FUNCIONES PRIVADAS:
 *   Indica a quien lee el código que no forma parte de la interfaz
 *   pública del módulo.
 */
static void _enviar_fin_simple(t_op_code codigo_fin,
                                uint32_t  pid,
                                int       fd_scheduler,
                                t_log    *logger) {
    t_paquete *resp = paquete_crear(codigo_fin);

    /*
     * SERIALIZACIÓN — BUENA PRÁCTICA (Guía de Serialización UTN):
     *   Nunca enviamos structs directamente porque el compilador agrega
     *   "padding" entre los campos para alinear memoria.
     *   El sizeof() de un struct NO es la suma de sus campos.
     *   Con paquete/buffer escribimos campo por campo, asegurando
     *   que emisor y receptor interpreten exactamente los mismos bytes.
     */
    buffer_agregar(resp->buffer, &pid, sizeof(uint32_t));

    /*
     * BUENA PRÁCTICA — VERIFICAR RETORNO DE paquete_enviar():
     *   Si el KS se desconectó, paquete_enviar retorna -1.
     *   Logueamos la advertencia pero NO hacemos abort: el loop de main
     *   detectará la desconexión en el próximo paquete_recibir().
     */
    int bytes_enviados = paquete_enviar(resp, fd_scheduler);
    if (bytes_enviados <= 0) {
        log_warning(logger, "## [IO] No se pudo enviar respuesta %d — KS desconectado?", codigo_fin);
    }

    paquete_destruir(resp);
}

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_sleep
 *
 * CONCEPTO: usleep()
 *   Es una syscall que suspende el proceso N MICROsegundos.
 *   El SO pone el proceso en estado "sleeping" y le da la CPU
 *   a otros procesos — no consume ciclos mientras espera.
 *   Simula una operación de IO lenta sin desperdiciar CPU.
 *
 * CONVERSIÓN: el enunciado habla en MILIsegundos.
 *   1 ms = 1.000 μs → multiplicamos por 1000.
 *   uint32_t puede guardar hasta ~4.294.967 ms ≈ 71 minutos.
 *   Es suficiente para cualquier prueba del TP.
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_sleep(t_paquete *paq, int fd_scheduler, t_log *logger) {

    /* ── Extraer datos del buffer ─────────────────────────────
     * buffer_leer() copia N bytes del buffer en la variable y
     * avanza el cursor interno. Las lecturas deben estar en el
     * MISMO orden en que el KS los escribió con buffer_agregar().
     * Si el orden es incorrecto, los datos se interpretan mal
     * (bug muy difícil de detectar).
     */
    uint32_t pid       = 0;
    uint32_t tiempo_ms = 0;
    buffer_leer(paq->buffer, &pid,       sizeof(uint32_t));
    buffer_leer(paq->buffer, &tiempo_ms, sizeof(uint32_t));

    /* ── Logs obligatorios del enunciado ──────────────────────
     * IMPORTANTE: deben ser log_info() para que aparezcan
     * con LOG_LEVEL=INFO. Los logs adicionales van en log_debug().
     */
    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Haciendo sleep por %u milisegundos.", pid, tiempo_ms);

    /* ── Dormir exactamente el tiempo pedido ──────────────────
     * useconds_t es el tipo que pide usleep (unsigned int en Linux).
     * Casteamos para evitar warning del compilador con -Wall.
     * Con -Wall activo (recomendado por la cátedra), las conversiones
     * implícitas generan warnings que deben resolverse.
     */
    usleep((useconds_t)(tiempo_ms * 1000));

    log_info(logger, "## PID: %u - Fin de IO", pid);

    /* ── Avisar al KS que el sleep terminó ───────────────────
     * Sin esta respuesta el proceso quedaría en BLOCKED para
     * siempre (o hasta que venza SUSPENSION_TIMEOUT).
     */
    _enviar_fin_simple(OP_IO_SLEEP_FIN, pid, fd_scheduler, logger);
}

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_stdout
 *
 * CONCEPTO: IO como terminal del proceso simulado.
 *   El proceso simulado no tiene acceso al terminal real.
 *   El KS actuó de intermediario:
 *     1. CPU ejecuta STDOUT AX BX (syscall al KS)
 *     2. KS le pide los bytes al KM con OP_MS_LEER
 *     3. KM lee de los Memory Sticks y se los manda al KS
 *     4. KS nos manda los bytes (OP_IO_STDOUT) — ya listos
 *     5. Nosotros los imprimimos ← estamos acá
 *     6. Respondemos OP_IO_STDOUT_FIN → KS desbloquea el proceso
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_stdout(t_paquete *paq, int fd_scheduler, t_log *logger) {

    uint32_t pid     = 0;
    uint32_t tamanio = 0;
    buffer_leer(paq->buffer, &pid,     sizeof(uint32_t));
    buffer_leer(paq->buffer, &tamanio, sizeof(uint32_t));

    /*
     * BUENA PRÁCTICA — VERIFICAR RETORNO DE malloc():
     *   malloc() puede retornar NULL si el SO no tiene memoria libre.
     *   En producción real esto es raro, pero es una buena práctica
     *   siempre verificarlo. La cátedra lo exige en la guía de sockets.
     *
     * NOTA: malloc(0) tiene comportamiento definido en C99 pero
     * dependiente de implementación. Si tamanio == 0, igual reservamos
     * 1 byte para el '\0' del null-terminator.
     */
    size_t bytes_a_reservar = (tamanio > 0) ? tamanio : 1;
    char  *contenido        = malloc(bytes_a_reservar + 1); /* +1 para '\0' */

    if (contenido == NULL) {
        log_error(logger, "## PID: %u - STDOUT: malloc() falló — sin memoria", pid);
        abort(); /* Error irrecuperable: sin memoria no podemos continuar */
    }

    buffer_leer(paq->buffer, contenido, tamanio);
    contenido[tamanio] = '\0'; /* null-terminar para usar como string en log_info */

    /* ── Logs obligatorios ────────────────────────────────── */
    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - %s", pid, contenido); /* log obligatorio STDOUT */

    /*
     * fflush(stdout): el SO tiene un buffer de salida.
     * Sin fflush, "printf" puede quedar en el buffer y no verse
     * inmediatamente en la terminal. Crítico para debugging.
     */
    printf("[STDOUT PID:%u] %s\n", pid, contenido);
    fflush(stdout);

    /*
     * BUENA PRÁCTICA — free() ANTES DE LA RESPUESTA:
     *   Liberamos la memoria cuando ya no la necesitamos.
     *   No esperar al final de la función: si hubiera más código
     *   después, podría quedar sin liberar ante un return temprano.
     */
    free(contenido);

    log_info(logger, "## PID: %u - Fin de IO", pid);
    _enviar_fin_simple(OP_IO_STDOUT_FIN, pid, fd_scheduler, logger);
}

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_stdin
 *
 * CONCEPTO: Lectura de teclado con tamaño exacto.
 *   El proceso simulado espera exactamente `tamanio` bytes en
 *   su dirección de memoria (dir_logica). Tenemos que proveer
 *   exactamente esa cantidad, ni más ni menos:
 *     - Más de tamanio chars → truncar
 *     - Menos de tamanio chars → rellenar con '\0'
 *
 * Por qué fgets() y no scanf() o gets():
 *   gets() fue eliminado en C11 por ser inseguro (buffer overflow).
 *   scanf("%s") tampoco limita la longitud si no se especifica.
 *   fgets(buf, n, stdin) garantiza que lee como máximo n-1 chars.
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_stdin(t_paquete *paq, int fd_scheduler, t_log *logger) {

    uint32_t pid        = 0;
    uint32_t tamanio    = 0;
    uint32_t dir_logica = 0;
    buffer_leer(paq->buffer, &pid,        sizeof(uint32_t));
    buffer_leer(paq->buffer, &tamanio,    sizeof(uint32_t));
    buffer_leer(paq->buffer, &dir_logica, sizeof(uint32_t));

    log_info(logger, "## PID: %u - Inicio de IO", pid);
    log_info(logger, "## PID: %u - Ingrese %u caracteres:", pid, tamanio);

    /*
     * Reservar buffer para la lectura.
     * Necesitamos tamanio + 2 bytes:
     *   - tamanio bytes de contenido real
     *   - 1 byte para el '\n' que fgets incluye al presionar Enter
     *   - 1 byte para el '\0' terminador de string
     *
     * BUENA PRÁCTICA — CONSTANTES NOMBRADAS O COMENTADAS:
     *   El "+2" no es mágico: explicamos qué representa cada byte.
     */
    char *buffer_lectura = malloc(tamanio + 2);
    if (buffer_lectura == NULL) {
        log_error(logger, "## PID: %u - STDIN: malloc() falló — sin memoria", pid);
        abort();
    }

    /* Inicializar con '\0' por si fgets falla o lee menos bytes */
    memset(buffer_lectura, '\0', tamanio + 2);

    /* fgets retorna NULL si hay error de lectura o EOF */
    if (fgets(buffer_lectura, (int)(tamanio + 2), stdin) == NULL) {
        log_warning(logger,
                    "## PID: %u - STDIN: fgets falló (EOF o error de stdin). "
                    "Se usará buffer vacío.", pid);
        /* buffer_lectura ya está inicializado con '\0', continuamos */
    }

    /*
     * Eliminar el '\n' que fgets deja al final cuando el usuario
     * presiona Enter. Si no lo quitamos, el proceso simulado
     * recibiría un salto de línea como parte de los datos.
     */
    size_t longitud = strlen(buffer_lectura);
    if (longitud > 0 && buffer_lectura[longitud - 1] == '\n') {
        buffer_lectura[longitud - 1] = '\0';
        longitud--;
    }

    /*
     * Construir el buffer final de EXACTAMENTE tamanio bytes.
     *
     * ALGORITMO:
     *   1. memset con '\0' → todo el buffer es "vacío"
     *   2. memcpy de min(longitud, tamanio) bytes del input
     *
     * ESCENARIO A — usuario escribió MENOS que tamanio (ej: 4 de 10):
     *   memset pone '\0' en todo.
     *   memcpy copia 4 bytes.
     *   Resultado: [H][o][l][a][\0][\0][\0][\0][\0][\0]  (relleno)
     *
     * ESCENARIO B — usuario escribió MÁS que tamanio (ej: 15 de 10):
     *   fgets ya limitó a tamanio+1 chars (truncado en la lectura).
     *   memcpy copia tamanio bytes.
     *   Resultado: solo los primeros tamanio chars.
     *
     * BUENA PRÁCTICA — OPERADOR TERNARIO PARA MÍNIMO:
     *   (a < b) ? a : b es idiomático en C para min(a, b).
     *   Evita incluir <math.h> o escribir una función extra.
     */
    char *bytes_finales = malloc(tamanio);
    if (bytes_finales == NULL) {
        free(buffer_lectura);
        log_error(logger, "## PID: %u - STDIN: segundo malloc() falló", pid);
        abort();
    }

    memset(bytes_finales, '\0', tamanio);

    uint32_t bytes_a_copiar = (longitud < (size_t)tamanio)
                              ? (uint32_t)longitud
                              : tamanio;
    memcpy(bytes_finales, buffer_lectura, bytes_a_copiar);

    free(buffer_lectura); /* ya no la necesitamos */

    log_info(logger, "## PID: %u - Fin de IO", pid);

    /*
     * Responder al KS con todos los datos que necesita.
     *
     * SERIALIZACIÓN (Guía UTN):
     *   El KS espera recibir en este orden:
     *     1. pid       (4 bytes) — para saber qué proceso desbloquear
     *     2. tamanio   (4 bytes) — cuántos bytes viene la data
     *     3. dir_logica (4 bytes) — a qué dirección del proceso escribir
     *     4. bytes_finales (tamanio bytes) — el contenido leído
     *
     *   El KS (en hilo_io, case OP_IO_STDIN_FIN) los leerá en este
     *   mismo orden con buffer_leer(). Si el orden no coincide,
     *   interpretará los bytes de forma incorrecta.
     *
     *   Nunca enviamos la struct completa con memcpy(&struct) porque
     *   el compilador puede agregar padding entre los campos.
     */
    t_paquete *resp = paquete_crear(OP_IO_STDIN_FIN);
    buffer_agregar(resp->buffer, &pid,          sizeof(uint32_t));
    buffer_agregar(resp->buffer, &tamanio,      sizeof(uint32_t));
    buffer_agregar(resp->buffer, &dir_logica,   sizeof(uint32_t));
    buffer_agregar(resp->buffer, bytes_finales, tamanio);

    int bytes_enviados = paquete_enviar(resp, fd_scheduler);
    if (bytes_enviados <= 0) {
        log_warning(logger, "## PID: %u - STDIN: no se pudo enviar respuesta al KS", pid);
    }

    paquete_destruir(resp);
    free(bytes_finales);
}
