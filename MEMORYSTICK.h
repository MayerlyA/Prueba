#ifndef KERNEL_MEMORY_MEMORY_STICKS_H
#define KERNEL_MEMORY_MEMORY_STICKS_H

/*
 * ARCHIVO: kernel_memory/src/kernel_memory_utils/memory_sticks.h
 *
 * Qué hace este módulo:
 *   Administra los Memory Sticks conectados al Kernel Memory.
 *
 *   Cada MS es una "rebanada" de la memoria total. Cuando el KM quiere
 *   leer o escribir en una dirección física, este módulo calcula EN QUÉ
 *   Memory Stick cae esa dirección y se la manda por el socket correcto.
 *
 *   Ejemplo con 2 MS de 512 bytes cada uno:
 *     - MS 0: offset 0    → 511
 *     - MS 1: offset 512  → 1023
 *   Una lectura en dir=600, tam=10 va al MS 1 con dir local = 600-512 = 88.
 *
 *   Una operación que cruza el límite entre dos MS (ej: dir=510, tam=10)
 *   se PARTE: primero 2 bytes al MS 0 y luego 8 bytes al MS 1.
 */

#include <stdint.h>
#include <stdbool.h>
#include <commons/log.h>

/* ─────────────────────────────────────────────────────────────────────
 * API pública
 * ───────────────────────────────────────────────────────────────────── */

/*
 * ms_init(logger)
 *   Inicializa la lista interna de Memory Sticks y el mutex.
 *   Debe llamarse UNA vez al arrancar.
 */
void ms_init(t_log *logger);

/*
 * ms_destruir()
 *   Libera las estructuras internas y cierra todos los sockets.
 */
void ms_destruir(void);

/*
 * ms_registrar(fd, tamanio)
 *   Registra un nuevo Memory Stick con el file descriptor 'fd' y
 *   su tamaño. El offset global que le corresponde se calcula
 *   automáticamente como la suma de los MS anteriores.
 *
 *   Retorna el offset base global asignado al nuevo MS.
 */
uint32_t ms_registrar(int fd, uint32_t tamanio);

/*
 * ms_cantidad()
 *   Retorna cuántos Memory Sticks están conectados actualmente.
 */
int ms_cantidad(void);

/*
 * ms_leer(dir_fisica, tamanio, buffer_out)
 *   Lee 'tamanio' bytes desde la dirección física global 'dir_fisica'.
 *   El resultado se escribe en 'buffer_out' (el llamador debe reservarlo).
 *
 *   Si la lectura cruza dos MS, se hacen dos pedidos separados y el
 *   resultado se concatena en buffer_out.
 *
 *   Retorna true si todas las lecturas fueron exitosas, false si algún
 *   MS respondió con error o se desconectó.
 */
bool ms_leer(uint32_t dir_fisica, uint32_t tamanio, void *buffer_out);

/*
 * ms_escribir(dir_fisica, tamanio, datos)
 *   Escribe 'tamanio' bytes de 'datos' a partir de la dirección física
 *   global 'dir_fisica'.
 *
 *   Si la escritura cruza dos MS, se hacen dos pedidos separados.
 *
 *   Retorna true si todo salió bien, false ante error.
 */
bool ms_escribir(uint32_t dir_fisica, uint32_t tamanio, const void *datos);

/*
 * ms_mover_segmento(base_src, base_dst, tamanio)
 *   Mueve 'tamanio' bytes de la dirección física 'base_src' a 'base_dst'.
 *   Se usa durante la compactación para reubicar segmentos.
 *
 *   Internamente: lee los bytes de base_src y los escribe en base_dst.
 *   Retorna true si el movimiento fue exitoso.
 */
bool ms_mover_segmento(uint32_t base_src, uint32_t base_dst, uint32_t tamanio);

/*
 * ms_marcar_desconectado(fd)
 *   Marca el MS con ese fd como desconectado.
 *   Se llama cuando paquete_recibir() retorna NULL en el hilo del MS.
 *   Retorna true si encontró el MS, false si no.
 */
bool ms_marcar_desconectado(int fd);

#endif /* KERNEL_MEMORY_MEMORY_STICKS_H */
