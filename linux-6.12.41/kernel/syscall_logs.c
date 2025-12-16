#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h> // Necesario para put_user()
#include <linux/tty.h>      // Incluye la declaración para do_syslog

/*
 * Declaración Externa:
 * do_syslog es la función interna del kernel que maneja el buffer de logs.
 * Se declara aquí para que el compilador la conozca al definir la syscall.
 *
 * NOTA: Esta declaración funciona si estás compilando este archivo como parte
 * de las fuentes de un kernel modificado.
 */
extern int do_syslog(int type, char __user *buf, int len);

/*
 * Acción de lectura de Logs:
 * El tipo 3 de la función syslog lee el buffer de logs sin borrar su contenido.
 */
#define SYSLOG_ACTION_READ 3

/*
 * SYSCALL_DEFINE3: Macro para definir la llamada al sistema.
 * - Nombre: get_kernel_logs
 * - Argumentos:
 * 1. char __user *buf: Puntero al buffer de usuario donde se copiarán los logs.
 * 2. size_t len: Tamaño máximo del buffer de usuario.
 * 3. int __user *actual_len_out: Puntero donde se escribirá la longitud real de los datos copiados.
 *
 * NOTA IMPORTANTE sobre "los últimos 5 logs":
 * La implementación estándar del kernel (do_syslog) devuelve el contenido del
 * buffer circular completo hasta la cantidad de bytes solicitados. Implementar
 * la lógica de "sólo los últimos 5 mensajes" requeriría un parsing avanzado del
 * formato de cada entrada de log, lo cual hace que la syscall sea innecesariamente
 * compleja. Por lo tanto, esta syscall devuelve el contenido del buffer completo.
 */
SYSCALL_DEFINE3(kernel_logs, char __user *, buf, size_t, len, int __user *, actual_len_out)
{
    long bytes_read;

    // 1. Validaciones iniciales
    if (len < 0)
        return -EINVAL; // Error de argumento inválido
    if (len == 0 || !buf)
        return 0;

    // Validar el puntero de salida (longitud real)
    if (!actual_len_out)
        return -EINVAL; // Error de argumento inválido

    // 2. Ejecutar la acción de lectura del log del kernel.
    // Llama a la función interna del kernel para leer los logs (SYSLOG_ACTION_READ = 3).
    bytes_read = do_syslog(SYSLOG_ACTION_READ, buf, len);

    // 3. Manejo de errores de la función interna (retornará un valor negativo en caso de error)
    if (bytes_read < 0)
        return (int)bytes_read; // Retorna el código de error directamente

    // 4. TRANSFERENCIA AL ESPACIO DE USUARIO: Escribir la cantidad de bytes leídos.
    // put_user intenta escribir el valor 'bytes_read' en la dirección 'actual_len_out'.
    if (put_user((int)bytes_read, actual_len_out))
        return -EFAULT; // Fallo al acceder a la memoria del usuario

    return 0; // Éxito
}