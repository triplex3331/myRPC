#define _GNU_SOURCE                         // Расширения GNU для функций, например, mkstemp
#include <stdio.h>                          // Стандартный ввод/вывод
#include <stdlib.h>                         // Стандартные функции: malloc, free, exit
#include <errno.h>                          // Переменная errno и функции для работы с ошибками
#include <string.h>                         // Строковые функции: strlen, strcmp, etc.
#include <unistd.h>                         // POSIX-функции: close, read, write, access
#include <sys/socket.h>                     // Работа с сокетами
#include <netinet/in.h>                     // Структуры и константы для работы с сетями (IPv4)
#include <arpa/inet.h>                      // inet_pton и прочие преобразования IP
#include <fcntl.h>                          // Флаги открытия файлов
#include <sys/types.h>                      // Общие системные типы
#include <json-c/json.h>                    // Работа с JSON
#include <signal.h>                         // Работа с сигналами (на будущее)
#include <sys/stat.h>                       // Работа с правами доступа к файлам
#include "mysyslog.h"                       // Локальный логгер

#define BUFFER_SIZE 4096                    // Размер буфера для сетевого приёма и передачи

// Проверяет, разрешён ли пользователь (присутствует ли в users.conf)
int user_allowed(const char *username) {
    FILE *fp = fopen("/etc/myRPC/users.conf", "r"); // Открытие файла разрешённых пользователей
    if (!fp) return 0;                               // Если файл не открылся — отказ

    char line[128];
    while (fgets(line, sizeof(line), fp)) {         // Читаем файл построчно
        line[strcspn(line, "\n")] = 0;             // Удаляем символ новой строки
        if (strcmp(line, username) == 0) {          // Сравнение логина
            fclose(fp);
            return 1;                               // Разрешён
        }
    }
    fclose(fp);
    return 0;                                       // Не найден
}

// Удаление пробелов и табуляции по краям строки
char *trim(char *str) {
    while (*str == ' ' || *str == '\t') str++;     // Убираем ведущие пробелы
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) *end-- = '\0'; // Убираем замыкающие пробелы
    return str;
}

// Экранирует строку для безопасного исполнения через shell
char *shell_escape(const char *input) {
    size_t len = strlen(input);                     // Длина входной строки
    char *escaped = malloc(len * 4 + 3);             // Память под экранированную строку
    if (!escaped) return NULL;                      // Ошибка выделения памяти

    char *p = escaped;
    *p++ = ''';                                    // Начинаем строку с кавычки
    for (size_t i = 0; i < len; ++i) {
        if (input[i] == ''') {                     // Экранируем одиночные кавычки
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = input[i];
        }
    }
    *p++ = ''';                                    // Завершаем кавычкой
    *p = '\0';                                      // Завершаем строку
    return escaped;
}

// Чтение файла и возврат его содержимого как строки
char *read_file(const char *filename) {
    FILE *fp = fopen(filename, "r");               // Открываем файл
    if (!fp) return strdup("(empty)");             // Если ошибка — вернуть (empty)

    fseek(fp, 0, SEEK_END);                         // В конец файла, чтобы узнать размер
    long size = ftell(fp);                          // Получаем размер
    fseek(fp, 0, SEEK_SET);                         // Обратно в начало

    char *content = malloc(size + 1);               // Память под содержимое
    if (!content) return strdup("(allocation error)");
    fread(content, 1, size, fp);                    // Чтение
    content[size] = '\0';                          // Завершение строки
    fclose(fp);                                     // Закрытие файла
    return content;                                 // Возврат строки
}

// Обработка JSON-запроса клиента и формирование JSON-ответа
void handle_request(const char *buffer, char *response_json) {
    struct json_object *jobj = json_tokener_parse(buffer); // Разбор JSON-запроса
    struct json_object *resp = json_object_new_object();   // Новый JSON-объект ответа

    if (!jobj) {
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Неверный JSON"));
        strcpy(response_json, json_object_to_json_string(resp));
        json_object_put(resp);
        return;
    }

    const char *login = json_object_get_string(json_object_object_get(jobj, "login"));
    const char *cmd = json_object_get_string(json_object_object_get(jobj, "command"));

    if (!login || !cmd) {                           // Если поля отсутствуют
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Ошибка полей"));
    } else if (!user_allowed(login)) {              // Проверка разрешения пользователя
        json_object_object_add(resp, "code", json_object_new_int(1));
        json_object_object_add(resp, "result", json_object_new_string("Доступ запрещен"));
    } else {
        char tmp_template[] = "/tmp/myRPC_XXXXXX"; // Создание временного файла
        int tmp_fd = mkstemp(tmp_template);
        if (tmp_fd < 0) {
            log_error("mkstemp failed: %s", strerror(errno));
            json_object_object_add(resp, "code", json_object_new_int(1));
            json_object_object_add(resp, "result", json_object_new_string("Неудачное создание файла"));
        } else {
            close(tmp_fd);                          // Закрываем дескриптор — файл будем использовать по имени
            char out_file[256], err_file[256];
            snprintf(out_file, sizeof(out_file), "%s.stdout", tmp_template);
            snprintf(err_file, sizeof(err_file), "%s.stderr", tmp_template);

            char *safe_cmd = shell_escape(cmd);     // Экранируем команду
            if (!safe_cmd) {
                log_error("Не удалось выделить память для экранирования команды");
                json_object_object_add(resp, "code", json_object_new_int(1));
                json_object_object_add(resp, "result", json_object_new_string("Internal error"));
            } else {
                char command[1024];
                snprintf(command, sizeof(command), "sh -c %s > %s 2> %s", safe_cmd, out_file, err_file);
                free(safe_cmd);

                int ret = system(command);           // Выполнение команды
                char *output = read_file(ret == 0 ? out_file : err_file); // Читаем результат

                json_object_object_add(resp, "code", json_object_new_int(ret == 0 ? 0 : 1));
                json_object_object_add(resp, "result", json_object_new_string(output));
                free(output);
            }
        }
    }

    strcpy(response_json, json_object_to_json_string(resp)); // Записываем JSON-ответ
    json_object_put(jobj);                                   // Освобождаем ресурсы
    json_object_put(resp);
}

// Точка входа — запуск сервера
int main() {
    log_info("Запуск myRPC-server");

    int port = 1234;               // Значение порта по умолчанию
    int use_stream = 1;            // Использовать TCP по умолчанию

    FILE *conf = fopen("/etc/myRPC/myRPC.conf", "r");
    if (conf) {
        char line[256];
        while (fgets(line, sizeof(line), conf)) {
            line[strcspn(line, "\n")] = 0;
            char *trimmed = trim(line);
            if (trimmed[0] == '#' || strlen(trimmed) == 0) continue; // Пропуск комментариев

            if (strstr(trimmed, "port")) {
                sscanf(trimmed, "port = %d", &port);                // Считываем порт
            } else if (strstr(trimmed, "socket_type")) {
                char type[16];
                if (sscanf(trimmed, "socket_type = %15s", type) == 1) {
                    if (strcmp(type, "dgram") == 0) use_stream = 0; // UDP
                    else if (strcmp(type, "stream") == 0) use_stream = 1; // TCP
                }
            }
        }
        fclose(conf);
    } else {
        log_warning("Не удалось открыть конфигурационный файл: %s", strerror(errno));
    }

    log_info("Порт из конфига: %d", port);
    log_info("Тип сокета: %s", use_stream ? "stream" : "dgram");

    int sockfd = socket(AF_INET, use_stream ? SOCK_STREAM : SOCK_DGRAM, 0); // Создаём сокет TCP или UDP
    if (sockfd < 0) {
        log_error("socket: %s", strerror(errno));
        exit(1);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY // Привязка к любому локальному интерфейсу
    };

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_error("bind: %s", strerror(errno));
        exit(1);
    }

    if (use_stream && listen(sockfd, 5) < 0) { // Только для TCP
        log_error("listen: %s", strerror(errno));
        exit(1);
    }

    log_info("Сервер запущен. Ожидание подключений...");

    while (1) { // Основной цикл сервера
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[BUFFER_SIZE] = {0};

        if (use_stream) {
            int client_sock = accept(sockfd, (struct sockaddr *)&client_addr, &client_len); // TCP-подключение
            if (client_sock < 0) {
                log_error("accept: %s", strerror(errno));
                continue;
            }

            ssize_t recv_len = recv(client_sock, buffer, sizeof(buffer) - 1, 0); // Приём данных
            if (recv_len <= 0) {
                log_error("recv: %s", strerror(errno));
                close(client_sock);
                continue;
            }

            buffer[recv_len] = '\0'; // Завершаем строку
            log_info("Получено от TCP-клиента: %s", buffer);

            char response_json[BUFFER_SIZE];
            handle_request(buffer, response_json); // Обработка запроса
            send(client_sock, response_json, strlen(response_json), 0); // Ответ клиенту

            close(client_sock); // Закрытие соединения

        } else {
            ssize_t recv_len = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                                        (struct sockaddr *)&client_addr, &client_len); // UDP-приём
            if (recv_len < 0) {
                log_error("recvfrom: %s", strerror(errno));
                continue;
            }

            buffer[recv_len] = '\0';
            log_info("Получено от UDP-клиента: %s", buffer);

            char response_json[BUFFER_SIZE];
            handle_request(buffer, response_json); // Обработка
            sendto(sockfd, response_json, strlen(response_json), 0,
                   (struct sockaddr *)&client_addr, client_len); // Ответ клиенту
        }
    }

    close(sockfd); // Закрываем сокет сервера
    return 0;
}
