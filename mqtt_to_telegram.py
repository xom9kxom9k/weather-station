import json
import logging
import asyncio
from datetime import datetime
import paho.mqtt.client as mqtt
from aiogram import Bot, Dispatcher, types
from aiogram.filters import Command


logging.basicConfig(
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    level=logging.INFO
)
logger = logging.getLogger(__name__)

from config import TELEGRAM_BOT_TOKEN, TELEGRAM_CHAT_ID

MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC = "weather/data"

latest_weather_data = {
    "temperature": None,
    "pressure": None,
    "humidity": None,
    "timestamp": None
}

# Создаем объекты бота и диспетчера
bot = Bot(token=TELEGRAM_BOT_TOKEN)
dp = Dispatcher()

# --- MQTT Callbacks ---
def on_connect(client, userdata, flags, rc):
    """Вызывается при подключении к MQTT брокеру"""
    if rc == 0:
        logger.info("Подключено к MQTT брокеру")
        client.subscribe(MQTT_TOPIC)
        logger.info(f"Подписка на топик: {MQTT_TOPIC}")
    else:
        logger.error(f"Ошибка подключения к MQTT, код: {rc}")

def on_message(client, userdata, msg):
    """Вызывается при получении сообщения из MQTT"""
    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)
        
        # Обновляем глобальные данные
        latest_weather_data["temperature"] = data.get("temperature")
        latest_weather_data["pressure"] = data.get("pressure")
        latest_weather_data["humidity"] = data.get("humidity")
        latest_weather_data["timestamp"] = datetime.now()
        
        logger.info(f"Получены данные: T={data.get('temperature')}°C, "
                   f"P={data.get('pressure')} hPa, H={data.get('humidity')}%")
    except Exception as e:
        logger.error(f"Ошибка обработки MQTT сообщения: {e}")

# --- Telegram Command Handlers ---
@dp.message(Command("start"))
async def start_command(message: types.Message):
    """Обработчик команды /start"""
    await message.answer(
        "🌤 Бот метеостанции запущен!\n\n"
        "Доступные команды:\n"
        "/climate - Получить текущие показания датчиков"
    )

@dp.message(Command("climate"))
async def climate_command(message: types.Message):
    """Обработчик команды /climate"""
    if latest_weather_data["temperature"] is None:
        await message.answer(
            "⚠️ Данные ещё не получены. Подождите немного..."
        )
        return
    
    # Форматируем сообщение
    timestamp = latest_weather_data["timestamp"].strftime("%H:%M:%S")
    text = (
        f"🌡 <b>Климатические данные</b>\n\n"
        f"🌡️ Температура: <b>{latest_weather_data['temperature']:.2f} °C</b>\n"
        f"💨 Давление: <b>{latest_weather_data['pressure']:.2f} hPa</b>\n"
        f"💧 Влажность: <b>{latest_weather_data['humidity']:.2f} %</b>\n\n"
        f"🕐 Обновлено: {timestamp}"
    )
    
    await message.answer(text, parse_mode='HTML')

# --- Main Function ---
async def main():
    """Главная функция запуска бота"""
    logger.info("Запуск бота...")
    
    # Настройка MQTT клиента
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        mqtt_client.loop_start()  # Запуск MQTT в фоновом режиме
        logger.info("MQTT клиент запущен")
    except Exception as e:
        logger.error(f"Ошибка подключения к MQTT брокеру: {e}")
        return
    
    # Запуск бота
    logger.info("Telegram бот запущен. Нажмите Ctrl+C для остановки.")
    try:
        await dp.start_polling(bot)
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        await bot.session.close()

if __name__ == "__main__":
    asyncio.run(main())
