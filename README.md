# Documentacion Practica 1 Introducción al Kernel

## Pasos para modificar el kernel -> Compilarlo -> Instalarlo

### 1. Extraer el código fuente del kernel  
[Descargar Kernel de Linux](https://www.kernel.org) (Descargar version longterm)

Extrae el código fuente del kernel de Linux (6.12.41 en este caso):
```sh
tar -xvf linux-6.12.41.tar.xz
```

### 2. Instalar dependencias necesarias  
Asegúrate de instalar los paquetes esenciales para la compilación del kernel:  

```sh
sudo apt -y install build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot
```

### 3. Configurar el kernel 

Generar configuracion basada en el kernel actual (mejor con sudo)
```sh
sudo make localmodconfig
```
Nota: Es posible que durante la creacion del archivo .config, aparezcan opciones de agregar nuevos modulos/drivers que no estan presentes en la configuracion actual del sistema, la opcion segura es usar "N" para no agregar nueva configuracion. 


Desactiva las claves de confianza del sistema para evitar problemas con la firma de módulos:  

```sh
sudo scripts/config --disable SYSTEM_TRUSTED_KEYS && \
sudo scripts/config --disable SYSTEM_REVOCATION_KEYS && \
sudo scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS "" && \
sudo scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
```

### 4. Compilar el kernel  
Ejecuta la compilación utilizando `fakeroot` y habilita el uso de N núcleos para acelerar el proceso:  

```sh
sudo fakeroot make -j<numero de nucleos deseados>
```
Nota: es importante adecuar l cantidad de nucleos a usar segun la maquina, usa 
```sh
nproc
```
para saber cuantos tiene y segun si vas a usar la maquina utiliza una buena cantidad de nucleos


### 5. Instalar Kernel
```sh
sudo make modules_install
sudo make install
```

### 6. Reiniciar
Para desplegar el nuevo kernel se puede hacer manualmente o con el comando:
```sh
sudo reboot
```

### Extras.
Si en caso de reiniciar no aparece la opcion de correr con el nuevo kernel entoces se sugiere hacer la siguiente modificacion al archivo /etc/default/grub
```shell

GRUB_DEFAULT=saved
GRUB_TIMEOUT_STYLE=menu
GRUB_TIMEOUT=10
GRUB_SAVEDEFAULT=true
GRUB_DISTRIBUTOR=`( . /etc/os-release; echo ${NAME:-Ubuntu} ) 2>/dev/null || echo Ubuntu`
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash"
GRUB_CMDLINE_LINUX=""
```

Una vez guardados los cambios, hacemos
```sh
sudo update-grub
```
Y vuelves a reiniciar.

## Frawmentos del Código

### Makefile del Kernel
Aqui definimos datos y como se procedera con la construcción del kernel
```Makefile
# SPDX-License-Identifier: GPL-2.0
VERSION = 6
PATCHLEVEL = 12
SUBLEVEL = 41
EXTRAVERSION = -diegofacundo-pereznicolau-202106538
NAME = Baby Opossum Posse
```

### Makefile del dentro de la carpeta del kernel de la carpet linux-version 

```Makefile
obj-y     = fork.o exec_domain.o panic.o \
	    cpu.o exit.o softirq.o resource.o \
	    sysctl.o capability.o ptrace.o user.o \
	    signal.o sys.o umh.o workqueue.o pid.o task_work.o \
	    extable.o params.o \
	    kthread.o sys_ni.o nsproxy.o \
	    notifier.o ksysfs.o cred.o reboot.o \
	    async.o range.o smpboot.o ucount.o regset.o ksyms_common.o \
        syscall_usac.o \
        syscall_time.o
```

### Testeo

```c
int width = 0, height = 0;
    if (syscall(sys_get_screen_resolution, &width, &height) == 0) {
        printf("Resolucion: %dx%d\n", width, height);
    } else {
        printf("No se pudo obtener la resolucion de pantalla\n");
    }
```
### Codigo 
```c
static int initVirtualKeyboard(void)
{
	int err;

	if (virtualKeyboard)
		return 0;

	virtualKeyboard = input_allocate_device();
	if (!virtualKeyboard)
		return -ENOMEM;

	virtualKeyboard->name  = "virtualKeyboard";
	virtualKeyboard->phys  = "vmd/input1";
	virtualKeyboard->id.bustype = BUS_VIRTUAL;
	virtualKeyboard->id.vendor  = 0x0010;
	virtualKeyboard->id.product = 0x0011;
	virtualKeyboard->id.version = 0x0001;

	__set_bit(EV_KEY, virtualKeyboard->evbit);

	{
		int code;
		for (code = 1; code < 256; code++)
			__set_bit(code, virtualKeyboard->keybit);
	}

	err = input_register_device(virtualKeyboard);
	if (err) {
		input_free_device(virtualKeyboard);
		virtualKeyboard = NULL;
		return err;
	}
	return 0;
}

SYSCALL_DEFINE2(move_mouse, int, dx, int, dy)
{
	int err = 0;

	err = initVirtualMouse();
	if (!err) {
		input_report_rel(virtualMouse, REL_X, dx);
		input_report_rel(virtualMouse, REL_Y, dy);
		input_sync(virtualMouse);
	}

	return err;
}
```


## Errores que ocurrieron

### 1. Espacio  blanco en Makefile
El problema estaba exactamente en la línea 43 del Makefile principal, donde abs_objtree := $(CURDIR)  tenía un espacio en blanco al final que impedía que la comparación ifeq ($(abs_objtree),$(CURDIR)) funcionara correctamente. Esto por el funcionamiento de los archivo Makefile que manejan un scanner que toma en cuenta espacios y tabulaciones.

El problema: Un espacio en blanco al final de la línea 43 en Makefile causaba que abs_objtree no fuera igual a $(CURDIR), lo que hacía que need-sub-make siempre fuera 1, creando un bucle infinito en __sub-make.

La solución: Eliminé el espacio en blanco al final de la línea

```Makefile
abs_objtree := $(CURDIR)

ifneq ($(sub_make_done),1)
```


### 2. Compilación de localmodconfig sin permisos
Esto ocurrio debido a la configuracion del sistema operativo donde muchos comandos de modificiacion de archivos deben realizarse como ussario root o administrador, de caso contrario fallaran. Para evitar esto usarse _sudo_ al incio de cada comando.

```sh
faxxd@vmhoe:/Sistemas_Operativos_2/Practica1/linux-6.12.41$  make modules_install
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/gpu/drm/ttm/ttm.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/gpu/drm/vmwgfx/vmwgfx.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/gpu/drm/drm_ttm_helper.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/firmware/efi/efi-pstore.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/firmware/dmi-sysfs.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/i2c/busses/i2c-piix4.ko': Permission denied
rm: cannot remove '/lib/modules/6.12.41-diegofacundo-pereznicolau-202106538/kernel/drivers/i2c/i2c-smbus.ko': Permission denied
```

### 3. kernel Makefile 
En el apartado de _obj-y_ cuando se agregue una nueva linea para los nuevos archivos que usaran para el sistema debe agregarse una linea invertida "\" en la linea anterior para que pueda analizar correctamente el MakeFile

```Makefile
obj-y     = fork.o exec_domain.o panic.o \
	    cpu.o exit.o softirq.o resource.o \
	    sysctl.o capability.o ptrace.o user.o \
	    signal.o sys.o umh.o workqueue.o pid.o task_work.o \
	    extable.o params.o \
	    kthread.o sys_ni.o nsproxy.o \
	    notifier.o ksysfs.o cred.o reboot.o \
	    async.o range.o smpboot.o ucount.o regset.o ksyms_common.o #falta "\"
        syscall_usac.o     
```

### 4. Mal nombre de la syscall en la tabla
Este error fue porque en la tabla de syscalls estaba mal referenciado la syscall del tiempo
´´´sh

ld: arch/x86/entry/syscall_64.o: in function `x64_sys_call':
/Sistemas_Operativos_2/Practica1/linux-6.12.41/./arch/x86/include/generated/asm/syscalls_64.h:551:(.text+0x16fe): undefined reference to `__x64_sys_uptime_syscall'
ld: arch/x86/entry/syscall_64.o:(.rodata+0x1130): undefined reference to `__x64_sys_uptime_syscall'
...
make[2]: *** [scripts/Makefile.vmlinux:34: vmlinux] Error 1
make[1]: *** [/Sistemas_Operativos_2/Practica1/linux-6.12.41/Makefile:1180: vmlinux] Error 2
make: *** [Makefile:224: __sub-make] Error 2

´´´
En la tabla con el error: 
´´´tbl
550 common uptime_syscall        sys_uptime_syscall
´´´

En el archivo C para definir la syscall: 
´´´C
SYSCALL_DEFINE0(uptime_ns) {
    unsigned long uptime_ns = ktime_get_ns();
    return uptime_ns;
}
´´´


Corregido 
´´´tbl
550 common uptime_ns        sys_uptime_ns
´´´
