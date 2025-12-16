// kernel/my_encrypt.c
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>

// Estructura que define "un pedazo" de trabajo para un hilo.
// Contiene punteros a los datos, la clave y dónde empezar/terminar.
typedef struct {
    unsigned char *buffer;        // Puntero a los datos completos del archivo en RAM
    size_t data_size;             // Tamaño total
    unsigned char *encryption_key;// Puntero a la clave en RAM
    size_t key_length;            // Largo de la clave
    size_t start_idx;             // Byte donde este hilo empieza a trabajar
    size_t end_idx;               // Byte donde este hilo termina
} DataFragment;

// Estructura para coordinar el hilo.
struct task_params {
    DataFragment data_fragment;   // Los datos que el hilo va a procesar
    struct completion completed_event; // Una "señal" para avisar cuando termine
};

// --- EL NÚCLEO DE LA OPERACIÓN ---
// Esta función es la que ejecuta cada hilo individualmente.
int perform_xor_operation(void *arg) {
    struct task_params *params = (struct task_params *)arg;
    DataFragment *fragment = &params->data_fragment;
    size_t i;

    printk(KERN_INFO "Thread iniciado: start_idx=%zu, end_idx=%zu\n", fragment->start_idx, fragment->end_idx);

    // Bucle principal: Recorre SOLO la sección del archivo asignada a este hilo
    for (i = fragment->start_idx; i < fragment->end_idx; i++) {
        // OPERACIÓN XOR (^=):
        // Toma el byte del archivo y le aplica XOR con un byte de la clave.
        // El operador % (módulo) hace que si la clave es corta, se repita en bucle.
        fragment->buffer[i] ^= fragment->encryption_key[i % fragment->key_length];
    }

    printk(KERN_INFO "Thread finalizado: start_idx=%zu, end_idx=%zu\n", fragment->start_idx, fragment->end_idx);
    
    // Avisa al hilo principal que este trabajador ha terminado
    complete(&params->completed_event);
    return 0;
}

// Función principal que prepara todo antes de lanzar los hilos
int handle_file_encryption(const char *input_filepath, const char *output_filepath, const char *key_filepath, int thread_count) {
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

    printk(KERN_INFO "Intentando abrir los archivos\n");

    // 1. ABRIR ARCHIVOS
    // filp_open es como fopen pero en espacio de kernel.
    input_file = filp_open(input_filepath, O_RDONLY, 0);
    // Para salida usamos O_CREAT (crear si no existe) y O_TRUNC (borrar contenido previo)
    output_file = filp_open(output_filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    key_file = filp_open(key_filepath, O_RDONLY, 0);

    // Verificación de errores al abrir archivos (IS_ERR verifica punteros inválidos)
    if (IS_ERR(input_file)) {
        ret_val = PTR_ERR(input_file);
        printk(KERN_ERR "Error al abrir el archivo de entrada: %d\n", ret_val);
        goto exit; // Salto al final para limpiar
    }
    // ... (chequeos similares para output y key_file omitidos por brevedad, son iguales) ...

    if (IS_ERR(output_file)) {
        ret_val = PTR_ERR(output_file);
        goto close_input_file;
    }
    if (IS_ERR(key_file)) {
        ret_val = PTR_ERR(key_file);
        goto close_output_file;
    }

    // 2. LEER LA CLAVE
    // Obtenemos el tamaño del archivo de la clave
    key_length = i_size_read(file_inode(key_file));
    if (key_length <= 0) {
        ret_val = -EINVAL;
        goto close_key_file;
    }

    // kmalloc: Reservamos memoria RAM del kernel para guardar la clave
    encryption_key = kmalloc(key_length, GFP_KERNEL);
    if (!encryption_key) {
        ret_val = -ENOMEM; // Error: No hay memoria RAM suficiente
        goto close_key_file;
    }

    // Leemos el contenido del archivo de clave a la RAM
    ret_val = kernel_read(key_file, encryption_key, key_length, &key_offset);
    if (ret_val < 0) goto free_encryption_key;

    // 3. LEER EL ARCHIVO DE ENTRADA (DATOS A CIFRAR)
    file_size = i_size_read(file_inode(input_file));
    if (file_size <= 0) {
        ret_val = -EINVAL;
        goto free_encryption_key;
    }

    // Reservamos RAM para TODO el archivo de entrada
    file_buffer = kmalloc(file_size, GFP_KERNEL);
    if (!file_buffer) {
        ret_val = -ENOMEM;
        goto free_encryption_key;
    }

    // Leemos todo el archivo a la memoria RAM (file_buffer)
    ret_val = kernel_read(input_file, file_buffer, file_size, &in_offset);
    if (ret_val < 0) goto free_file_buffer;

    // 4. PREPARAR HILOS (MULTITHREADING)
    // Asignamos memoria para las listas de control de hilos
    thread_list = kmalloc(sizeof(struct task_struct *) * thread_count, GFP_KERNEL);
    task_list = kmalloc(sizeof(struct task_params) * thread_count, GFP_KERNEL);
    fragment_list = kmalloc(sizeof(DataFragment) * thread_count, GFP_KERNEL);

    if (!thread_list || !task_list || !fragment_list) {
        ret_val = -ENOMEM;
        goto free_file_buffer;
    }

    // Calculamos cuánto trabajo le toca a cada hilo
    fragment_size = file_size / thread_count;
    extra_bytes = file_size % thread_count; // Lo que sobra si la división no es exacta

    // Bucle para crear y lanzar cada hilo
    for (i = 0; i < thread_count; i++) {
        // Configuramos los datos que este hilo específico va a usar
        fragment_list[i].buffer = file_buffer; // Todos apuntan al mismo buffer gigante
        fragment_list[i].data_size = file_size;
        fragment_list[i].encryption_key = encryption_key;
        fragment_list[i].key_length = key_length;
        
        // Calculamos dónde empieza y termina este hilo
        fragment_list[i].start_idx = i * fragment_size;
        // El último hilo se lleva los bytes extra que sobraron
        fragment_list[i].end_idx = (i == thread_count - 1) ? (i + 1) * fragment_size + extra_bytes : (i + 1) * fragment_size;

        task_list[i].data_fragment = fragment_list[i];
        init_completion(&task_list[i].completed_event); // Inicializamos el semáforo/aviso

        // kthread_run crea y arranca el hilo inmediatamente ejecutando 'perform_xor_operation'
        thread_list[i] = kthread_run(perform_xor_operation, &task_list[i], "xor_thread_%d", i);
        if (IS_ERR(thread_list[i])) {
            
            ret_val = PTR_ERR(thread_list[i]);
            goto free_all_resources;
        }
    }

    // 5. ESPERAR A LOS HILOS (SINCRONIZACIÓN)
    // El hilo principal se detiene aquí hasta que todos los trabajadores terminen
    for (i = 0; i < thread_count; i++) {
        wait_for_completion(&task_list[i].completed_event);
    }

    // 6. GUARDAR RESULTADO
    // Una vez que todos los hilos modificaron 'file_buffer', lo escribimos al disco
    ret_val = kernel_write(output_file, file_buffer, file_size, &out_offset);
    if (ret_val < 0) {
        printk(KERN_ERR "Error al escribir en salida: %d\n", ret_val);
    }

// 7. LIMPIEZA DE MEMORIA (GARBAGE COLLECTION MANUAL)
// En C y Kernel, debes liberar todo lo que reservaste con kmalloc
free_all_resources:
    kfree(thread_list);
    kfree(task_list);
    kfree(fragment_list);

free_file_buffer:
    kfree(file_buffer);

free_encryption_key:
    kfree(encryption_key);

close_key_file:
    filp_close(key_file, NULL);

close_output_file:
    filp_close(output_file, NULL);

close_input_file:
    filp_close(input_file, NULL);

exit:
    return ret_val;
}

// Definición de la System Call (lo que llama el usuario)
SYSCALL_DEFINE4(my_encrypt, const char __user *, input_filepath, const char __user *, output_filepath, const char __user *, key_filepath, int, thread_count) {
    char *k_input_filepath, *k_output_filepath, *k_key_filepath;
    int ret_val;

    // COPIAR DATOS DE USUARIO A KERNEL
    // strndup_user copia las cadenas de texto (rutas) de forma segura.
    // El kernel no puede leer directamente la memoria del usuario sin riesgo.
    k_input_filepath = strndup_user(input_filepath, PATH_MAX);
    k_output_filepath = strndup_user(output_filepath, PATH_MAX);
    k_key_filepath = strndup_user(key_filepath, PATH_MAX);

    // Verificar si falló la copia
    if (IS_ERR(k_input_filepath) || IS_ERR(k_output_filepath) || IS_ERR(k_key_filepath)) {
        ret_val = -EFAULT;
        goto free_memory;
    }

    // Llamar a la función lógica definida arriba
    ret_val = handle_file_encryption(k_input_filepath, k_output_filepath, k_key_filepath, thread_count);

free_memory:
   
    kfree(k_input_filepath);
    kfree(k_output_filepath);
    kfree(k_key_filepath);

    return ret_val;
}