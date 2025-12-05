#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define INTERVALO_SEG 2
#define BUFFER_SIZE 1024

// Estructura para valores de memoria (en kB)
typedef struct {
    unsigned long mem_total;
    unsigned long mem_free;
    unsigned long mem_available;
    unsigned long swap_total;
    unsigned long swap_free;
} MemInfo;

// Función de lectura/parsing de /proc (modularizada)
int leer_meminfo(MemInfo *info) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        perror("fopen /proc/meminfo");
        return -1;  // Error: archivo no abierto
    }

    char linea[256];
    info->mem_total = 0;
    info->mem_free = 0;
    info->mem_available = 0;
    info->swap_total = 0;
    info->swap_free = 0;

    while (fgets(linea, sizeof(linea), f)) {
        if (strncmp(linea, "MemTotal:", 9) == 0) {
            sscanf(linea, "MemTotal: %lu kB", &info->mem_total);
        } else if (strncmp(linea, "MemFree:", 8) == 0) {
            sscanf(linea, "MemFree: %lu kB", &info->mem_free);
        } else if (strncmp(linea, "MemAvailable:", 13) == 0) {
            sscanf(linea, "MemAvailable: %lu kB", &info->mem_available);
        } else if (strncmp(linea, "SwapTotal:", 10) == 0) {
            sscanf(linea, "SwapTotal: %lu kB", &info->swap_total);
        } else if (strncmp(linea, "SwapFree:", 9) == 0) {
            sscanf(linea, "SwapFree: %lu kB", &info->swap_free);
        }
    }

    fclose(f);
    return 0;
}

// Funciones de red (modularizadas)
int crear_socket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
    }
    return sock;
}

int conectar_socket(int sock, const char *ip_recolector, int puerto) {
    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_recolector, &servidor.sin_addr) <= 0) {
        perror("inet_pton");
        return -1;
    }
    if (connect(sock, (struct sockaddr*)&servidor, sizeof(servidor)) < 0) {
        perror("connect");
        return -1;
    }
    return 0;
}

int enviar_datos(int sock, const char *mensaje) {
    if (send(sock, mensaje, strlen(mensaje), 0) < 0) {
        perror("send");
        return -1;
    }
    return 0;
}

const char* obtener_ip_agente(const char *ip_provisto) {
    return (ip_provisto && strlen(ip_provisto) > 0) ? ip_provisto : "127.0.0.1";
}

// Lógica principal
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_recolector> <puerto> [ip_logica_agente]\n", argv[0]);
        return 1;
    }

    const char *ip_recolector = argv[1];
    int puerto = atoi(argv[2]);
    const char *ip_agente = (argc >= 4) ? argv[3] : "";
    ip_agente = obtener_ip_agente(ip_agente);

    int sock = crear_socket();
    if (sock < 0) return 1;

    // Reintento de conexión si falla (manejo de error)
    int intentos = 0;
    while (conectar_socket(sock, ip_recolector, puerto) < 0) {
        if (++intentos > 5) {
            fprintf(stderr, "No se pudo conectar después de %d intentos\n", intentos);
            close(sock);
            return 1;
        }
        sleep(1);
    }

    printf("Conectado al recolector %s:%d como %s\n", ip_recolector, puerto, ip_agente);

    MemInfo info;
    while (1) {
        if (leer_meminfo(&info) < 0) {
            // Manejo de error: valores default y continuar
            info.mem_total = info.mem_available = info.mem_free = info.swap_total = info.swap_free = 0;
        }

        float mem_used_mb = (info.mem_total - info.mem_available) / 1024.0f;
        float mem_free_mb = info.mem_free / 1024.0f;
        float swap_total_mb = info.swap_total / 1024.0f;
        float swap_free_mb = info.swap_free / 1024.0f;

        char mensaje[BUFFER_SIZE];
        snprintf(mensaje, sizeof(mensaje), "MEM;%s;%.2f;%.2f;%.2f;%.2f\n",
                 ip_agente, mem_used_mb, mem_free_mb, swap_total_mb, swap_free_mb);

        if (enviar_datos(sock, mensaje) < 0) {
            // Desconexión: cerrar y salir (o reintentar si se quiere)
            break;
        }

        printf("[%ld] Enviado: %s", time(NULL), mensaje);
        sleep(INTERVALO_SEG);
    }

    close(sock);
    return 0;
}
