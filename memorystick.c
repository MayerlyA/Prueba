// ============================================================
// ARCHIVO: memory_stick/src/main.c
// ------------------------------------------------------------
// El Memory Stick simula un módulo de memoria RAM física.
// Internamente es un bloque de bytes reservados con malloc().
//
// Responsabilidades:
//   1. Al arrancar: reservar tamanio bytes y registrarse en el KM
//   2. En ejecución: atender lecturas y escrituras de múltiples
//      CPUs (y del KM durante compactación) de forma concurrente
//
// IMPORTANTE: Pueden llegar pedidos de múltiples CPUs al mismo
// tiempo. Por eso usamos un hilo por conexión y un mutex para
// proteger el acceso al bloque de memoria.
// ============================================================

#include "utils/sockets.h"
#include "utils/protocolo.h"
#include "utils/paquete.h"
#include "utils/hilo.h"
#include "memory_stick_utils/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>

// ─────────────────────────────────────────────────────────────────────────
// VARIABLES GLOBALES DEL MÓDULO
//
// Estas son accedidas por múltiples hilos (uno por CPU conectada).
// Cualquier acceso al bloque de memoria o al logger
// debe hacerse con el mutex tomado.
// ─────────────────────────────────────────────────────────────────────────

// El bloque de bytes que simula la memoria RAM física
static void    *g_memoria       = NULL;
static uint32_t g_tamanio       = 0;

// Protege el acceso concurrente al bloque g_memoria
// Si dos CPUs leen/escriben al mismo tiempo sin esto, los datos se corrompen
static pthread_mutex_t g_mutex_memoria = PTHREAD_MUTEX_INITIALIZER;

// Configuración y logger (solo lectura después de la inicialización,
// por eso no necesitan mutex)
static t_config_ms *g_cfg    = NULL;
static t_log       *g_logger = NULL;

// ─────────────────────────────────────────────────────────────────────────
// ESTRUCTURA: t_args_cliente
//
// Datos que el hilo principal le pasa a cada hilo hijo al crear uno.
// Cada hilo atiende a UNA conexión (una CPU o el KM).
// ─────────────────────────────────────────────────────────────────────────
typedef struct {
    int fd;      // socket de la conexión nueva
} t_args_cliente;

// ── Prototipos internos ────────────────────────────────────────────────
static void *hilo_cliente(void *arg);
static void  atender_cliente(int fd);

// ─────────────────────────────────────────────────────────────────────────
// FUNCIÓN: main
//
// Arranque del Memory Stick:
//   1. Leer parámetros: argv[1]=config, argv[2]=tamaño en bytes
//   2. Reservar la memoria simulada con malloc()
//   3. Conectarse al KM e informar el tamaño
//   4. Levantar servidor y aceptar conexiones en loop
// ─────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <config> <tamanio>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // El tamaño de este Memory Stick se pasa como argumento de línea de comandos
    // Así podemos levantar múltiples instancias con distintos tamaños
    g_tamanio = (uint32_t) atoi(argv[2]);

    t_config_ms cfg = leer_config(argv[1]);
    g_cfg = &cfg;

    g_logger = log_create("memory_stick.log", "MEMORY_STICK", true,
                          log_level_from_string(cfg.log_level));

    // ── Paso 1: reservar el bloque de memoria simulada ───────────────────
    // calloc() reserva el bloque Y lo inicializa a ceros.
    // (malloc() solo reserva; los bytes quedan con basura)
    g_memoria = calloc(1, g_tamanio);
    if (g_memoria == NULL) {
        log_error(g_logger, "## No se pudo reservar %u bytes de memoria", g_tamanio);
        return EXIT_FAILURE;
    }
    log_info(g_logger, "## Memoria reservada: %u bytes", g_tamanio);

    // ── Paso 2: conectarse al Kernel Memory e informar tamaño ────────────
    // El KM usará este tamaño para saber cuánto espacio tiene este MS
    // y qué offsets globales le asigna.
    int fd_km = conectar_a_modulo(cfg.ip_kernel_memory,
                                  cfg.puerto_kernel_memory,
                                  MOD_MEMORY_STICK, g_logger, "Kernel Memory");
    if (fd_km == -1) {
        free(g_memoria);
        log_destroy(g_logger);
        return EXIT_FAILURE;
    }

    // Enviar el tamaño al KM (protocolo OP_MS_REGISTRAR)
    t_paquete *paq = paquete_crear(OP_MS_REGISTRAR);
    buffer_agregar(paq->buffer, &g_tamanio, sizeof(uint32_t));
    paquete_enviar(paq, fd_km);
    paquete_destruir(paq);

    log_info(g_logger, "## Conectado a Kernel Memory — informado tamaño: %u bytes", g_tamanio);

    // El KM ya no nos mandará nada más por este socket (es solo de registro).
    // Las CPUs se conectarán por un socket separado en nuestro servidor.
    socket_cerrar(fd_km);

    // ── Paso 3: levantar servidor ─────────────────────────────────────────
    int fd_escucha = socket_escuchar(cfg.puerto_memory_stick);
    if (fd_escucha == -1) {
        log_error(g_logger, "## Error al iniciar servidor en puerto %s", cfg.puerto_memory_stick);
        free(g_memoria);
        log_destroy(g_logger);
        return EXIT_FAILURE;
    }
    log_info(g_logger, "## Escuchando conexiones en puerto %s", cfg.puerto_memory_stick);

    // ── Paso 4: loop de aceptación ─────────────────────────────────────────
    // Por cada cliente nuevo (CPU o KM) lanzamos un hilo independiente.
    // El hilo principal vuelve inmediatamente a esperar la próxima conexión.
    // Así podemos atender múltiples CPUs al mismo tiempo.
    while (1) {
        // protocolo_aceptar_conexion hace el accept() + handshake
        t_modulo modulo;
        int fd_nuevo = protocolo_aceptar_conexion(fd_escucha, &modulo, g_logger);
        if (fd_nuevo == -1) continue; // error en accept(), intentar de nuevo

        // Creamos los args para el hilo (malloc porque el hilo los liberará)
        t_args_cliente *args = malloc(sizeof(t_args_cliente));
        args->fd = fd_nuevo;

        // Lanzar hilo independiente para este cliente
        // lanzar_hilo() ya hace pthread_create + pthread_detach
        lanzar_hilo(hilo_cliente, args);

        if (modulo == MOD_CPU) {
            log_info(g_logger, "## Nueva CPU conectada (fd=%d)", fd_nuevo);
        } else if (modulo == MOD_KERNEL_MEMORY) {
            log_info(g_logger, "## Kernel Memory conectado para operación (fd=%d)", fd_nuevo);
        } else {
            log_warning(g_logger, "## Cliente desconocido: módulo=%d (fd=%d)", modulo, fd_nuevo);
        }
    }

    // Limpieza (no se llega aquí en condiciones normales)
    free(g_memoria);
    socket_cerrar(fd_escucha);
    config_destruir(&cfg);
    log_destroy(g_logger);
    return EXIT_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────
// FUNCIÓN: hilo_cliente
//
// Función que ejecuta cada hilo hijo.
// Solo extrae el fd de los args, libera la memoria y llama a atender_cliente.
// Separamos esto de atender_cliente para que el patrón de crear/liberar args
// sea siempre el mismo en todos los hilos del proyecto.
// ─────────────────────────────────────────────────────────────────────────
static void *hilo_cliente(void *arg) {
    t_args_cliente *args = (t_args_cliente *) arg;
    int fd = args->fd;
    free(args); // liberar ANTES de atender (no necesitamos más el struct)

    atender_cliente(fd);

    socket_cerrar(fd);
    return NULL;
}

// ─────────────────────────────────────────────────────────────────────────
// FUNCIÓN: atender_cliente
//
// Loop de atención de UN cliente (una CPU o el KM).
// Recibe paquetes y responde hasta que el cliente cierra la conexión.
//
// Operaciones soportadas:
//   OP_MS_LEER    → leer bytes a partir de una dirección FÍSICA
//   OP_MS_ESCRIBIR → escribir bytes a partir de una dirección FÍSICA
//
// NOTA sobre "dirección física":
//   La CPU ya hizo la traducción lógica→física antes de llegar acá.
//   Nosotros solo vemos offsets dentro de nuestro bloque de memoria.
//   Si la CPU pide dirección 1500 y nuestro offset global es 1024,
//   el índice dentro de g_memoria es 1500 - 1024 = 476.
//   → Esta resta la hace la CPU, no nosotros. Nosotros recibimos
//     directamente el offset local (ya traducido por la CPU).
//
// ACTUALIZACIÓN CP3: el KM nos informa nuestro offset global al
// conectarse (OP_MS_INFO). La CPU usa ese offset para calcular qué
// parte de la dirección física cae en este MS.
// ─────────────────────────────────────────────────────────────────────────
static void atender_cliente(int fd) {
    t_paquete *paq;

    while ((paq = paquete_recibir(fd)) != NULL) {

        switch (paq->codigo) {

            // ── OP_MS_LEER ────────────────────────────────────────────────
            // Protocolo entrada: uint32_t direccion_fisica + uint32_t tamanio
            // Protocolo salida:  OP_MS_RESPUESTA + bytes leídos
            case OP_MS_LEER: {
                uint32_t direccion, tamanio;
                buffer_leer(paq->buffer, &direccion, sizeof(uint32_t));
                buffer_leer(paq->buffer, &tamanio,   sizeof(uint32_t));
                paquete_destruir(paq);

                log_info(g_logger, "## Lectura — Dir. Física: %u — Tamaño: %u", direccion, tamanio);

                // Reservar buffer para los datos que vamos a leer
                void *datos = malloc(tamanio);

                // ── Sección crítica ──────────────────────────────────────
                // Tomamos el mutex porque otro hilo podría estar escribiendo
                // en la misma dirección al mismo tiempo.
                pthread_mutex_lock(&g_mutex_memoria);

                // Validar que la dirección y el tamaño caen dentro del bloque
                if (direccion + tamanio > g_tamanio) {
                    log_error(g_logger, "## Lectura fuera de rango: dir=%u tam=%u (tamaño MS=%u)",
                              direccion, tamanio, g_tamanio);
                    pthread_mutex_unlock(&g_mutex_memoria);
                    free(datos);
                    // Responder con ceros para no dejar al cliente colgado
                    datos = calloc(1, tamanio);
                } else {
                    // Copiar los bytes desde la memoria simulada al buffer de respuesta
                    // (char*) para hacer aritmética de punteros byte a byte
                    memcpy(datos, (char *) g_memoria + direccion, tamanio);
                    pthread_mutex_unlock(&g_mutex_memoria);

                    // Simular la latencia de la memoria (MEMORY_DELAY ms)
                    // Esto lo hacemos FUERA del mutex para no bloquear a otros clientes mientras dormimos
                    if (g_cfg->memory_delay != NULL) {
                        usleep((*g_cfg->memory_delay) * 1000); // convertir ms a µs
                    }
                }

                // Armar respuesta con los datos leídos
                t_paquete *resp = paquete_crear(OP_MS_RESPUESTA);
                buffer_agregar(resp->buffer, datos, tamanio);
                paquete_enviar(resp, fd);
                paquete_destruir(resp);
                free(datos);
                break;
            }

            // ── OP_MS_ESCRIBIR ────────────────────────────────────────────
            // Protocolo entrada: uint32_t direccion_fisica + uint32_t tamanio + bytes
            // Protocolo salida:  OP_MS_RESPUESTA (solo confirmación, sin datos)
            case OP_MS_ESCRIBIR: {
                uint32_t direccion, tamanio;
                buffer_leer(paq->buffer, &direccion, sizeof(uint32_t));
                buffer_leer(paq->buffer, &tamanio,   sizeof(uint32_t));

                // Leer los datos que hay que escribir
                void *datos = malloc(tamanio);
                buffer_leer(paq->buffer, datos, tamanio);
                paquete_destruir(paq);

                log_info(g_logger, "## Escritura — Dir. Física: %u — Tamaño: %u", direccion, tamanio);

                // ── Sección crítica ──────────────────────────────────────
                pthread_mutex_lock(&g_mutex_memoria);

                if (direccion + tamanio > g_tamanio) {
                    log_error(g_logger, "## Escritura fuera de rango: dir=%u tam=%u (tamaño MS=%u)",
                              direccion, tamanio, g_tamanio);
                } else {
                    // Copiar los bytes al bloque de memoria simulada
                    memcpy((char *) g_memoria + direccion, datos, tamanio);
                }

                pthread_mutex_unlock(&g_mutex_memoria);

                free(datos);

                // Simular latencia fuera del mutex
                if (g_cfg->memory_delay != NULL) {
                    usleep((*g_cfg->memory_delay) * 1000);
                }

                // Confirmar la escritura (sin datos en el payload)
                t_paquete *resp = paquete_crear(OP_MS_RESPUESTA);
                paquete_enviar(resp, fd);
                paquete_destruir(resp);
                break;
            }

            default:
                log_warning(g_logger, "## MS: opcode desconocido: %d", paq->codigo);
                paquete_destruir(paq);
                break;
        }
    }

    log_info(g_logger, "## Cliente (fd=%d) desconectado", fd);
}
