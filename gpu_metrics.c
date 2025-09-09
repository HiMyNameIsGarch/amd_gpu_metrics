#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#define PORT 7654
#define BUFFER_SIZE 16384

typedef struct {
    float usage;
    float temperature;
    float power;
    float memory_usage;
    float memory_activity;
    int fan_level;
    int fan_rpm;
    int voltage;
    int sclk_clock;
    int mclk_clock;
    char product_name[128];
    char gpu_arch[32];
} GPU_Metrics;

void parse_rocm_smi_output(GPU_Metrics *metrics) {
    // test the command before working with it, otherwise the metrics will show only 0 values
    FILE *fp = popen("/usr/bin/rocm-smi --showuse --showmemuse --showpower --showtemp --showfan --showvoltage --showclocks 2>/dev/null", "r");
    if (fp == NULL) {
        return;
    }

    char buffer[512];
    memset(metrics, 0, sizeof(GPU_Metrics));

    // harcoded matrics, maybe delete them later
    strcpy(metrics->gpu_arch, "gfx803");
    strcpy(metrics->product_name, "Radeon RX 570 Series");

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, "GPU use (%)")) {
            sscanf(buffer, "GPU[%*d] : GPU use (%*s) : %f", &metrics->usage);
        }
        else if (strstr(buffer, "Temperature (Sensor edge)")) {
            sscanf(buffer, "GPU[%*d] : Temperature (Sensor edge) (C) : %f", &metrics->temperature);
        }
        else if (strstr(buffer, "Current Socket Graphics Package Power")) {
            sscanf(buffer, "GPU[%*d] : Current Socket Graphics Package Power (W) : %f", &metrics->power);
        }
        else if (strstr(buffer, "GPU Memory Allocated (VRAM%)")) {
            sscanf(buffer, "GPU[%*d] : GPU Memory Allocated (VRAM%%) : %f", &metrics->memory_usage);
        }
        else if (strstr(buffer, "GPU Memory Read/Write Activity (%)")) {
            sscanf(buffer, "GPU[%*d] : GPU Memory Read/Write Activity (%*s) : %f", &metrics->memory_activity);
        }
        else if (strstr(buffer, "Fan RPM")) {
            sscanf(buffer, "GPU[%*d] : Fan RPM: %d", &metrics->fan_rpm);
        }
        else if (strstr(buffer, "Fan Level")) {
            sscanf(buffer, "GPU[%*d] : Fan Level: %*d (%d)", &metrics->fan_level);
        }
        else if (strstr(buffer, "Voltage")) {
            sscanf(buffer, "GPU[%*d] : Voltage (mV): %d", &metrics->voltage);
        }
        else if (strstr(buffer, "clock level")) {
            sscanf(buffer, "GPU[%*d] : mclk clock level: %*d: (%dMhz)", &metrics->mclk_clock);
            sscanf(buffer, "GPU[%*d] : sclk clock level: %*d: (%dMhz)", &metrics->sclk_clock);
        }
    }
    pclose(fp);
}

char* build_metrics_response() {
    static char response[BUFFER_SIZE];
    char metrics[BUFFER_SIZE - 512] = {0};
    GPU_Metrics gpu_metrics;

    time_t now = time(NULL);

    parse_rocm_smi_output(&gpu_metrics);

    // build comprehensive metrics string
    snprintf(metrics, sizeof(metrics),
        "# HELP amd_gpu_info GPU information\n"
        "# TYPE amd_gpu_info gauge\n"
        "amd_gpu_info{gpu=\"0\",product_name=\"%s\",architecture=\"%s\"} 1\n\n"

        "# HELP amd_gpu_usage_percent GPU usage percentage\n"
        "# TYPE amd_gpu_usage_percent gauge\n"
        "amd_gpu_usage_percent{gpu=\"0\"} %.1f\n\n"

        "# HELP amd_gpu_temperature_celsius GPU temperature in Celsius\n"
        "# TYPE amd_gpu_temperature_celsius gauge\n"
        "amd_gpu_temperature_celsius{gpu=\"0\"} %.1f\n\n"

        "# HELP amd_gpu_power_watts GPU power consumption in Watts\n"
        "# TYPE amd_gpu_power_watts gauge\n"
        "amd_gpu_power_watts{gpu=\"0\"} %.3f\n\n"

        "# HELP amd_gpu_memory_usage_percent GPU memory usage percentage\n"
        "# TYPE amd_gpu_memory_usage_percent gauge\n"
        "amd_gpu_memory_usage_percent{gpu=\"0\"} %.1f\n\n"

        "# HELP amd_gpu_memory_activity_percent GPU memory activity percentage\n"
        "# TYPE amd_gpu_memory_activity_percent gauge\n"
        "amd_gpu_memory_activity_percent{gpu=\"0\"} %.1f\n\n"

        "# HELP amd_gpu_fan_speed_percent GPU fan speed percentage\n"
        "# TYPE amd_gpu_fan_speed_percent gauge\n"
        "amd_gpu_fan_speed_percent{gpu=\"0\"} %d\n\n"

        "# HELP amd_gpu_current_voltage GPU current voltage\n"
        "# TYPE amd_gpu_current_voltage gauge\n"
        "amd_gpu_current_voltage{gpu=\"0\"} %d\n\n"

        "# HELP amd_gpu_current_sclk_clock GPU current sclk_clock\n"
        "# TYPE amd_gpu_current_sclk_clock gauge\n"
        "amd_gpu_current_sclk_clock{gpu=\"0\"} %d\n\n"

        "# HELP amd_gpu_current_mclk_clock GPU current mclk_clock\n"
        "# TYPE amd_gpu_current_mclk_clock gauge\n"
        "amd_gpu_current_mclk_clock{gpu=\"0\"} %d\n\n"

        "# HELP amd_gpu_scrape_timestamp Last metrics scrape timestamp\n"
        "# TYPE amd_gpu_scrape_timestamp gauge\n"
        "amd_gpu_scrape_timestamp{gpu=\"0\"} %ld\n\n"

        "# HELP amd_gpu_scrape_duration_seconds Time taken to scrape GPU metrics\n"
        "# TYPE amd_gpu_scrape_duration_seconds gauge\n"
        "amd_gpu_scrape_duration_seconds 0.001\n",

        gpu_metrics.product_name,
        gpu_metrics.gpu_arch,
        gpu_metrics.usage,
        gpu_metrics.temperature,
        gpu_metrics.power,
        gpu_metrics.memory_usage,
        gpu_metrics.memory_activity,
        gpu_metrics.fan_rpm,
        gpu_metrics.voltage,
        gpu_metrics.sclk_clock,
        gpu_metrics.mclk_clock,
        now
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
