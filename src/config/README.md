# Конфигурация и установка демона myRPC-server

Этот модуль описывает, как правильно установить и настроить `myRPC-server` для работы в системе на базе systemd.

---

## 🧾 Конфигурационные файлы

Для корректной работы сервера необходимо наличие двух конфигурационных файлов:

- `/etc/myRPC/myRPC.conf` — основной конфиг с параметрами подключения
- `/etc/myRPC/users.conf` — список разрешённых пользователей

### Пример `myRPC.conf`:

```ini
port = 4040
socket_type = stream
```

### Пример `users.conf`:

```text
user1
admin
tester
```

> Убедитесь, что папка `/etc/myRPC/` существует и содержит оба файла. Если нет — создайте её вручную:

```bash
sudo mkdir -p /etc/myRPC
sudo cp -f src/config/myRPC.conf /etc/myRPC/
sudo cp -f src/config/users.conf /etc/myRPC/
```

---

## ⚙️ Установка демона

### Файл systemd: `myRPC-server.service`

Файл активации systemd-сервиса должен быть размещён в:

```
/lib/systemd/system/myRPC-server.service
```

### Пример команды для копирования:

```bash
sudo cp -f myRPC/config/myRPC-server.service /lib/systemd/system/myRPC-server.service
```

---

## 🚀 Установка бинарного файла

После сборки проекта необходимо скопировать исполняемый файл сервера:

```bash
sudo cp -f myRPC/bin/myRPC-server /usr/local/bin/myRPC-server
```

---

## 🟢 Активация сервиса

Перезагрузите systemd и запустите сервер:

```bash
sudo systemctl daemon-reload
sudo systemctl enable myRPC-server
sudo systemctl start myRPC-server
```

Проверка статуса:

```bash
systemctl status myRPC-server
```

---

## 📌 Важно

- Убедитесь, что конфиги размещены в `/etc/myRPC/`
- Если конфигурация была изменена — не забудьте перезапустить сервис:

```bash
sudo systemctl restart myRPC-server
```

---

> Эти шаги необходимы для корректного запуска и стабильной работы `myRPC-server` как фонового демона на Linux-системе.
