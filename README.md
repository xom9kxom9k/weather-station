# Weather Station 🌡️

Проект метеостанции на ESP32 с датчиком BME280. Измеряет температуру, давление и влажность, отправляет данные на MQTT брокер и в Telegram бота.

## Возможности

- **Веб-интерфейс** — просмотр данных через HTTP сервер на ESP32
- **MQTT** — публикация данных на Mosquitto брокер каждые 2 секунды
- **Telegram бот** — получение данных по команде `/climate`

## Архитектура

```
BME280 → ESP32 → MQTT Broker (Mosquitto) → Python Script → Telegram Bot
                    ↓
              HTTP Server (порт 80)
```

## Требования

### Hardware
- ESP32
- Датчик BME280 (I2C, адрес 0x76)

### Software
- ESP-IDF v5.5
- Python 3.8+
- Mosquitto MQTT Broker

## Установка

### 1. Настройка ESP32

```bash
# Скопируйте конфигурацию WiFi
cp main/wifi_config.h.example main/wifi_config.h

# Отредактируйте SSID и пароль WiFi
nano main/wifi_config.h

# Укажите IP адрес MQTT брокера в main/main.c
# #define MQTT_BROKER_URI "mqtt://YOUR_BROKER_IP"

# Соберите и прошейте
idf.py build
idf.py -p /dev/tty.usbserial-XX flash monitor
```

### 2. Настройка Mosquitto (MQTT Broker)

Установите Mosquitto:
```bash
# macOS
brew install mosquitto
brew services start mosquitto

# Ubuntu/Debian
sudo apt install mosquitto mosquitto-clients
```

Настройте `/opt/homebrew/etc/mosquitto/mosquitto.conf`:
```conf
listener 1883 0.0.0.0
allow_anonymous true
```

Перезапустите: `brew services restart mosquitto`

### 3. Настройка Telegram бота

```bash
# Установите зависимости Python
pip install -r requirements.txt

# Скопируйте конфигурацию
cp config.py.example config.py

# Отредактируйте токен бота и Chat ID
nano config.py

# Запустите бота
python mqtt_to_telegram.py
```

## Использование

### Веб-интерфейс
Откройте в браузере IP-адрес ESP32 (отображается в логах при подключении).

### Telegram
1. Найдите вашего бота в Telegram
2. Отправьте `/start`
3. Отправьте `/climate` для получения текущих данных

### MQTT
```bash
# Подписка на данные
mosquitto_sub -h localhost -t "weather/data"
```

## Структура проекта

```
weather-station/
├── main/
│   ├── main.c              # Основной код ESP32
│   ├── web_page.h          # HTML страница
│   ├── wifi_config.h       # Конфигурация WiFi (не в git)
│   └── wifi_config.h.example
├── mqtt_to_telegram.py     # Telegram бот
├── config.py               # Токены бота (не в git)
├── config.py.example
├── requirements.txt        # Зависимости Python
└── README.md
```

## Лицензия

MIT
