#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/unistd.h>
#include <stdbool.h>

#define sys_kernel_logs 549
#define sys_uptime_s 550
#define sys_cpu_usage 551
#define sys_ram_usage 552
#define sys_my_encrypt 553

int main(int argc, char *argv[]) {
    
    long uptime = syscall(sys_uptime_s);
    if (uptime > 0) {
        printf("Uptime: %ld s\n", uptime);
    } else {
        printf("No se pudo obtener el uptime\n");
        printf("%ld s\n", uptime);
    }

    int cpu_usage;
    long resultCPU = syscall(sys_cpu_usage, &cpu_usage);
    if (resultCPU == 0) {
        printf("CPU in Usage: %d.%2d%%\n", cpu_usage/100, cpu_usage%100);
        printf("CPU free: %d.%2d%%\n", 99-cpu_usage/100, 100-cpu_usage%100);
    } else {
        printf("No se pudo obtener el uso de CPU (La syscall devolvio un estado de error)\n");
    }

    int ram_usage;
    long resultRAM = syscall(sys_ram_usage, &ram_usage);
    if (resultRAM == 0) {
        printf("RAM in Usage: %d.%2d%%\n", ram_usage/100, ram_usage%100);
        printf("RAM Free: %d.%2d%%\n", 99-ram_usage/100, 100-ram_usage%100);
    } else {
        printf("No se pudo obtener el uso de RAM (La syscall devolvio un estado de error)\n");
    }

    #define LOG_BUFFER_SIZE 1024
    char logs_buffer[LOG_BUFFER_SIZE];
    int actual_length = 0;
    long resultLogs;
    //Inicialr el buffer para evitar basura en la memoria 
    memset(logs_buffer, 0, LOG_BUFFER_SIZE);
    resultLogs = syscall(sys_kernel_logs, logs_buffer, LOG_BUFFER_SIZE, &actual_length);
    if (resultLogs == 0) {
        logs_buffer[actual_length] = '\0'; // Asegurar que el buffer este null-terminated
        printf("Longuitud real de logs: %d\n", actual_length);
        printf("Kernel Logs:\n%s\n", logs_buffer);
    } else {
        #include <stdio.h>
        printf("No se pudo obtener los logs del kernel (La syscall devolvio un estado de error)\n");
        printf("La syscall devolvio el codigo de error: %ld\n", resultLogs);
    }

    analizer();

    return 0;
}

void encryptAnalizer(int syscall_number) {
    char file_input[256] = {0}, file_output[256] = {0}, key[256] = {0};
    int threads_numbers = 0;
    char command[256];
    bool run = true;

    while(run){
        printf("\nIngrese un parametro (-p, -o, -k, -j o run para ejecutar): ");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "-p") == 0) {
            printf("Archivo de entrada: ");
            fgets(file_input, sizeof(file_input), stdin);
            file_input[strcspn(file_input, "\n")] = 0;

        } else if (strcmp(command, "-o") == 0) {
            printf("Archivo de salida: ");
            fgets(file_output, sizeof(file_output), stdin);
            file_output[strcspn(file_output, "\n")] = 0;

        } else if (strcmp(command, "-k") == 0) {
            printf("Clave: ");
            fgets(key, sizeof(key), stdin);
            key[strcspn(key, "\n")] = 0;

        } else if (strcmp(command, "-j") == 0) {
            printf("Número de hilos: ");
            scanf("%d", &threads_numbers);
            getchar();

        } else if (strcmp(command, "run") == 0) {

            if (strlen(file_input) == 0 || strlen(file_output) == 0 || strlen(key) == 0 || threads_numbers == 0) {
                printf("\nFaltan parametros obligatorios ...\n");
                continue;
            }

            long result = syscall(syscall_number, file_input, file_output, key, threads_numbers);
            if (result >= 0)
                printf("Archivo encriptado exitosamente\n");
            else
                printf("Ocurrió un error\n");

            return;  
        } else {
            printf("Comando no reconocido\n");
        }
    }
}

void analizer() {
    char command[256];
    bool run = true;

    while (run) {
        printf("\n****  Multithreading # Encrypt  ****\n");
        printf("1. Encriptar\n");
        printf("2. Salir\n\n");
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "1") == 0) {
            encryptAnalizer(sys_my_encrypt);
        } else if (strcmp(command, "2") == 0) {
            printf("Hasta luego !\n");
            run = false;
            return;
        }else {
            printf("Comando - %s - no reconocido, vuelva a intentarlo", command);
        }
    }
}

// gcc test.c -o test 
// ./test 100 100