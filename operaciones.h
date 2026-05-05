/*
 * ============================================================
 * ARCHIVO : io/src/io_utils/operaciones.h
 * ROL     : Interfaz de las tres operaciones del módulo IO
 * ============================================================
 *
 * DISEÑO: Una función por tipo de IO.
 * Cada función recibe el paquete ya leído desde main(),
 * ejecuta la operación y envía la respuesta al KS.
 *
 * Por qué el paquete viene de main() y no se lee acá:
 *   main() ya hizo paquete_recibir(). El paquete tiene los
 *   datos en su buffer. Si cada función llamara a paquete_recibir()
 *   internamente, estaría esperando un SEGUNDO paquete que nunca llega
 *   → deadlock. El dueño del ciclo de vida del paquete es main().
 */
#ifndef IO_OPERACIONES_H_
#define IO_OPERACIONES_H_

/* Solo incluimos lo que los tipos de los parámetros necesitan */
#include <commons/log.h>
#include "utils/paquete.h"

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_sleep
 *
 * Recibe del KS: uint32_t pid | uint32_t tiempo_ms
 * Hace:          usleep(tiempo_ms * 1000)
 * Responde:      OP_IO_SLEEP_FIN | uint32_t pid
 *
 * Log INFO obligatorio (enunciado):
 *   "## PID: <pid> - Inicio de IO"
 *   "## PID: <pid> - Haciendo sleep por <tiempo_ms> milisegundos."
 *   "## PID: <pid> - Fin de IO"
 *
 * BUENA PRÁCTICA — NOMBRE DESCRIPTIVO:
 *   "io_ejecutar_sleep" dice exactamente qué hace.
 *   Evitar nombres como "handle_op", "procesar", "do_thing".
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_sleep(t_paquete *paq, int fd_scheduler, t_log *logger);

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_stdout
 *
 * Recibe del KS: uint32_t pid | uint32_t tamanio | bytes[tamanio]
 *   (el KS ya buscó los bytes en Kernel Memory antes de enviárnoslos)
 * Hace:          imprime en pantalla + log
 * Responde:      OP_IO_STDOUT_FIN | uint32_t pid
 *
 * Log INFO obligatorio (enunciado):
 *   "## PID: <pid> - Inicio de IO"
 *   "## PID: <pid> - <CONTENIDO A IMPRIMIR>"
 *   "## PID: <pid> - Fin de IO"
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_stdout(t_paquete *paq, int fd_scheduler, t_log *logger);

/* ══════════════════════════════════════════════════════════════
 * io_ejecutar_stdin
 *
 * Recibe del KS: uint32_t pid | uint32_t tamanio | uint32_t dir_logica
 * Hace:          lee tamanio bytes del teclado
 *                - si el usuario escribió MÁS → truncar
 *                - si escribió MENOS → rellenar con '\0'
 * Responde:      OP_IO_STDIN_FIN | pid | tamanio | dir_logica | bytes[tamanio]
 *   (el KS usa dir_logica para escribir en Kernel Memory)
 *
 * Log INFO obligatorio (enunciado):
 *   "## PID: <pid> - Inicio de IO"
 *   "## PID: <pid> - Ingrese <tamanio> caracteres:"
 *   "## PID: <pid> - Fin de IO"
 *
 * NOTA DE COORDINACIÓN CON EL EQUIPO DEL KS:
 *   El paquete OP_IO_STDIN debe incluir dir_logica (4 bytes extra).
 *   Verificar con el equipo del KS antes de integrar.
 * ══════════════════════════════════════════════════════════════ */
void io_ejecutar_stdin(t_paquete *paq, int fd_scheduler, t_log *logger);

#endif /* IO_OPERACIONES_H_ */
