#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#define PORT 7654
#define BUFFER_SIZE 16384
#define MAX_PATH 512
#define BUFFER_SIZE_ 128

typedef struct {
    char name[64];
    long utilization;
    long vram_used;
    long vram_total;
    long temperature;
    long power_usage;
    int found;
} GPUInfo;

int read_sysfs_file(const char *path, char *output) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    if (fgets(output, BUFFER_SIZE, file)) {
        // Remove newline if present
        output[strcspn(output, "\n")] = 0;
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

void parse_gpu_info(GPUInfo *metrics, char* card_name) {
    char path[MAX_PATH];
    char buffer[BUFFER_SIZE_];

    memset(metrics, 0, sizeof(GPUInfo));

    snprintf(metrics->name, sizeof(metrics->name), "%s", card_name);
    metrics->found = 0;

    // GPU Utilization
    snprintf(path, MAX_PATH, "/sys/class/drm/%s/gpu_busy_percent", card_name);
    if (read_sysfs_file(path, buffer)) {
        metrics->utilization = atol(buffer);
        metrics->found = 1;
    }

    // VRAM Used
    snprintf(path, MAX_PATH, "/sys/class/drm/%s/device/mem_info_vram_used", card_name);
    if (read_sysfs_file(path, buffer)) {
        metrics->vram_used = atol(buffer);
        metrics->found = 1;
    }

    // VRAM Total
    snprintf(path, MAX_PATH, "/sys/class/drm/%s/device/mem_info_vram_total", card_name);
    if (read_sysfs_file(path, buffer)) {
        metrics->vram_total = atol(buffer);
        metrics->found = 1;
    }

    // Temperature - try multiple possible locations
    glob_t glob_result;
    snprintf(path, MAX_PATH, "/sys/class/drm/%s/device/hwmon/hwmon*/temp1_input", card_name);
    if (glob(path, GLOB_NOSORT, NULL, &glob_result) == 0) {
        if (glob_result.gl_pathc > 0 && read_sysfs_file(glob_result.gl_pathv[0], buffer)) {
            metrics->temperature = atol(buffer) / 1000; // Convert millidegree to degree
            metrics->found = 1;
        }
        globfree(&glob_result);
    }
}

char* build_metrics_response() {
    static char response[BUFFER_SIZE];
    char metrics[BUFFER_SIZE - 512] = {0};
    GPUInfo gpu_metrics;

    time_t now = time(NULL);

    parse_gpu_info(&gpu_metrics, "card1");

    // build comprehensive metrics string
    snprintf(metrics, sizeof(metrics),
        "# HELP amd_gpu_usage_percent GPU usage percentage\n"
        "# TYPE amd_gpu_usage_percent gauge\n"
        "amd_gpu_usage_percent{gpu=\"0\"} %.1ld\n\n"

        "# HELP amd_gpu_temperature_celsius GPU temperature in Celsius\n"
        "# TYPE amd_gpu_temperature_celsius gauge\n"
        "amd_gpu_temperature_celsius{gpu=\"0\"} %.1ld\n\n"

        "# HELP amd_gpu_power_watts GPU power consumption in Watts\n"
        "# TYPE amd_gpu_power_watts gauge\n"
        "amd_gpu_power_watts{gpu=\"0\"} %.3ld\n\n"

        "# HELP amd_gpu_memory_usage_percent GPU memory usage percentage\n"
        "# TYPE amd_gpu_memory_usage_percent gauge\n"
        "amd_gpu_memory_usage_percent{gpu=\"0\"} %.1ld\n\n"

        "# HELP amd_gpu_memory_activity_percent GPU memory activity percentage\n"
        "# TYPE amd_gpu_memory_activity_percent gauge\n"
        "amd_gpu_memory_activity_percent{gpu=\"0\"} %.1ld\n\n"

        "# HELP amd_gpu_scrape_duration_seconds Time taken to scrape GPU metrics\n"
        "# TYPE amd_gpu_scrape_duration_seconds gauge\n"
        "amd_gpu_scrape_duration_seconds 0.001\n",

        gpu_metrics.utilization,
        gpu_metrics.temperature,
        gpu_metrics.power_usage,
        gpu_metrics.vram_used,
        gpu_metrics.vram_total
    );

    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(metrics), metrics);

    return response;
}


void handle_client(int client_socket) {
    char request[1024];
    ssize_t bytes_read = read(client_socket, request, sizeof(request) - 1);

    if (bytes_read > 0) {
        request[bytes_read] = '\0';
        char* response = NULL;

        if (strstr(request, "GET /metrics")) {
            response = build_metrics_response();
        }
        else {
            const char *not_found =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Connection: close\r\n"
                "\r\n"
                "404 - Not Found. Available endpoints: /metrics";
            write(client_socket, not_found, strlen(not_found));
            close(client_socket);
            return;
        }

        if (response) {
            write(client_socket, response, strlen(response));
        }
    }

    close(client_socket);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("AMD GPU Exporter running on port %d\n", PORT);
    printf("Metrics available at: http://localhost:%d/metrics\n", PORT);

    while (1) {
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        handle_client(client_socket);
    }

    return 0;
}
