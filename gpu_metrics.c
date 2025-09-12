#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <stdarg.h>

#define PORT 7654
#define BUFFER_SIZE 16384
#define MAX_PATH 512
#define BUFFER_SIZE_ 128

#define DIV_TO_MB (1024 * 1024)
#define NO_DIV 1
#define DIV_TO_WATT 1000000.0
#define DIV_TO_MHZ 1000000
#define DIV_TO_CESIUS 1000.0
#define SCRAPE_TIME_PRECISION 1000000.0

typedef struct {
    char name[64];
    long utilization;
    double temperature;
    double power_usage;
    long vram_used;
    long vram_total;
    long sclk_clock;
    long mclk_clock;
    long voltage;
    long fan_rpm;
    double scrape_time;
    int found;
} GPUInfo;

typedef struct {
    char device_path[MAX_PATH]; // /sys/class/drm/card1/device
    char hwmon_path[MAX_PATH];  // /sys/class/drm/card1/device/hwmon/hwmon0
} GPU_Paths;

GPU_Paths gpu_paths;

int discover_gpu_paths(const char* card_name) {
    char pattern[MAX_PATH];
    glob_t glob_result;
    int found = 0;

    // build device path
    snprintf(gpu_paths.device_path, sizeof(gpu_paths.device_path),
             "/sys/class/drm/%s/device", card_name);

    // discover hwmon path
    snprintf(pattern, sizeof(pattern), "/sys/class/drm/%s/device/hwmon/hwmon*", card_name);
    if (glob(pattern, GLOB_NOSORT, NULL, &glob_result) == 0 && glob_result.gl_pathc > 0) {
        strncpy(gpu_paths.hwmon_path, glob_result.gl_pathv[0], MAX_PATH);
        globfree(&glob_result);
        found = 1;
    }

    return found;
}

int read_sysfs_file(const char *path, char *output) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    if (fgets(output, BUFFER_SIZE, file)) {
        // remove newline if present
        output[strcspn(output, "\n")] = 0;
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

int build_safe_path(char* dest, size_t dest_size, const char* base_path, const char* file_path) {
    // check if the combined path would fit
    size_t total_len = strlen(base_path) + strlen(file_path) + 2; // +2 for '/' and null terminator
    if (total_len > dest_size) {
        fprintf(stderr, "Path too long: %s/%s (%zu > %zu)\n",
                base_path, file_path, total_len, dest_size);
        return 0;
    }

    snprintf(dest, dest_size, "%s/%s", base_path, file_path);
    return 1;
}

int read_sysfs_metric_div_long(const char* file_path, double divisor, long* result) {
    char full_path[MAX_PATH];
    char buffer[64];

    if (!build_safe_path(full_path, sizeof(full_path), gpu_paths.device_path, file_path)) {
        return 0;
    }
    if (read_sysfs_file(full_path, buffer)) {
        *result = atol(buffer) / divisor;
        return 1;
    }
    return 0 ;
}

int read_sysfs_metric_div_double(const char* file_path, double divisor, double* result)
{
    char full_path[MAX_PATH];
    char buffer[64];
    if (!build_safe_path(full_path, sizeof(full_path), gpu_paths.device_path, file_path)) {
        return 0;
    }
    if (read_sysfs_file(full_path, buffer)) {
        *result = atol(buffer) / divisor;
        return 1;
    }
    return 0;
}

int read_sysfs_metric_div_glob_double(const char* file_path, double divisor, double* result)
{
    char full_path[MAX_PATH];
    char buffer[64];
    if (!build_safe_path(full_path, sizeof(full_path), gpu_paths.hwmon_path, file_path)) {
        return 0;
    }
    if (read_sysfs_file(full_path, buffer)) {
        *result = atol(buffer) / divisor;
        return 1;
    }
    return 0;
}

int read_sysfs_metric_div_glob_long(const char* file_path, long divisor, long* result)
{
    char full_path[MAX_PATH];
    char buffer[64];
    if (!build_safe_path(full_path, sizeof(full_path), gpu_paths.hwmon_path, file_path)) {
        return 0;
    }
    if (read_sysfs_file(full_path, buffer)) {
        *result = atol(buffer) / divisor;
        return 1;
    }
    return 0;
}

void parse_gpu_info(GPUInfo *metrics, char* card_name) {
    memset(metrics, 0, sizeof(GPUInfo));

    snprintf(metrics->name, sizeof(metrics->name), "%s", card_name);
    metrics->found = 0;

    // start recording the scrape time
    struct timeval start, end;
    gettimeofday(&start, NULL);
    // GPU Utilization
    metrics->found = read_sysfs_metric_div_long("gpu_busy_percent", NO_DIV, &metrics->utilization);
    // Power usage in Watts
    metrics->found = read_sysfs_metric_div_glob_double("power1_input", DIV_TO_WATT, &metrics->power_usage);
    // VRAM Used
    metrics->found = read_sysfs_metric_div_long("mem_info_vram_used", DIV_TO_MB , &metrics->vram_used);
    // VRAM Total
    metrics->found = read_sysfs_metric_div_long("mem_info_vram_total", DIV_TO_MB , &metrics->vram_total);
    // Temperature
    metrics->found = read_sysfs_metric_div_glob_double("temp1_input", DIV_TO_CESIUS , &metrics->temperature);
    // Sclk clock
    metrics->found = read_sysfs_metric_div_glob_long("freq1_input", DIV_TO_MHZ , &metrics->sclk_clock);
    // Mclk clock
    metrics->found = read_sysfs_metric_div_glob_long("freq2_input", DIV_TO_MHZ , &metrics->mclk_clock);
    // Voltage
    metrics->found = read_sysfs_metric_div_glob_long("in0_input", NO_DIV , &metrics->voltage);
    // Fan RPM
    metrics->found = read_sysfs_metric_div_glob_long("fan1_input", NO_DIV , &metrics->fan_rpm);
    // Scrape time
    gettimeofday(&end, NULL);
    metrics->scrape_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / SCRAPE_TIME_PRECISION;
}

typedef struct {
    char metrics[2048];
    int gpu_id;
} metrics_builder_t;

void metrics_builder_init(metrics_builder_t* builder, int gpu_id) {
    builder->metrics[0] = '\0';
    builder->gpu_id = gpu_id;
}

void metrics_builder_add_metric(metrics_builder_t* builder,
                               const char* help,
                               const char* name,
                               const char* format,
                               ...) {
    char temp[256];

    // build the base part
    snprintf(temp, sizeof(temp),
        "# HELP amd_gpu_%s %s\n"
        "# TYPE amd_gpu_%s gauge\n"
        "amd_gpu_%s{gpu=\"%d\"} ",
        name, help, name, name, builder->gpu_id);

    // append the formatted value
    char value_str[64];
    va_list args;
    va_start(args, format);
    vsnprintf(value_str, sizeof(value_str), format, args);
    va_end(args);

    strncat(temp, value_str, sizeof(temp) - strlen(temp) - 1);
    strncat(temp, "\n\n", sizeof(temp) - strlen(temp) - 1);

    strncat(builder->metrics, temp, sizeof(builder->metrics) - strlen(builder->metrics) - 1);
}

char* build_metrics_response() {
    static char response[BUFFER_SIZE];
    GPUInfo gpu_metrics;

    parse_gpu_info(&gpu_metrics, "card1");

    metrics_builder_t builder;
    metrics_builder_init(&builder, 0);

    metrics_builder_add_metric(&builder, "usage percentage", "usage_percent", "%.1d",
                               gpu_metrics.utilization);

    metrics_builder_add_metric(&builder, "temperature in Celsius", "temperature_celsius", "%.1f",
                               gpu_metrics.temperature);

    metrics_builder_add_metric(&builder, "power consumption in Watts", "power_watts", "%.1f",
                               gpu_metrics.power_usage);

    metrics_builder_add_metric(&builder, "vram memory total in MB", "vram_total", "%.1ld",
                               gpu_metrics.vram_total);

    metrics_builder_add_metric(&builder, "vram memory used in MB", "vram_current_used", "%.1ld",
                               gpu_metrics.vram_used);

    metrics_builder_add_metric(&builder, "sclk clock in voltage", "sclk_current_used", "%.1ld",
                               gpu_metrics.sclk_clock);

    metrics_builder_add_metric(&builder, "mclk clock in voltage", "mclk_current_used", "%.1ld",
                               gpu_metrics.mclk_clock);

    metrics_builder_add_metric(&builder, "memory activity percentage", "memory_activity_percentage", "%.1ld",
                               gpu_metrics.power_usage);

    metrics_builder_add_metric(&builder, "current voltage", "current_voltage", "%.1ld",
                               gpu_metrics.voltage);

    metrics_builder_add_metric(&builder, "current fan rpm", "current_fan_rpm", "%.1ld",
                               gpu_metrics.fan_rpm);

    metrics_builder_add_metric(&builder, "scrape metrics time", "scrape_metrics_time", "%.6f",
                               gpu_metrics.scrape_time);

    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(builder.metrics), builder.metrics);

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

    // discover GPU paths at server startup
    if (!discover_gpu_paths("card1")) {
        fprintf(stderr, "ERROR: Failed to discover GPU paths. Check if GPU is available.\n");
        exit(1);
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
