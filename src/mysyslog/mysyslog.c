#include "mysyslog.h"          // Заголовочный файл с определениями уровней логирования
#include <stdio.h>             // Стандартный ввод/вывод (fopen, fprintf)
#include <stdlib.h>            // Общие функции (в т.ч. exit — на всякий случай)
#include <stdarg.h>            // Для работы с переменным числом аргументов (va_list)
#include <time.h>              // Для получения и форматирования текущего времени

// Функция возвращает строковое представление уровня логирования
const char *level_str(int level) {
    switch (level) {
        case LOG_INFO: return "INFO";         // Информационные сообщения
        case LOG_WARNING: return "WARNING";   // Предупреждения
        case LOG_ERR: return "ERROR";         // Ошибки
        default: return "UNKNOWN";            // Неизвестный уровень (на всякий случай)
    }
}

// Основная функция логирования сообщений в файл
// Поддерживает printf-подобный синтаксис через переменные аргументы
void mysyslog(int level, const char *format, ...) {
    // Открытие файла для дозаписи (append mode)
    FILE *fp = fopen("/var/log/myrpc.log", "a");
    if (!fp) return; // Если не удалось открыть файл — логгирование невозможно

    // Получение текущего времени
    time_t now = time(NULL);               // Время в формате time_t (кол-во секунд с эпохи Unix)
    struct tm *t = localtime(&now);        // Преобразование во временную структуру по локальному времени

    // Запись метки времени и уровня логирования
    fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
        t->tm_year + 1900,                 // Год (tm_year — количество лет с 1900)
        t->tm_mon + 1,                     // Месяц (от 0 до 11, поэтому +1)
        t->tm_mday,                        // День месяца
        t->tm_hour,                        // Час
        t->tm_min,                         // Минута
        t->tm_sec,                         // Секунда
        level_str(level)                   // Строка с названием уровня логирования
    );

    // Обработка переменного числа аргументов
    va_list args;
    va_start(args, format);               // Инициализация переменных аргументов
    vfprintf(fp, format, args);           // Вывод форматированной строки в файл
    va_end(args);                         // Завершение обработки аргументов

    // Завершаем запись переносом строки
    fprintf(fp, "\n");

    // Закрываем файл
    fclose(fp);
}
