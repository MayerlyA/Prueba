/*
 * ARCHIVO: kernel_memory/src/kernel_memory_utils/memory_sticks.c
 *
 * Ver memory_sticks.h para la descripción del módulo.
 */

#include "memory_sticks.h"
#include "memoria.h"

#include <utils/paquete.h>
#include <utils/sockets.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <commons/collections/list.h>

/* ─────────────────────────────────────────────────────────────────────
 * Estructura interna de un MS registrado
 * ───────────────────────────────────────────────────────────────────── */

typedef struct {
    int      fd;           /* Socket hacia el Memory Stick. */
    uint32_t offset;       /* Dirección física global donde empieza. */
    uint32_t tamanio;      /* Cuántos bytes tiene. */
    bool     conectado;    /* false si el MS se desconectó. */
} t_ms_info;

/* ─────────────────────────────────────────────────────────────────────
 * Estado global del módulo
 * ───────────────────────────────────────────────────────────────────── */

static t_list          *g_lista_ms  = NULL;
static pthread_mutex_t  g_mutex_ms  = PTHREAD_MUTEX_INITIALIZER;
static t_log           *g_logger    = NULL;

/* ─────────────────────────────────────────────────────────────────────
 * Helper privado: encontrar el MS que contiene la dirección dada
 * ───────────────────────────────────────────────────────────────────── */

/*
 * ms_para_dir(dir_fisica, idx_out)
 *   Retorna el t_ms_info* cuyo rango [offset, offset+tamanio) contiene
 *   la dirección dir_fisica, y escribe su índice en *idx_out.
 *   Retorna NULL si ningún MS la contiene o está desconectado.
 *
 *   DEBE llamarse con g_mutex_ms tomado.
 */
static t_ms_info *ms_para_dir(uint32_t dir_fisica, int *idx_out) {
    for (int i = 0; i < list_size(g_lista_ms); i++) {
        t_ms_info *ms = list_get(g_lista_ms, i);
        if (!ms->conectado) continue;
        if (dir_fisica >= ms->offset &&
            dir_fisica <  ms->offset + ms->tamanio) {
            if (idx_out) *idx_out = i;
            return ms;
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────
 * API pública
 * ───────────────────────────────────────────────────────────────────── */

void ms_init(t_log *logger) {
    g_lista_ms = list_create();
    g_logger   = logger;
}

void ms_destruir(void) {
    pthread_mutex_lock(&g_mutex_ms);
    for (int i = 0; i < list_size(g_lista_ms); i++) {
        t_ms_info *ms = list_get(g_lista_ms, i);
        if (ms->conectado) socket_cerrar(ms->fd);
        free(ms);
    }
    list_destroy(g_lista_ms);
    g_lista_ms = NULL;
    pthread_mutex_unlock(&g_mutex_ms);
}

uint32_t ms_registrar(int fd, uint32_t tamanio) {
    pthread_mutex_lock(&g_mutex_ms);

    /*
     * El offset del nuevo MS es la suma de los tamaños de todos los
     * MS ya registrados (conectados o no — la posición física no cambia
     * aunque un MS se desconecte).
     */
    uint32_t offset_nuevo = 0;
    for (int i = 0; i < list_size(g_lista_ms); i++) {
        offset_nuevo += ((t_ms_info *)list_get(g_lista_ms, i))->tamanio;
    }

    t_ms_info *nuevo = malloc(sizeof(t_ms_info));
    nuevo->fd        = fd;
    nuevo->offset    = offset_nuevo;
    nuevo->tamanio   = tamanio;
    nuevo->conectado = true;

    list_add(g_lista_ms, nuevo);

    pthread_mutex_unlock(&g_mutex_ms);
    return offset_nuevo;
}

int ms_cantidad(void) {
    pthread_mutex_lock(&g_mutex_ms);
    int n = list_size(g_lista_ms);
    pthread_mutex_unlock(&g_mutex_ms);
    return n;
}

/*
 * ms_leer_en_ms(ms, dir_local, tamanio, buf_dest)
 *   Función interna: manda OP_MS_LEER al MS específico y lee la respuesta.
 *   'dir_local' es la dirección dentro del MS (no la global).
 *   Escribe los bytes en buf_dest.
 *   Retorna true si la operación tuvo éxito.
 */
static bool ms_leer_en_ms(t_ms_info *ms, uint32_t dir_local,
                           uint32_t tamanio, void *buf_dest) {
    /* Construir el pedido: [dir_local][tamanio] */
    t_paquete *req = paquete_crear(OP_MS_LEER);
    buffer_agregar(req->buffer, &dir_local, sizeof(uint32_t));
    buffer_agregar(req->buffer, &tamanio,   sizeof(uint32_t));
    paquete_enviar(req, ms->fd);
    paquete_destruir(req);

    /* Esperar respuesta */
    t_paquete *resp = paquete_recibir(ms->fd);
    if (resp == NULL || resp->codigo != OP_MS_LEER_OK) {
        log_error(g_logger, "## Error lectura MS: dir_local=%u tam=%u",
                  dir_local, tamanio);
        if (resp) paquete_destruir(resp);
        return false;
    }

    /* Copiar los bytes al buffer de salida */
    if (tamanio > 0) {
        buffer_leer(resp->buffer, buf_dest, tamanio);
    }
    paquete_destruir(resp);
    return true;
}

/*
 * ms_escribir_en_ms(ms, dir_local, tamanio, datos)
 *   Función interna: manda OP_MS_ESCRIBIR al MS específico.
 *   Retorna true si la operación tuvo éxito.
 */
static bool ms_escribir_en_ms(t_ms_info *ms, uint32_t dir_local,
                               uint32_t tamanio, const void *datos) {
    /* Construir el pedido: [dir_local][tamanio][bytes] */
    t_paquete *req = paquete_crear(OP_MS_ESCRIBIR);
    buffer_agregar(req->buffer, &dir_local,       sizeof(uint32_t));
    buffer_agregar(req->buffer, &tamanio,          sizeof(uint32_t));
    if (tamanio > 0) {
        buffer_agregar(req->buffer, (void *)datos, tamanio);
    }
    paquete_enviar(req, ms->fd);
    paquete_destruir(req);

    /* Esperar respuesta */
    t_paquete *resp = paquete_recibir(ms->fd);
    if (resp == NULL || resp->codigo != OP_MS_ESCRIBIR_OK) {
        log_error(g_logger, "## Error escritura MS: dir_local=%u tam=%u",
                  dir_local, tamanio);
        if (resp) paquete_destruir(resp);
        return false;
    }
    paquete_destruir(resp);
    return true;
}

bool ms_leer(uint32_t dir_fisica, uint32_t tamanio, void *buffer_out) {
    if (tamanio == 0) return true;

    uint32_t restante   = tamanio;
    uint32_t dir_actual = dir_fisica;
    uint8_t *dest       = (uint8_t *)buffer_out;
    bool ok = true;

    pthread_mutex_lock(&g_mutex_ms);

    /*
     * Iteramos mientras queden bytes por leer.
     * En cada vuelta buscamos el MS que contiene dir_actual,
     * leemos lo que podemos (hasta el borde del MS o hasta
     * completar 'restante') y avanzamos.
     */
    while (restante > 0 && ok) {
        t_ms_info *ms = ms_para_dir(dir_actual, NULL);
        if (ms == NULL) {
            log_error(g_logger, "## No hay MS para dir física %u", dir_actual);
            ok = false;
            break;
        }

        /* Dirección dentro del MS (relativa a su inicio). */
        uint32_t dir_local = dir_actual - ms->offset;

        /* Cuántos bytes podemos leer en este MS antes de cruzar su límite. */
        uint32_t disponible_en_ms = ms->tamanio - dir_local;
        uint32_t a_leer = (restante < disponible_en_ms) ? restante : disponible_en_ms;

        ok = ms_leer_en_ms(ms, dir_local, a_leer, dest);

        dest       += a_leer;
        dir_actual += a_leer;
        restante   -= a_leer;
    }

    pthread_mutex_unlock(&g_mutex_ms);
    return ok;
}

bool ms_escribir(uint32_t dir_fisica, uint32_t tamanio, const void *datos) {
    if (tamanio == 0) return true;

    uint32_t    restante   = tamanio;
    uint32_t    dir_actual = dir_fisica;
    const uint8_t *src     = (const uint8_t *)datos;
    bool ok = true;

    pthread_mutex_lock(&g_mutex_ms);

    while (restante > 0 && ok) {
        t_ms_info *ms = ms_para_dir(dir_actual, NULL);
        if (ms == NULL) {
            log_error(g_logger, "## No hay MS para dir física %u", dir_actual);
            ok = false;
            break;
        }

        uint32_t dir_local        = dir_actual - ms->offset;
        uint32_t disponible_en_ms = ms->tamanio - dir_local;
        uint32_t a_escribir       = (restante < disponible_en_ms) ? restante : disponible_en_ms;

        ok = ms_escribir_en_ms(ms, dir_local, a_escribir, src);

        src        += a_escribir;
        dir_actual += a_escribir;
        restante   -= a_escribir;
    }

    pthread_mutex_unlock(&g_mutex_ms);
    return ok;
}

bool ms_mover_segmento(uint32_t base_src, uint32_t base_dst, uint32_t tamanio) {
    if (tamanio == 0) return true;

    /*
     * Leemos los bytes del origen en un buffer temporal y luego los
     * escribimos en el destino. Así evitamos pisarnos si src y dst
     * se superponen.
     */
    void *buf = malloc(tamanio);
    bool ok = ms_leer(base_src, tamanio, buf);
    if (ok) {
        ok = ms_escribir(base_dst, tamanio, buf);
    }
    free(buf);
    return ok;
}

bool ms_marcar_desconectado(int fd) {
    pthread_mutex_lock(&g_mutex_ms);
    bool encontrado = false;
    for (int i = 0; i < list_size(g_lista_ms); i++) {
        t_ms_info *ms = list_get(g_lista_ms, i);
        if (ms->fd == fd) {
            ms->conectado = false;
            encontrado = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex_ms);
    return encontrado;
}
