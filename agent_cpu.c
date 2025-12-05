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

// Estructura para stats de CPU
typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
    unsigned long long guest;
    unsigned long long guest_nice;
} CpuStats;

// Función de lectura/parsing de /proc (modularizada, como ejemplo 5.2)
int leer_cpu_stats(CpuStats *stats) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) {
        perror("fopen /proc/stat");
        return -1;
    }

    char linea[256];
    if (!fgets(linea, sizeof(linea), f)) {
        fclose(f);
        return -1;
    }

    if (strncmp(linea, "cpu ", 4) != 0) {
        fprintf(stderr, "No se encontró línea cpu\n");
        fclose(f);
        return -1;
    }

    int campos = sscanf(linea, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &stats->user, &stats->nice, &stats->system, &stats->idle,
                        &stats->iowait, &stats->irq, &stats->softirq, &stats->steal,
                        &stats->guest, &stats->guest_nice);

    fclose(f);
    if (campos < 8) {  // Mínimo user,nice,system,idle,iowait,irq,softirq,steal
        return -1;
    }
    return 0;
}

// Función para calcular porcentajes (parte de parsing/lógica)
void calcular_porcentajes(const CpuStats *prev, const CpuStats *curr,
                          float *total_usage, float *user_pct, float *system_pct, float *idle_pct) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;

    unsigned long long prev_non_idle = prev->user + prev->nice + prev->system + prev->irq + prev->softirq + prev->steal;
    unsigned long long curr_non_idle = curr->user + curr->nice + curr->system + curr->irq + curr->softirq + curr->steal;

    unsigned long long total_diff = (curr_idle + curr_non_idle) - (prev_idle + prev_non_idle);
    unsigned long long idle_diff = curr_idle - prev_idle;

    if (total_diff == 0) {
        *total_usage = *user_pct = *system_pct = *idle_pct = 0.0f;
        return;
    }

    *total_usage = 100.0f * (total_diff - idle_diff) / total_diff;
    *idle_pct = 100.0f * idle_diff / total_diff;

    unsigned long long user_diff = (curr->user + curr->nice) - (prev->user + prev->nice);
    unsigned long long system_diff = curr->system - prev->system;

    *user_pct = 100.0f * user_diff / total_diff;
    *system_pct = 100.0f * system_diff / total_diff;
}

// Funciones de red (igual que en agent_mem)
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

    CpuStats prev, curr;
    if (leer_cpu_stats(&prev) < 0) {
        fprintf(stderr, "Error inicial leyendo /proc/stat\n");
        close(sock);
        return 1;
    }

    sleep(INTERVALO_SEG);  // Espera para delta

    while (1) {
        if (leer_cpu_stats(&curr) < 0) {
            // Manejo error: usar prev como curr
            curr = prev;
        }

        float total_usage, user_pct, system_pct, idle_pct;
        calcular_porcentajes(&prev, &curr, &total_usage, &user_pct, &system_pct, &idle_pct);

        char mensaje[BUFFER_SIZE];
        snprintf(mensaje, sizeof(mensaje), "CPU;%s;%.2f;%.2f;%.2f;%.2f\n",
                 ip_agente, total_usage, user_pct, system_pct, idle_pct);

        if (enviar_datos(sock, mensaje) < 0) {
            break;
        }

        printf("[%ld] Enviado: %s", time(NULL), mensaje);

        prev = curr;
        sleep(INTERVALO_SEG);
    }

    close(sock);
    return 0;
}
