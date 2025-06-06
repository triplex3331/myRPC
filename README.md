# myRPC UNIX — система удалённого выполнения команд

Проект **myRPC** включает в себя демон `myRPC-server`, клиентскую утилиту `myRPC-client`, собственную библиотеку логирования `mysyslog`, а также набор Bash-скриптов для сборки `.deb`-пакетов и формирования локального репозитория. Клиент может подключиться к репозиторию, установить нужные пакеты и взаимодействовать с сервером.

---

## 🧩 Компоненты проекта

### myRPC-client — клиентская утилита

Позволяет указать параметры подключения (IP, порт, тип сокета) и отправить команду серверу. В случае ошибки выводится сообщение. При запуске без аргументов — справка.

### myRPC-server — серверная часть

Читает конфигурацию из `/etc/myRPC/myRPC.conf` (порт, тип сокета), принимает команды, извлекает имя пользователя, проверяет его в `/etc/myRPC/users.conf`, выполняет команду и логирует результат через `mysyslog`.

### mysyslog — библиотека логирования

Пишет журналы выполнения:

- Успешно: `/tmp/myRPC_XXXXXX.stdout`
- Ошибки: `/tmp/myRPC_XXXXXX.stderr`

---

## ⚙️ Настройка

### `myRPC.conf`

```ini
port = 4040
socket_type = stream
```

Если файл отсутствует, используются значения по умолчанию: порт `25565`, тип сокета `stream`.

После настройки:

```bash
sudo mkdir -p /etc/myRPC
sudo cp -f src/config/myRPC.conf /etc/myRPC/
```

### `users.conf`

```text
username1
username2
...
```

Если имя пользователя отсутствует в этом списке — сервер отклонит запрос.

```bash
sudo cp -f src/config/users.conf /etc/myRPC/
```

---

## 🔧 Сборка и установка

### Требования

- `gcc`
- `make`
- `fakeroot`
- `python3`
- `git`

### Инструкция

```bash
# Клонируем репозиторий
git clone <URL>

# Переходим в директорию
cd myRPC

# Сборка бинарников
make all

# Сборка .deb пакетов
make deb

# Создание локального репозитория
make repo

# Общая сборка с подключением библиотеки mysyslog
make repository
```

---

## 🌐 Сетевой deb-репозиторий

Для запуска локального HTTP-сервера:

```bash
cd repo
python3 -m http.server 8000
```

На клиенте:

```bash
echo "deb [trusted=yes] http://<IP-сервера>:8000 ./" | sudo tee /etc/apt/sources.list.d/myrpc.list
sudo apt update
sudo apt install myRPC-client myRPC-server mysyslog
```

Также доступно вручную:

```
http://localhost:8000/Packages.gz
```

---

## 🚀 Запуск

### Сервер

Проверка и управление через `systemd`:

```bash
systemctl status myRPC-server
systemctl daemon-reload
systemctl restart myRPC-server
```

После изменения конфигурации:

```bash
sudo systemctl restart myRPC-server
```

### Клиент

Запуск из папки `bin`:

```bash
./bin/myRPC-client -c "<команда>" -h <IP> -p <порт> -d
```

Или, если установлен .deb:

```bash
myRPC-client -c "<команда>" -h <IP> -p <порт> -d
```

#### Аргументы:

- `-c` — команда для выполнения
- `-h` — адрес сервера
- `-p` — порт
- `-d` — использовать UDP
- `-s` — использовать TCP (по умолчанию)

#### Пример:

```bash
myRPC-client -c "echo Hello from client" -h 127.0.0.1 -p 4040 -d
```

---

## 🧼 Очистка

Удаление временных файлов:

```bash
make clean
```

---

## 📦 Дополнительно

- Гибкая конфигурация через `.conf`-файлы.
- Репозиторий может использоваться и для сторонних .deb пакетов.

---

> Разработано для UNIX-систем. Идеально для обучения и тестирования механизмов клиент-серверного взаимодействия на C.
