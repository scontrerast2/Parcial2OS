#ifndef SHARED_H
#define SHARED_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
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
SharedData *shm_data = NULL;
#define BUFFER_SIZE 1024

SharedData *shm_data = NULL;

// Función para inicializar shared memory (modular)
int init_shared_memory() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }
    shm_data = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return -1;
    }
    close(shm_fd);

    memset(shm_data, 0, sizeof(SharedData));  // Inicializar
    return 0;
}

// Función para obtener/crear agente en shm (lógica principal)
AgenteInfo* obtener_o_crear_agente(const char *ip) {
    for (int i = 0; i < shm_data->num_agentes; i++) {
        if (strcmp(shm_data->agentes[i].ip, ip) == 0) {
            return &shm_data->agentes[i];
        }
    }
    if (shm_data->num_agentes >= MAX_AGENTES) return NULL;

    AgenteInfo *nuevo = &shm_data->agentes[shm_data->num_agentes++];
    strncpy(nuevo->ip, ip, sizeof(nuevo->ip)-1);
    nuevo->activo = 1;
    nuevo->ultima_actualizacion = time(NULL);
    nuevo->cpu_usage = nuevo->cpu_user = nuevo->cpu_system = nuevo->cpu_idle = 0.0f;
    nuevo->mem_used_mb = nuevo->mem_free_mb = nuevo->swap_total_mb = nuevo->swap_free_mb = 0.0f;
    return nuevo;
}

// Función de parsing de línea recibida (modularizada)
int parsear_linea(const char *linea, char *tipo, char *ip) {
    if (sscanf(linea, "%3s;%31[^;]", tipo, ip) != 2) {
        return -1;
    }
    return 0;
}

// Hilo para manejar cliente (lógica de red)
void* manejar_cliente(void *arg) {
    int client_sock = (long)arg;
    char buffer[BUFFER_SIZE];

    int recibidos;
    while ((recibidos = recv(client_sock, buffer, sizeof(buffer)-1, 0)) > 0) {
        buffer[recibidos] = '\0';
        char *linea = strtok(buffer, "\n");
        while (linea) {
            char *cr = strchr(linea, '\r');
            if (cr) *cr = '\0';

            char tipo[4];
            char ip[32];
            if (parsear_linea(linea, tipo, ip) < 0) {
                linea = strtok(NULL, "\n");
                continue;
            }

            AgenteInfo *ag = obtener_o_crear_agente(ip);
            if (ag) {
                ag->ultima_actualizacion = time(NULL);
                ag->activo = 1;

                if (strncmp(tipo, "MEM", 3) == 0) {
                    sscanf(linea, "MEM;%*[^;];%f;%f;%f;%f",
                           &ag->mem_used_mb, &ag->mem_free_mb,
                           &ag->swap_total_mb, &ag->swap_free_mb);
                } else if (strncmp(tipo, "CPU", 3) == 0) {
                    sscanf(linea, "CPU;%*[^;];%f;%f;%f;%f",
                           &ag->cpu_usage, &ag->cpu_user,
                           &ag->cpu_system, &ag->cpu_idle);
                }
            }
            linea = strtok(NULL, "\n");
        }
    }
    if (recibidos < 0) {
        perror("recv");
    }
    close(client_sock);
    return NULL;
}

// Lógica principal
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }
    int puerto = atoi(argv[1]);

    if (init_shared_memory() < 0) return 1;

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv = { .sin_family = AF_INET, .sin_port = htons(puerto), .sin_addr.s_addr = INADDR_ANY };
    if (bind(server_sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server_sock, 10) < 0) {
        perror("listen");
        return 1;
    }

    printf("Recolector escuchando en puerto %d\n", puerto);

    while (1) {
        int client = accept(server_sock, NULL, NULL);
        if (client < 0) {
            perror("accept");
            continue;  // Manejo error: continuar aceptando
        }

        pthread_t t;
        if (pthread_create(&t, NULL, manejar_cliente, (void*)(long)client) != 0) {
            perror("pthread_create");
            close(client);
        } else {
            pthread_detach(t);
        }
    }

    close(server_sock);
    return 0;
}
