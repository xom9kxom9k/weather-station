#ifndef WEB_PAGE_H
#define WEB_PAGE_H

static const char HTML_PAGE[] = R"(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Метеостанция BME280</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 40px;
            max-width: 800px;
            width: 100%;
        }
        h1 {
            text-align: center;
            color: #333;
            margin-bottom: 30px;
            font-size: 2.5em;
        }
        .sensors {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .sensor-card {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 25px;
            border-radius: 15px;
            text-align: center;
            transition: transform 0.3s, box-shadow 0.3s;
        }
        .sensor-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }
        .sensor-label {
            font-size: 1.1em;
            opacity: 0.9;
            margin-bottom: 10px;
        }
        .sensor-value {
            font-size: 2.5em;
            font-weight: bold;
            margin: 10px 0;
        }
        .sensor-unit {
            font-size: 1.2em;
            opacity: 0.8;
        }
        .update-time {
            text-align: center;
            color: #666;
            font-size: 0.9em;
            margin-top: 20px;
        }
        .loading {
            text-align: center;
            color: #666;
            padding: 20px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌡️ Метеостанция BME280</h1>
        <div id="content">
            <div class="loading">Загрузка данных...</div>
        </div>
        <div class="update-time" id="updateTime"></div>
    </div>
    <script>
        function updateData() {
            fetch('/api/data')
                .then(response => response.json())
                .then(data => {
                    const content = document.getElementById('content');
                    content.innerHTML = `
                        <div class="sensors">
                            <div class="sensor-card">
                                <div class="sensor-label">Температура</div>
                                <div class="sensor-value">${data.temperature.toFixed(1)}</div>
                                <div class="sensor-unit">°C</div>
                            </div>
                            <div class="sensor-card">
                                <div class="sensor-label">Давление</div>
                                <div class="sensor-value">${data.pressure.toFixed(1)}</div>
                                <div class="sensor-unit">hPa</div>
                            </div>
                            <div class="sensor-card">
                                <div class="sensor-label">Влажность</div>
                                <div class="sensor-value">${data.humidity.toFixed(1)}</div>
                                <div class="sensor-unit">%</div>
                            </div>
                        </div>
                    `;
                    const updateTime = document.getElementById('updateTime');
                    updateTime.textContent = 'Обновлено: ' + new Date().toLocaleString('ru-RU');
                })
                .catch(error => {
                    console.error('Error:', error);
                    document.getElementById('content').innerHTML = 
                        '<div class="loading">Ошибка загрузки данных</div>';
                });
        }
        updateData();
        setInterval(updateData, 2000);
    </script>
</body>
</html>
)";

#endif