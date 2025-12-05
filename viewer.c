#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#define MAX_AGENTES 100
#define SHM_NAME    "/monitor_shm"

typedef struct {
    char ip[32];

    time_t ultima_actualizacion;

    float cpu_usage;
    float cpu_user;
    float cpu_system;
    float cpu_idle;

    float mem_used_mb;
    float mem_free_mb;
    float swap_total_mb;
    float swap_free_mb;

    int activo;
} AgenteInfo;

typedef struct {
    int num_agentes;
    AgenteInfo agentes[MAX_AGENTES];
} SharedData;
SharedData *data = NULL;
#include "shared.h"

#define INTERVALO_SEG 2

int main() {
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    SharedData *data = mmap(NULL, sizeof(SharedData), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }
    close(shm_fd);

    while (1) {
        printf("\033[2J\033[H");  // Limpiar pantalla (como en el enunciado)

        time_t ahora = time(NULL);
        char *tiempo = ctime(&ahora);
        tiempo[strlen(tiempo)-1] = '\0';

        printf("=== MONITOR DE AGENTES === %s ===\n\n", tiempo);

        printf("%-15s %-8s %-8s %-8s %-8s %-12s %-12s %-12s %-12s\n",
               "IP", "CPU%", "CPU_user%", "CPU_sys%", "CPU_idle%", "Mem_used_MB", "Mem_free_MB", "Swap_tot_MB", "Swap_free_MB");
        printf("----------------------------------------------------------------------------------------------------\n");

        int hay_datos = 0;
        for (int i = 0; i < data->num_agentes; i++) {
            time_t diff = ahora - data->agentes[i].ultima_actualizacion;
            if (diff > 30) {  // Sin datos recientes
                data->agentes[i].activo = 0;
            }

            if (data->agentes[i].activo) {
                hay_datos = 1;
                printf("%-15s %8.1f %8.1f %8.1f %8.1f %12.1f %12.1f %12.1f %12.1f\n",
                       data->agentes[i].ip,
                       data->agentes[i].cpu_usage,
                       data->agentes[i].cpu_user,
                       data->agentes[i].cpu_system,
                       data->agentes[i].cpu_idle,
                       data->agentes[i].mem_used_mb,
                       data->agentes[i].mem_free_mb,
                       data->agentes[i].swap_total_mb,
                       data->agentes[i].swap_free_mb);
            } else {
                // Mostrar - para IPs conocidas pero sin datos recientes
                printf("%-15s %8s %8s %8s %8s %12s %12s %12s %12s\n",
                       data->agentes[i].ip, "-", "-", "-", "-", "-", "-", "-", "-");
            }
        }

        if (!hay_datos && data->num_agentes == 0) {
            printf("  (Sin datos - esperando agentes...)\n");
        }

        fflush(stdout);
        sleep(INTERVALO_SEG);
    }

    munmap(data, sizeof(SharedData));
    return 0;
}
