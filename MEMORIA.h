#ifndef KERNEL_MEMORY_MEMORIA_H
#define KERNEL_MEMORY_MEMORIA_H

/*
 * ARCHIVO: kernel_memory/src/kernel_memory_utils/memoria.h
 *
 * Qué hace este módulo:
 *   Administra el espacio de memoria principal usando SEGMENTACIÓN PURA.
 *
 *   - La memoria es una línea continua de bytes formada por todos los
 *     Memory Sticks conectados en orden de llegada.
 *   - Cada proceso tiene una TABLA DE SEGMENTOS: una lista de entradas
 *     (base, tamaño) que indica dónde vive cada segmento en esa línea.
 *   - El KM mantiene una lista de HUECOS (espacios libres) y una lista
 *     de SEGMENTOS OCUPADOS para poder aplicar Best Fit / Worst Fit al
 *     crear nuevos segmentos.
 */

#include <stdint.h>
#include <stdbool.h>
#include <commons/collections/list.h>
#include <utils/contexto.h>

/* ─────────────────────────────────────────────────────────────────────
 * Tipos de datos
 * ───────────────────────────────────────────────────────────────────── */

/*
 * Un HUECO es un rango libre de memoria física.
 *   base  = dirección física donde empieza el hueco
 *   tamanio = cuántos bytes libres tiene
 */
typedef struct {
    uint32_t base;
    uint32_t tamanio;
} t_hueco;

/*
 * Un SEGMENTO OCUPADO es un rango de memoria física que pertenece
 * a un proceso.
 *   pid        = dueño del segmento
 *   id_seg     = identificador del segmento dentro del proceso
 *   base       = dirección física donde empieza
 *   tamanio    = cuántos bytes ocupa
 */
typedef struct {
    uint32_t pid;
    uint32_t id_seg;
    uint32_t base;
    uint32_t tamanio;
} t_segmento_ocupado;

/* ─────────────────────────────────────────────────────────────────────
 * API pública
 * ───────────────────────────────────────────────────────────────────── */

/*
 * memoria_init()
 *   Inicializa las estructuras internas del módulo (listas, mutex).
 *   Debe llamarse UNA sola vez al arrancar el KM, antes de todo.
 *
 *   strategy: "BEST" o "WORST"
 */
void memoria_init(const char *strategy);

/*
 * memoria_destruir()
 *   Libera toda la memoria dinámica del módulo.
 *   Útil al cerrar el proceso.
 */
void memoria_destruir(void);

/*
 * memoria_agregar_ms(tamanio)
 *   Se llama cuando un Memory Stick se conecta e informa su tamaño.
 *   Agrega un nuevo HUECO al final de la memoria total con ese tamaño.
 *   Retorna la dirección base (offset global) que le fue asignado al MS.
 */
uint32_t memoria_agregar_ms(uint32_t tamanio);

/*
 * memoria_total()
 *   Retorna la suma de tamaños de todos los MS conectados hasta ahora.
 */
uint32_t memoria_total(void);

/*
 * memoria_libre_total()
 *   Retorna la suma de todos los huecos (memoria libre dispersa).
 *   Sirve para saber si COMPACTANDO habría espacio para un segmento.
 */
uint32_t memoria_libre_total(void);

/*
 * memoria_crear_segmento(pid, id_seg, tamanio, base_out)
 *   Intenta reservar 'tamanio' bytes para el segmento (pid, id_seg).
 *   Aplica Best Fit o Worst Fit según la estrategia configurada.
 *
 *   Retorna:
 *     true  → encontró hueco, reservó memoria, *base_out = dirección física
 *     false → no hay hueco contiguo suficiente (puede haber libre pero disperso)
 */
bool memoria_crear_segmento(uint32_t pid, uint32_t id_seg,
                             uint32_t tamanio, uint32_t *base_out);

/*
 * memoria_eliminar_segmento(pid, id_seg)
 *   Libera el segmento (pid, id_seg): lo elimina de la lista de ocupados
 *   y crea/fusiona el hueco correspondiente.
 */
void memoria_eliminar_segmento(uint32_t pid, uint32_t id_seg);

/*
 * memoria_liberar_proceso(pid)
 *   Llama a memoria_eliminar_segmento para TODOS los segmentos del proceso.
 *   Se usa en OP_FINALIZAR_PROCESO.
 */
void memoria_liberar_proceso(uint32_t pid);

/*
 * memoria_hay_espacio_para(pid_en_swap, lista_tamanios)
 *   Verifica si se pueden ubicar todos los segmentos de un proceso
 *   suspendido (cuyos tamaños están en lista_tamanios) sin compactar.
 *   Usa la misma lógica de Best/Worst Fit pero sin modificar nada.
 *
 *   lista_tamanios: t_list de uint32_t* con el tamaño de cada segmento.
 *   Retorna true si hay hueco para cada uno.
 */
bool memoria_hay_espacio_para(t_list *lista_tamanios);

/*
 * memoria_compactar(callback_mover)
 *   Compacta la memoria: corre todos los segmentos al principio,
 *   eliminando los huecos intermedios.
 *
 *   Por cada segmento que se mueve llama:
 *     callback_mover(pid, id_seg, base_vieja, base_nueva)
 *   para que el llamador actualice la tabla de segmentos del proceso
 *   y también mueva los bytes en el MS real.
 *
 *   Al terminar solo queda UN hueco grande al final de la memoria.
 */
void memoria_compactar(void (*callback_mover)(uint32_t pid, uint32_t id_seg,
                                              uint32_t base_vieja,
                                              uint32_t base_nueva));

/*
 * memoria_traducir(contexto, dir_logica, base_fisica_out, tamanio_out)
 *   Traduce una dirección lógica a física usando la tabla de segmentos
 *   del contexto.
 *
 *   dir_logica: número de segmento en los 16 bits altos + offset en los bajos.
 *   Retorna true si la traducción es válida, false si hay SEG_FAULT.
 *
 *   *base_fisica_out = base del segmento + offset
 *   *tamanio_out     = cuántos bytes quedan disponibles desde esa dir.
 */
bool memoria_traducir(t_contexto *ctx, uint32_t dir_logica,
                      uint32_t *base_fisica_out, uint32_t *tamanio_out);

#endif /* KERNEL_MEMORY_MEMORIA_H */
