#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h> // Necesario para put_user()
#include <linux/mm.h>      // si_meminfo, struct sysinfo, PAGE_SIZE

/*
 * SYSCALL_DEFINE1: Macro para definir la llamada al sistema.
 * - Nombre: ram_usage_info
 * - Argumentos: 1 (int *ram_usage_out)
 * - __user: Indica que el puntero viene del espacio de usuario.
 *
 * Retorna el porcentaje de uso de RAM actual (multiplicado por 100, ej: 5050 para 50.50%).
 */
SYSCALL_DEFINE1(ram_usage, int __user *, ram_usage_out)
{
    struct sysinfo si;
    u64 total_ram_pages, free_ram_pages, used_ram_pages;
    u32 percent_usage_x100 = 0; // Valor a retornar (0 a 10000)
    u64 used_ram_x10000;

    // 1. VALIDACIÓN: Verificar que el puntero de usuario sea válido
    if (!ram_usage_out) {
        return -EINVAL; // Error de argumento inválido
    }

    // 2. OBTENER INFORMACIÓN DE MEMORIA
    // si_meminfo rellena la estructura 'si' con métricas de memoria en unidades de página.
    si_meminfo(&si);

    // 3. CALCULAR USO EN PÁGINAS (Usamos u64 para evitar desbordamiento)
    total_ram_pages = (u64)si.totalram;
    free_ram_pages = (u64)si.freeram;
    used_ram_pages = total_ram_pages - free_ram_pages;

    // 4. CALCULAR PORCENTAJE (Fixed Point: Multiplicado por 10000)
    if (total_ram_pages > 0) {
        // Multiplicamos el uso por 10000 antes de dividir por el total.
        // Esto nos da el porcentaje con dos decimales de precisión.
        used_ram_x10000 = used_ram_pages * 10000ULL;
        
        // div64_u64 es la forma segura de dividir u64 en el kernel
        percent_usage_x100 = (u32)div64_u64(used_ram_x10000, total_ram_pages);
    } else {
        // Caso de error: 0% de uso
        percent_usage_x100 = 0;
    }

    // 5. TRANSFERENCIA AL ESPACIO DE USUARIO
    // put_user intenta escribir el valor en la dirección 'ram_usage_out'.
    if (put_user((int)percent_usage_x100, ram_usage_out)) {
        return -EFAULT; // Fallo al acceder a la memoria del usuario
    }

    return 0; // Éxito
}