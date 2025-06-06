#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/types.h>
#include <json-c/json.h>
#include <signal.h>
#include <sys/stat.h>
#include "mysyslog.h"

#define BUFFER_SIZE 4096

// Проверка, есть ли пользователь в /etc/myRPC/users.conf
int user_allowed(const char *username) {
    FILE *fp = fopen("/etc/myRPC/users.conf", "r");
    if (!fp) return 0;

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

// Удаление пробелов и табуляции с краёв строки
char *trim(char *str) {
    while (*str == ' ' || *str == '\t') str++;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) *end-- = '\0';
    return str;
}

// Экранирование строки для безопасной передачи в `sh -c`
char *shell_escape(const char *input) {
    size_t len = strlen(input);
    char *escaped = malloc(len * 4 + 3);
    if (!escaped) return NULL;

    char *p = escaped;
    *p++ = '\'';
    for (size_t i = 0; i < len; ++i) {
        if (input[i] == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = input[i];
        }
    }
    *p++ = '\'';
    *p = '\0';
    return escaped;
}

// Чтение содержимого файла в строку
char *read_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return strdup("(empty)");

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) return strdup("(allocation error)");
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);
    return content;
}

// Обработка JSON-запроса и формирование JSON-ответа
void handle_request(const char *buffer, char *response_json) {
    struct json_object *jobj = json_tokener_parse(buffer);
    struct json_object *resp = json_object_new_object();

    if (!jobj) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Неверный JSON"));
        strcpy(response_json, json_object_to_json_string(resp));
        json_object_put(resp);
        return;
    }

    const char *login = json_object_get_string(json_object_object_get(jobj, "login"));
    const char *cmd = json_object_get_string(json_object_object_get(jobj, "command"));

    if (!login || !cmd) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Ошибка полей"));
    } else if (!user_allowed(login)) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Доступ запрещен"));
    } else {
        // Создаём временный файл
        char tmp_template[] = "/tmp/myRPC_XXXXXX";
        int tmp_fd = mkstemp(tmp_template);
        if (tmp_fd < 0) {
            log_error("mkstemp failed: %s", strerror(errno));
            json_object_object_add(resp, "code", json_object_new_int(1));
            json_object_object_add(resp, "result", json_object_new_string("Неудачное создание файла"));
        } else {
            close(tmp_fd);
            char out_file[256], err_file[256];
            snprintf(out_file, sizeof(out_file), "%s.stdout", tmp_template);
            snprintf(err_file, sizeof(err_file), "%s.stderr", tmp_template);

            char *safe_cmd = shell_escape(cmd);
            if (!safe_cmd) {
                log_error("Не удалось выделить память для экранирования команды");
                json_object_object_add(resp, "code", json_object_new_int(1));
                json_object_object_add(resp, "result", json_object_new_string("Internal error"));
            } else {
                // Выполнение команды с перенаправлением вывода
                char command[1024];
                snprintf(command, sizeof(command), "sh -c %s > %s 2> %s", safe_cmd, out_file, err_file);
                free(safe_cmd);

                int ret = system(command);
                char *output = read_file(ret == 0 ? out_file : err_file);

                json_object_object_add(resp, "code", json_object_new_int(ret == 0 ? 0 : 1));
                json_object_object_add(resp, "result", json_object_new_string(output));
                free(output);
            }
        }
    }

    strcpy(response_json, json_object_to_json_string(resp));
    json_object_put(jobj);
    json_object_put(resp);
}

int main() {
    log_info("Запуск myRPC-server");

    // Значения по умолчанию
    int port = 1234;
    int use_stream = 1; // TCP по умолчанию

    // Чтение конфигурации
    FILE *conf = fopen("/etc/myRPC/myRPC.conf", "r");
    if (conf) {
        char line[256];
        while (fgets(line, sizeof(line), conf)) {
            line[strcspn(line, "\n")] = 0;
            char *trimmed = trim(line);
            if (trimmed[0] == '#' || strlen(trimmed) == 0) continue;

            if (strstr(trimmed, "port")) {
                sscanf(trimmed, "port = %d", &port);
            } else if (strstr(trimmed, "socket_type")) {
                char type[16];
                if (sscanf(trimmed, "socket_type = %15s", type) == 1) {
                    if (strcmp(type, "dgram") == 0) use_stream = 0;
                    else if (strcmp(type, "stream") == 0) use_stream = 1;
                }
            }
        }
        fclose(conf);
    } else {
        log_warning("Не удалось открыть конфигурационный файл: %s", strerror(errno));
    }

    log_info("Порт из конфига: %d", port);
    log_info("Тип сокета: %s", use_stream ? "stream" : "dgram");

    // Создание сокета
    int sockfd = socket(AF_INET, use_stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (sockfd < 0) {
        log_error("socket: %s", strerror(errno));
        exit(1);
    }

    // Привязка сокета
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_error("bind: %s", strerror(errno));
        exit(1);
    }

    // Прослушивание (только для TCP)
    if (use_stream && listen(sockfd, 5) < 0) {
        log_error("listen: %s", strerror(errno));
        exit(1);
    }

    log_info("Сервер запущен. Ожидание подключений...");

    // Основной цикл обработки подключений
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[BUFFER_SIZE] = {0};

        if (use_stream) {
            int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
            if (client_sock < 0) {
                log_error("accept: %s", strerror(errno));
                continue;
            }

            ssize_t recv_len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (recv_len <= 0) {
                log_error("recv: %s", strerror(errno));
                close(client_sock);
                continue;
            }

            buffer[recv_len] = '\0';
            log_info("Получено от TCP-клиента: %s", buffer);

            char response_json[BUFFER_SIZE];
            handle_request(buffer, response_json);
            send(client_sock, response_json, strlen(response_json), 0);

            close(client_sock);

        } else {
            // UDP режим
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                                        (struct sockaddr *)&client_addr, &client_len);
            if (recv_len < 0) {
                log_error("recvfrom: %s", strerror(errno));
                continue;
            }

            buffer[recv_len] = '\0';
            log_info("Получено от UDP-клиента: %s", buffer);

            char response_json[BUFFER_SIZE];
            handle_request(buffer, response_json);
            sendto(sockfd, response_json, strlen(response_json), 0,
                   (struct sockaddr *)&client_addr, client_len);
        }
    }

    close(sockfd);
    return 0;
}
