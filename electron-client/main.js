/**
 * main.js - Главный процесс Electron
 *
 * Отвечает за:
 * 1. Создание и управление графическим окном приложения (BrowserWindow).
 * 2. Управление низкоуровневым TCP-сокетом (node:net) для общения с C++ сервером.
 * 3. Буферизацию входящего сетевого потока и парсинг JSON, разделенного '\n'.
 * 4. Безопасную передачу сообщений между сетью и интерфейсом через IPC (Inter-Process Communication).
 */

const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('node:path');
const net = require('node:net');

let mainWindow = null;
let tcpSocket = null;
let socketBuffer = ''; // Буфер для накопления кусочков TCP-пакетов
let pendingLoginPromise = null; // Промис для ожидания ответа login_result от сервера

/**
 * Создание главного окна приложения
 */
function createWindow() {
  mainWindow = new BrowserWindow({
    width: 960,
    height: 680,
    minWidth: 720,
    minHeight: 520,
    backgroundColor: '#1e1e2e', // Тёмный фон Catppuccin Mocha
    title: 'Wiretalk Messenger',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true, // Изоляция контекста для безопасности
      nodeIntegration: false,
    },
  });

  mainWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));

  mainWindow.on('closed', () => {
    mainWindow = null;
    disconnectTcp();
  });
}

/**
 * Подключение к C++ серверу по TCP и отправка логина
 */
function connectTcp(host, port, username) {
  return new Promise((resolve, reject) => {
    disconnectTcp();

    socketBuffer = '';
    tcpSocket = new net.Socket();

    let isConnected = false;

    // Таймер на случай зависания соединения
    const timeout = setTimeout(() => {
      if (!isConnected) {
        tcpSocket.destroy();
        reject(new Error('Превышено время ожидания подключения к серверу'));
      }
    }, 5000);

    tcpSocket.connect(port, host, () => {
      isConnected = true;
      clearTimeout(timeout);

      // В сервере задан LOGIN_TIMEOUT = 1000мс,
      // поэтому запрос login отправляем сразу же после установки TCP-соединения!
      const loginPayload = JSON.stringify({ type: 'login', user: username }) + '\n';
      tcpSocket.write(loginPayload, 'utf-8');

      // Сохраняем обработчики промиса для ответа 'login_result'
      pendingLoginPromise = { resolve, reject };
    });

    // Обработка входящих данных
    tcpSocket.on('data', (chunk) => {
      socketBuffer += chunk.toString('utf-8');

      // Протокол сервера использует разделитель строк '\n'
      let newlineIndex;
      while ((newlineIndex = socketBuffer.indexOf('\n')) !== -1) {
        const line = socketBuffer.substring(0, newlineIndex).trim();
        socketBuffer = socketBuffer.substring(newlineIndex + 1);

        if (!line) continue;

        try {
          const message = JSON.parse(line);
          handleIncomingMessage(message);
        } catch (err) {
          console.error('Ошибка парсинга JSON от сервера:', line, err);
        }
      }
    });

    tcpSocket.on('error', (err) => {
      console.error('Ошибка TCP сокета:', err.message);
      if (pendingLoginPromise) {
        pendingLoginPromise.reject(new Error(`Ошибка подключения: ${err.message}`));
        pendingLoginPromise = null;
      }
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('connection-status', {
          status: 'error',
          error: err.message,
        });
      }
    });

    tcpSocket.on('close', () => {
      console.log('TCP сокет закрыт');
      if (pendingLoginPromise) {
        pendingLoginPromise.reject(new Error('Сервер разорвал соединение до завершения входа'));
        pendingLoginPromise = null;
      }
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('connection-status', {
          status: 'disconnected',
        });
      }
      tcpSocket = null;
    });
  });
}

/**
 * Обработка разобранного JSON-сообщения от сервера
 */
function handleIncomingMessage(msg) {
  // Если мы ожидали подтверждения входа
  if (msg.type === 'login_result' && pendingLoginPromise) {
    if (msg.ok) {
      pendingLoginPromise.resolve(msg);
    } else {
      pendingLoginPromise.reject(new Error(msg.reason || 'Ошибка авторизации'));
      disconnectTcp();
    }
    pendingLoginPromise = null;
    return;
  }

  // Пересылаем сообщение во фронтенд (renderer)
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send('server-message', msg);
  }
}

/**
 * Отправка произвольного JSON-сообщения на сервер
 */
function sendTcp(obj) {
  if (tcpSocket && !tcpSocket.destroyed) {
    const raw = JSON.stringify(obj) + '\n';
    tcpSocket.write(raw, 'utf-8');
    return true;
  }
  return false;
}

/**
 * Разрыв соединения
 */
function disconnectTcp() {
  if (tcpSocket) {
    tcpSocket.destroy();
    tcpSocket = null;
  }
  socketBuffer = '';
}

// -------------------------------------------------------------
// IPC обработчики (вызовы из renderer интерфейса)
// -------------------------------------------------------------

// Запрос на подключение: invoke('tcp:connect', { host, port, username })
ipcMain.handle('tcp:connect', async (_event, { host, port, username }) => {
  return await connectTcp(host, Number(port), username);
});

// Отправка сообщения: send('tcp:send', messageObj)
ipcMain.on('tcp:send', (_event, msg) => {
  sendTcp(msg);
});

// Отключение: send('tcp:disconnect')
ipcMain.on('tcp:disconnect', () => {
  disconnectTcp();
});

// -------------------------------------------------------------
// Жизненный цикл приложения Electron
// -------------------------------------------------------------

app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
