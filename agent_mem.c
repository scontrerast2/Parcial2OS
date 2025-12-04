#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
// Valores por defecto
#define INTERVALO_SEG     2          // segundos entre lecturas
#define BUFFER_SIZE       1024
// Función para leer un valor de /proc/meminfo (en kB)
unsigned long leer_meminfo_valor(const char *clave) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return 0;
    char linea[256];
    unsigned long valor = 0;
    while (fgets(linea, sizeof(linea), fp)) {
        if (strncmp(linea, clave, strlen(clave)) == 0) {
            sscanf(linea, "%*s %lu kB", &valor);
            break;
        }
    }
    fclose(fp);
    return valor;
}
// Obtener la IP lógica del agente (parámetro o localhost)
const char* obtener_ip_agente(const char *ip_provisto) {
    return (ip_provisto && strlen(ip_provisto) > 0) ? ip_provisto : "127.0.0.1";
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ip_recolector> <puerto> [ip_logica_agente]\n", argv[0]);
        fprintf(stderr, "Ejemplo: %s 192.168.1.100 5000 192.168.1.50\n", argv[0]);
        return 1;
    }
    const char *ip_recolector = argv[1];
    int puerto = atoi(argv[2]);
    const char *ip_agente = (argc >= 4) ? argv[3] : "";

    ip_agente = obtener_ip_agente(ip_agente);

    // Crear socket TCP
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_recolector, &servidor.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    // Conectar al recolector
    if (connect(sock, (struct sockaddr*)&servidor, sizeof(servidor)) < 0) {
        perror("connect");
        return 1;
    }

    printf("Conectado al recolector %s:%d como agente %s\n", ip_recolector, puerto, ip_agente);
    printf("Enviando datos cada %d segundos...\n", INTERVALO_SEG);

    while (1) {
        unsigned long mem_total     = leer_meminfo_MemTotal();
        unsigned long mem_available = leer_meminfo_MemAvailable();
        unsigned long mem_free      = leer_meminfo_MemFree();
        unsigned long swap_total    = leer_meminfo_SwapTotal();
        unsigned long swap_free     = leer_meminfo_SwapFree();

        // Cálculo de memoria usada en MB (redondeado)
        double mem_used_mb = (mem_total - mem_available) / 1024.0;

        // Convertir todo a MB
        double mem_free_mb    = mem_free / 1024.0;
        double swap_total_mb  = swap_total / 1024.0;
        double swap_free_mb   = swap_free / 1024.0;

        // Construir mensaje
        char mensaje[BUFFER_SIZE];
        snprintf(mensaje, sizeof(mensaje),
                 "MEM;%s;%.2f;%.2f;%.2f;%.2f\n",
                 ip_agente,
                 mem_used_mb,
                 mem_free_mb,
                 swap_total_mb,
                 swap_free_mb);

        // Enviar
        if (send(sock, mensaje, strlen(mensaje), 0) < 0) {
            perror("send");
            break;
        }

        printf("[%ld] Enviado: %s", time(NULL), mensaje);

        sleep(INTERVALO_SEG);
    }

    close(sock);
    return 0;
}
