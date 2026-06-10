/*
 * ARCHIVO: kernel_memory/src/kernel_memory_utils/memoria.c
 *
 * Implementa la administración del espacio de memoria principal.
 * Ver memoria.h para la descripción completa de cada función.
 */

#include "memoria.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <commons/collections/list.h>

/* ─────────────────────────────────────────────────────────────────────
 * Estado global del módulo (solo visible dentro de este .c)
 * ───────────────────────────────────────────────────────────────────── */

/* Lista de t_hueco*: los rangos libres de la memoria. */
static t_list *g_huecos = NULL;

/* Lista de t_segmento_ocupado*: los rangos usados, uno por segmento vivo. */
static t_list *g_ocupados = NULL;

/* Tamaño total acumulado de todos los MS conectados. */
static uint32_t g_memoria_total = 0;

/* Algoritmo elegido: true = BEST FIT, false = WORST FIT. */
static bool g_usar_best_fit = true;

/* Mutex que protege g_huecos, g_ocupados y g_memoria_total. */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─────────────────────────────────────────────────────────────────────
 * Helpers privados (static = no visibles fuera de este .c)
 * ───────────────────────────────────────────────────────────────────── */

/*
 * hueco_nuevo(base, tamanio)
 *   Crea un t_hueco en el heap con los valores dados.
 *   El llamador es responsable de liberarlo con free().
 */
static t_hueco *hueco_nuevo(uint32_t base, uint32_t tamanio) {
    t_hueco *h = malloc(sizeof(t_hueco));
    h->base = base;
    h->tamanio = tamanio;
    return h;
}

/*
 * ocupado_nuevo(pid, id_seg, base, tamanio)
 *   Crea un t_segmento_ocupado en el heap.
 */
static t_segmento_ocupado *ocupado_nuevo(uint32_t pid, uint32_t id_seg,
                                          uint32_t base, uint32_t tamanio) {
    t_segmento_ocupado *s = malloc(sizeof(t_segmento_ocupado));
    s->pid     = pid;
    s->id_seg  = id_seg;
    s->base    = base;
    s->tamanio = tamanio;
    return s;
}

/*
 * elegir_hueco(tamanio)
 *   Recorre g_huecos y elige el índice del mejor candidato según la
 *   estrategia configurada:
 *     - BEST FIT:  el hueco MÁS PEQUEÑO que aún sea suficientemente grande.
 *     - WORST FIT: el hueco MÁS GRANDE disponible.
 *
 *   Retorna el índice en g_huecos, o -1 si ningún hueco es suficiente.
 *
 *   Nota: DEBE llamarse con g_mutex ya tomado.
 */
static int elegir_hueco(uint32_t tamanio) {
    int    mejor_idx  = -1;
    uint32_t mejor_tam = 0;

    for (int i = 0; i < list_size(g_huecos); i++) {
        t_hueco *h = list_get(g_huecos, i);

        /* El hueco debe ser suficientemente grande. */
        if (h->tamanio < tamanio) continue;

        if (mejor_idx == -1) {
            /* Primer candidato válido: tomarlo sin comparar. */
            mejor_idx = i;
            mejor_tam = h->tamanio;
        } else if (g_usar_best_fit && h->tamanio < mejor_tam) {
            /* BEST FIT: preferimos el hueco más chico que alcance. */
            mejor_idx = i;
            mejor_tam = h->tamanio;
        } else if (!g_usar_best_fit && h->tamanio > mejor_tam) {
            /* WORST FIT: preferimos el hueco más grande. */
            mejor_idx = i;
            mejor_tam = h->tamanio;
        }
    }
    return mejor_idx;
}

/*
 * fusionar_huecos_adyacentes()
 *   Después de liberar un segmento pueden quedar dos huecos contiguos
 *   en la lista. Esta función los fusiona en uno solo para evitar
 *   fragmentación interna en la lista (no fragmentación de memoria,
 *   esa es inevitable sin compactación).
 *
 *   Estrategia: ordena los huecos por base y fusiona los que se tocan.
 *   DEBE llamarse con g_mutex ya tomado.
 */
static int comparar_huecos_por_base(void *a, void *b) {
    /* Función de comparación para list_sort: ordena de menor a mayor base. */
    return (int)((t_hueco *)a)->base - (int)((t_hueco *)b)->base;
}

static void fusionar_huecos_adyacentes(void) {
    /* 1. Ordenar por dirección base. */
    list_sort(g_huecos, comparar_huecos_por_base);

    /* 2. Recorrer de corrido: si dos huecos se tocan, fusionarlos. */
    int i = 0;
    while (i < list_size(g_huecos) - 1) {
        t_hueco *h1 = list_get(g_huecos, i);
        t_hueco *h2 = list_get(g_huecos, i + 1);

        if (h1->base + h1->tamanio == h2->base) {
            /* Se tocan: absorbemos h2 en h1 y eliminamos h2. */
            h1->tamanio += h2->tamanio;
            list_remove(g_huecos, i + 1);
            free(h2);
            /* No avanzamos i: h1 puede ahora tocar al siguiente. */
        } else {
            i++;
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * Implementación de la API pública
 * ───────────────────────────────────────────────────────────────────── */

void memoria_init(const char *strategy) {
    g_huecos       = list_create();
    g_ocupados     = list_create();
    g_memoria_total = 0;
    /* Comparamos ignorando mayúsculas/minúsculas para robustez. */
    g_usar_best_fit = (strcmp(strategy, "BEST") == 0);
}

void memoria_destruir(void) {
    pthread_mutex_lock(&g_mutex);
    list_destroy_and_destroy_elements(g_huecos,   free);
    list_destroy_and_destroy_elements(g_ocupados, free);
    g_huecos   = NULL;
    g_ocupados = NULL;
    pthread_mutex_unlock(&g_mutex);
}

uint32_t memoria_agregar_ms(uint32_t tamanio) {
    pthread_mutex_lock(&g_mutex);

    /*
     * El nuevo MS empieza justo donde termina la memoria actual.
     * Ejemplo: si ya había 1024 bytes, el nuevo MS empieza en la
     * dirección física 1024.
     */
    uint32_t base_nueva = g_memoria_total;
    g_memoria_total += tamanio;

    /* Agregar el nuevo rango como un hueco libre al final de la lista. */
    list_add(g_huecos, hueco_nuevo(base_nueva, tamanio));

    /* Intentamos fusionar por si el último hueco anterior terminaba justo
     * donde empieza este (solo ocurre si el MS anterior terminó en un hueco). */
    fusionar_huecos_adyacentes();

    pthread_mutex_unlock(&g_mutex);
    return base_nueva;
}

uint32_t memoria_total(void) {
    return g_memoria_total;
}

uint32_t memoria_libre_total(void) {
    pthread_mutex_lock(&g_mutex);
    uint32_t libre = 0;
    for (int i = 0; i < list_size(g_huecos); i++) {
        libre += ((t_hueco *)list_get(g_huecos, i))->tamanio;
    }
    pthread_mutex_unlock(&g_mutex);
    return libre;
}

bool memoria_crear_segmento(uint32_t pid, uint32_t id_seg,
                             uint32_t tamanio, uint32_t *base_out) {
    pthread_mutex_lock(&g_mutex);

    int idx = elegir_hueco(tamanio);
    if (idx == -1) {
        /* No hay ningún hueco contiguo suficientemente grande. */
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    t_hueco *h = list_get(g_huecos, idx);
    *base_out = h->base;

    /*
     * Dos casos al reservar dentro del hueco:
     *   a) El segmento ocupa todo el hueco → eliminar el hueco.
     *   b) El segmento ocupa parte del hueco → reducirlo por la izquierda.
     */
    if (h->tamanio == tamanio) {
        list_remove(g_huecos, idx);
        free(h);
    } else {
        h->base    += tamanio;
        h->tamanio -= tamanio;
    }

    /* Registrar el segmento en la lista de ocupados. */
    list_add(g_ocupados, ocupado_nuevo(pid, id_seg, *base_out, tamanio));

    pthread_mutex_unlock(&g_mutex);
    return true;
}

void memoria_eliminar_segmento(uint32_t pid, uint32_t id_seg) {
    pthread_mutex_lock(&g_mutex);

    /* Buscar el segmento en la lista de ocupados. */
    t_segmento_ocupado *encontrado = NULL;
    int encontrado_idx = -1;

    for (int i = 0; i < list_size(g_ocupados); i++) {
        t_segmento_ocupado *s = list_get(g_ocupados, i);
        if (s->pid == pid && s->id_seg == id_seg) {
            encontrado     = s;
            encontrado_idx = i;
            break;
        }
    }

    if (encontrado == NULL) {
        /* El segmento no existe: no hacemos nada (ya fue liberado o nunca existió). */
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    /* Crear un hueco donde estaba el segmento. */
    list_add(g_huecos, hueco_nuevo(encontrado->base, encontrado->tamanio));

    /* Eliminar de la lista de ocupados y liberar la estructura. */
    list_remove(g_ocupados, encontrado_idx);
    free(encontrado);

    /* Fusionar huecos adyacentes para mantener la lista limpia. */
    fusionar_huecos_adyacentes();

    pthread_mutex_unlock(&g_mutex);
}

void memoria_liberar_proceso(uint32_t pid) {
    pthread_mutex_lock(&g_mutex);

    /*
     * Eliminamos de g_ocupados todos los segmentos con ese PID y creamos
     * los huecos correspondientes. Usamos un índice manual porque
     * modificamos la lista mientras la recorremos.
     */
    int i = 0;
    while (i < list_size(g_ocupados)) {
        t_segmento_ocupado *s = list_get(g_ocupados, i);
        if (s->pid == pid) {
            list_add(g_huecos, hueco_nuevo(s->base, s->tamanio));
            list_remove(g_ocupados, i);
            free(s);
            /* No incrementamos i porque el siguiente elemento pasó al índice i. */
        } else {
            i++;
        }
    }

    fusionar_huecos_adyacentes();

    pthread_mutex_unlock(&g_mutex);
}

bool memoria_hay_espacio_para(t_list *lista_tamanios) {
    pthread_mutex_lock(&g_mutex);

    /*
     * Simulamos la asignación uno a uno con copias de los huecos.
     * Si todos los segmentos encuentran hueco → retornamos true.
     * Después descartamos la simulación sin modificar el estado real.
     */

    /* Copiar la lista de huecos para la simulación. */
    t_list *huecos_sim = list_create();
    for (int i = 0; i < list_size(g_huecos); i++) {
        t_hueco *orig = list_get(g_huecos, i);
        list_add(huecos_sim, hueco_nuevo(orig->base, orig->tamanio));
    }

    bool todos_caben = true;

    for (int j = 0; j < list_size(lista_tamanios) && todos_caben; j++) {
        uint32_t tam = *(uint32_t *)list_get(lista_tamanios, j);

        /* Buscar el mejor/peor hueco en la lista simulada. */
        int    mejor_idx = -1;
        uint32_t mejor_tam = 0;

        for (int k = 0; k < list_size(huecos_sim); k++) {
            t_hueco *h = list_get(huecos_sim, k);
            if (h->tamanio < tam) continue;
            if (mejor_idx == -1) {
                mejor_idx = k;
                mejor_tam = h->tamanio;
            } else if (g_usar_best_fit && h->tamanio < mejor_tam) {
                mejor_idx = k;
                mejor_tam = h->tamanio;
            } else if (!g_usar_best_fit && h->tamanio > mejor_tam) {
                mejor_idx = k;
                mejor_tam = h->tamanio;
            }
        }

        if (mejor_idx == -1) {
            todos_caben = false;
        } else {
            /* "Reservar" en la simulación. */
            t_hueco *h = list_get(huecos_sim, mejor_idx);
            if (h->tamanio == tam) {
                list_remove(huecos_sim, mejor_idx);
                free(h);
            } else {
                h->base    += tam;
                h->tamanio -= tam;
            }
        }
    }

    /* Liberar la copia de simulación. */
    list_destroy_and_destroy_elements(huecos_sim, free);

    pthread_mutex_unlock(&g_mutex);
    return todos_caben;
}

/*
 * Comparador auxiliar para ordenar segmentos por dirección base.
 * Se usa en memoria_compactar() para procesar los segmentos de izquierda
 * a derecha y moverlos al principio sin pisarse.
 */
static int comparar_ocupados_por_base(void *a, void *b) {
    return (int)((t_segmento_ocupado *)a)->base -
           (int)((t_segmento_ocupado *)b)->base;
}

void memoria_compactar(void (*callback_mover)(uint32_t pid, uint32_t id_seg,
                                              uint32_t base_vieja,
                                              uint32_t base_nueva)) {
    pthread_mutex_lock(&g_mutex);

    /* 1. Ordenar los segmentos por dirección base (de izquierda a derecha). */
    list_sort(g_ocupados, comparar_ocupados_por_base);

    /*
     * 2. Recorrer los segmentos y "correrlos" al principio uno a uno.
     *    cursor = la próxima dirección libre desde el inicio.
     */
    uint32_t cursor = 0;

    for (int i = 0; i < list_size(g_ocupados); i++) {
        t_segmento_ocupado *s = list_get(g_ocupados, i);

        if (s->base != cursor) {
            /*
             * El segmento no está pegado al cursor: hay un hueco antes.
             * Lo movemos a la dirección 'cursor'.
             */
            uint32_t base_vieja = s->base;
            s->base = cursor;

            /*
             * Notificamos al llamador para que:
             *   a) Actualice la tabla de segmentos del proceso (contexto).
             *   b) Mueva los bytes reales en el Memory Stick.
             */
            callback_mover(s->pid, s->id_seg, base_vieja, cursor);
        }

        cursor += s->tamanio;
    }

    /*
     * 3. Reconstruir la lista de huecos: ahora hay un solo hueco grande
     *    al final de los datos compactados.
     */
    list_destroy_and_destroy_elements(g_huecos, free);
    g_huecos = list_create();

    if (cursor < g_memoria_total) {
        list_add(g_huecos, hueco_nuevo(cursor, g_memoria_total - cursor));
    }

    pthread_mutex_unlock(&g_mutex);
}

bool memoria_traducir(t_contexto *ctx, uint32_t dir_logica,
                      uint32_t *base_fisica_out, uint32_t *tamanio_out) {
    if (ctx == NULL) return false;

    /*
     * Formato de la dirección lógica:
     *   - Bits 31..16 → número de segmento (índice en la tabla de segmentos)
     *   - Bits 15..0  → offset dentro del segmento
     *
     * Ejemplo: dir_logica = 0x00020050
     *   → segmento 2, offset 0x50 (80 bytes desde la base del segmento)
     */
    uint32_t num_seg = (dir_logica >> 16) & 0xFFFF;
    uint32_t offset  =  dir_logica        & 0xFFFF;

    /* Verificar que el número de segmento sea válido. */
    if (num_seg >= ctx->num_segmentos) return false;

    t_segmento *seg = &ctx->tabla_segmentos[num_seg];

    /* Verificar que el offset no se salga del segmento (SEG_FAULT). */
    if (offset >= seg->size) return false;

    *base_fisica_out = seg->base + offset;
    *tamanio_out     = seg->size - offset;

    return true;
}
