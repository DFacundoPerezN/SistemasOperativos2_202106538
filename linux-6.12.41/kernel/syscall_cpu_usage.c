#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>     // Necesario para mover datos entre Kernel y Usuario (put_user)
#include <linux/delay.h>       // Necesario para msleep()
#include <linux/kernel_stat.h> // Necesario para acceder a kcpustat_cpu()
#include <linux/sched/cputime.h>
#include <linux/cpumask.h>

/*
 * Helper: read_cpu_times 
 * Lee los tiempos acumulados de TODOS los CPUs del sistema.
 * El kernel mantiene contadores individuales por núcleo, aquí los sumamos.
 */
static void read_cpu_times(u64 *idle, u64 *total)
{
    int cpu;
    // Inicializamos contadores para cada estado posible del procesador
    u64 user = 0, nice = 0, system = 0, irq = 0, softirq = 0, steal = 0, iowait = 0, idle_t = 0;

    // for_each_online_cpu: Macro que itera solo sobre los núcleos activos/encendidos
    for_each_online_cpu(cpu) {
        // Obtenemos la estructura de estadísticas del CPU actual
        const struct kernel_cpustat *kcs = &kcpustat_cpu(cpu);

        // Sumamos los "ticks" acumulados en cada estado:
        user    += kcs->cpustat[CPUTIME_USER];    // Procesos de usuario normales
        nice    += kcs->cpustat[CPUTIME_NICE];    // Procesos con prioridad baja
        system  += kcs->cpustat[CPUTIME_SYSTEM];  // Tiempo ejecutando código del kernel
        irq     += kcs->cpustat[CPUTIME_IRQ];     // Interrupciones de hardware
        softirq += kcs->cpustat[CPUTIME_SOFTIRQ]; // Interrupciones de software
        steal   += kcs->cpustat[CPUTIME_STEAL];   // Tiempo "robado" por el hipervisor (si es una VM)
        iowait  += kcs->cpustat[CPUTIME_IOWAIT];  // Esperando al disco (I/O)
        idle_t  += kcs->cpustat[CPUTIME_IDLE];    // Tiempo totalmente inactivo
    }

    // Guardamos el tiempo inactivo total en el puntero proporcionado
    *idle  = idle_t;
    // El "Total" es la suma de estar ocupado + estar esperando + estar inactivo
    *total = user + nice + system + irq + softirq + steal + iowait + idle_t;
}

/*
 * Helper: get_cpu_percent_x100
 * Calcula el uso de CPU comparando dos momentos en el tiempo.
 * Retorna un valor de 0 a 10000 (donde 10000 es 100.00%).
 */
static u32 get_cpu_percent_x100(void)
{
    u64 idle1, total1, idle2, total2;
    s64 didle, dtotal;

    // 1. TOMA DE MUESTRA INICIAL (T0)
    read_cpu_times(&idle1, &total1);

    // 2. INTERVALO DE MUESTREO
    // Dormimos el proceso por 100ms. Sin esto, la diferencia sería 0 o irrelevante.
    // ADVERTENCIA: Esto bloquea al proceso que llamó a la syscall.
    msleep(100); 

    // 3. TOMA DE MUESTRA FINAL (T1)
    read_cpu_times(&idle2, &total2);

    // 4. CÁLCULO DEL DELTA (Diferencia entre T1 y T0)
    // Casteamos a s64 (signed) por seguridad en la resta
    didle  = (s64)idle2  - (s64)idle1;
    dtotal = (s64)total2 - (s64)total1;

    // Validación: Evitar división por cero si el intervalo fue anómalo
    if (dtotal <= 0)
        return 0;

    // 5. CÁLCULO DEL PORCENTAJE
    // Fórmula: (Actividad / Total) -> ((Total - Idle) / Total)
    // Multiplicamos por 10000ULL para manejar 2 decimales usando enteros (Fixed Point).
    // Usamos div64_u64 para dividir números de 64 bits de forma segura en el kernel.
    return (u32)div64_u64((u64)(dtotal - didle) * 10000ULL, (u64)dtotal);
}

/*
 * SYSCALL_DEFINE1: Macro para definir la llamada al sistema.
 * - Nombre: cpu_usage
 * - Argumentos: 1 (int *cpu_usage_out)
 * - __user: Indica que el puntero viene del espacio de usuario (no confiable).
 */
SYSCALL_DEFINE1(cpu_usage, int __user *, cpu_usage_out)
{
    u32 cpu;

    // 1. VALIDACIÓN DE PUNTEROS
    // Si el usuario pasa NULL, retornamos error de argumento inválido.
    if (!cpu_usage_out)
        return -EINVAL;

    // 2. EJECUCIÓN LÓGICA (Bloqueante por 100ms)
    cpu = get_cpu_percent_x100();

    // 3. TRANSFERENCIA AL USUARIO
    // put_user intenta escribir el valor 'cpu' en la dirección 'cpu_usage_out'.
    // Verifica permisos de escritura y manejo de memoria virtual.
    // Retorna -EFAULT si la dirección de memoria es inválida.
    if (put_user((int)cpu, cpu_usage_out))
        return -EFAULT;

    return 0; // Éxito (convención de retorno en C/Linux)
}