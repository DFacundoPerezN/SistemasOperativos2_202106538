// kernel/my_decrypt.c
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/string.h>

// Estructura que define "un pedazo" de trabajo para un hilo.
typedef struct {
    unsigned char *buffer;          // Puntero a los datos completos del archivo en RAM
    size_t data_size;               // Tamaño total
    unsigned char *encryption_key;  // Puntero a la clave en RAM
    size_t key_length;              // Largo de la clave
    size_t start_idx;               // Byte donde este hilo empieza a trabajar
    size_t end_idx;                 // Byte donde este hilo termina
} DataFragment;

// Estructura para coordinar el hilo.
struct task_params {
    DataFragment data_fragment;     // Los datos que el hilo va a procesar
    struct completion completed_event; // Una "señal" para avisar cuando termine
};

// --- EL NÚCLEO DE LA OPERACIÓN (MISMO CÓDIGO XOR) ---
int perform_xor_decryption(void *arg) {
    struct task_params *params = (struct task_params *)arg;
    DataFragment *fragment = &params->data_fragment;
    size_t i;

    printk(KERN_INFO "Decryption Thread iniciado: start_idx=%zu, end_idx=%zu\n", fragment->start_idx, fragment->end_idx);

    // Bucle principal: Recorre SOLO la sección del archivo asignada a este hilo
    for (i = fragment->start_idx; i < fragment->end_idx; i++) {
        // OPERACIÓN XOR (^=): La misma lógica para encriptar y desencriptar.
        fragment->buffer[i] ^= fragment->encryption_key[i % fragment->key_length];
    }

    printk(KERN_INFO "Decryption Thread finalizado: start_idx=%zu, end_idx=%zu\n", fragment->start_idx, fragment->end_idx);
    
    // Avisa al hilo principal que este trabajador ha terminado
    complete(&params->completed_event);
    return 0;
}

// Función principal que prepara todo antes de lanzar los hilos
int handle_file_decryption(const char *input_filepath, const char *output_filepath, const char *key_filepath, int thread_count) {
    struct file *input_file, *output_file, *key_file; // Punteros a los archivos en el kernel
    loff_t in_offset = 0, out_offset = 0, key_offset = 0; // Posición de lectura/escritura (cursor)
    unsigned char *encryption_key, *file_buffer; // Buffers para guardar datos en RAM
    size_t file_size, key_length;
    
    // Arrays para gestionar los múltiples hilos
    struct task_params *task_list;
    struct task_struct **thread_list;
    DataFragment *fragment_list;
    
    size_t fragment_size, extra_bytes;
    int i, ret_val = 0;

    printk(KERN_INFO "Intentando descifrar: Abrir archivos\n");

    // 1. ABRIR ARCHIVOS
    input_file = filp_open(input_filepath, O_RDONLY, 0);
    output_file = filp_open(output_filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    key_file = filp_open(key_filepath, O_RDONLY, 0);

    // Verificación de errores al abrir archivos
    if (IS_ERR(input_file)) {
        ret_val = PTR_ERR(input_file);
        printk(KERN_ERR "Error al abrir el archivo cifrado de entrada: %d\n", ret_val);
        goto exit;
    }
    if (IS_ERR(output_file)) {
        ret_val = PTR_ERR(output_file);
        printk(KERN_ERR "Error al abrir el archivo de salida para descifrado: %d\n", ret_val);
        goto close_input_file;
    }
    if (IS_ERR(key_file)) {
        ret_val = PTR_ERR(key_file);
        printk(KERN_ERR "Error al abrir el archivo de la clave: %d\n", ret_val);
        goto close_output_file;
    }

    // 2. LEER LA CLAVE
    key_length = i_size_read(file_inode(key_file));
    if (key_length <= 0) {
        ret_val = -EINVAL;
        printk(KERN_ERR "Error: La clave esta vacia o es invalida.\n");
        goto close_key_file;
    }

    encryption_key = kmalloc(key_length, GFP_KERNEL);
    if (!encryption_key) {
        ret_val = -ENOMEM;
        goto close_key_file;
    }

    ret_val = kernel_read(key_file, encryption_key, key_length, &key_offset);
    if (ret_val < 0) goto free_encryption_key;

    // 3. LEER EL ARCHIVO DE ENTRADA (DATOS A DESCIFRAR)
    file_size = i_size_read(file_inode(input_file));
    if (file_size <= 0) {
        ret_val = -EINVAL;
        printk(KERN_ERR "Error: El archivo cifrado esta vacio o es invalido.\n");
        goto free_encryption_key;
    }

    file_buffer = kmalloc(file_size, GFP_KERNEL);
    if (!file_buffer) {
        ret_val = -ENOMEM;
        goto free_encryption_key;
    }

    ret_val = kernel_read(input_file, file_buffer, file_size, &in_offset);
    if (ret_val < 0) goto free_file_buffer;

    // 4. PREPARAR HILOS (MULTITHREADING)
    thread_list = kmalloc(sizeof(struct task_struct *) * thread_count, GFP_KERNEL);
    task_list = kmalloc(sizeof(struct task_params) * thread_count, GFP_KERNEL);
    fragment_list = kmalloc(sizeof(DataFragment) * thread_count, GFP_KERNEL);

    if (!thread_list || !task_list || !fragment_list) {
        ret_val = -ENOMEM;
        goto free_file_buffer;
    }

    // Calculamos cuánto trabajo le toca a cada hilo
    fragment_size = file_size / thread_count;
    extra_bytes = file_size % thread_count;

    // Bucle para crear y lanzar cada hilo
    for (i = 0; i < thread_count; i++) {
        fragment_list[i].buffer = file_buffer;
        fragment_list[i].data_size = file_size;
        fragment_list[i].encryption_key = encryption_key;
        fragment_list[i].key_length = key_length;
        
        fragment_list[i].start_idx = (size_t)i * fragment_size;
        fragment_list[i].end_idx = (i == thread_count - 1) 
                                 ? (size_t)(i + 1) * fragment_size + extra_bytes 
                                 : (size_t)(i + 1) * fragment_size;

        task_list[i].data_fragment = fragment_list[i];
        init_completion(&task_list[i].completed_event);

        // kthread_run crea y arranca el hilo inmediatamente
        thread_list[i] = kthread_run(perform_xor_decryption, &task_list[i], "xor_decrypt_thread_%d", i);
        if (IS_ERR(thread_list[i])) {
            ret_val = PTR_ERR(thread_list[i]);
            goto free_all_resources;
        }
    }

    // 5. ESPERAR A LOS HILOS (SINCRONIZACIÓN)
    for (i = 0; i < thread_count; i++) {
        wait_for_completion(&task_list[i].completed_event);
    }

    // 6. GUARDAR RESULTADO DESCIFRADO
    ret_val = kernel_write(output_file, file_buffer, file_size, &out_offset);
    if (ret_val < 0) {
        printk(KERN_ERR "Error al escribir el archivo descifrado: %d\n", ret_val);
    }

// 7. LIMPIEZA DE MEMORIA
free_all_resources:
    if (thread_list) kfree(thread_list);
    if (task_list) kfree(task_list);
    if (fragment_list) kfree(fragment_list);

free_file_buffer:
    if (file_buffer) kfree(file_buffer);

free_encryption_key:
    if (encryption_key) kfree(encryption_key);

close_key_file:
    if (key_file && !IS_ERR(key_file)) filp_close(key_file, NULL);

close_output_file:
    if (output_file && !IS_ERR(output_file)) filp_close(output_file, NULL);

close_input_file:
    if (input_file && !IS_ERR(input_file)) filp_close(input_file, NULL);

exit:
    return ret_val;
}

// Definición de la System Call (lo que llama el usuario)
SYSCALL_DEFINE4(my_decrypt, const char __user *, input_filepath, const char __user *, output_filepath, const char __user *, key_filepath, int, thread_count) {
    char *k_input_filepath = NULL, *k_output_filepath = NULL, *k_key_filepath = NULL;
    int ret_val;

    // COPIAR DATOS DE USUARIO A KERNEL
    k_input_filepath = strndup_user(input_filepath, PATH_MAX);
    k_output_filepath = strndup_user(output_filepath, PATH_MAX);
    k_key_filepath = strndup_user(key_filepath, PATH_MAX);

    // Verificar si falló la copia
    if (IS_ERR(k_input_filepath) || IS_ERR(k_output_filepath) || IS_ERR(k_key_filepath)) {
        ret_val = -EFAULT;
        goto free_memory;
    }

    // Llamar a la función lógica de desencriptación
    ret_val = handle_file_decryption(k_input_filepath, k_output_filepath, k_key_filepath, thread_count);

free_memory:
    if (!IS_ERR_OR_NULL(k_input_filepath)) kfree(k_input_filepath);
    if (!IS_ERR_OR_NULL(k_output_filepath)) kfree(k_output_filepath);
    if (!IS_ERR_OR_NULL(k_key_filepath)) kfree(k_key_filepath);

    return ret_val;
}