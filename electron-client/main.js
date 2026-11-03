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
let pendingAuthPromise = null; // Промис для ожидания ответа login_result / register_result
let currentCredentials = null; // Сохраненные данные для цепочки register -> login

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
 * Подключение к C++ серверу по TCP и отправка запроса входа или регистрации
 */
function connectTcp({ host, port, username, password, action = 'login' }) {
  return new Promise((resolve, reject) => {
    disconnectTcp();

    socketBuffer = '';
    tcpSocket = new net.Socket();
    currentCredentials = { username, password };

    let isConnected = false;

    // Таймер на случай зависания соединения
    const timeout = setTimeout(() => {
      if (!isConnected) {
        tcpSocket.destroy();
        reject(new Error('Превышено время ожидания подключения к серверу'));
      }
    }, 5000);

    tcpSocket.connect(Number(port), host, () => {
      isConnected = true;
      clearTimeout(timeout);

      // В сервере задан LOGIN_TIMEOUT = 1000мс,
      // поэтому запрос отправляем сразу же после установки TCP-соединения!
      pendingAuthPromise = { resolve, reject };

      if (action === 'register') {
        const regPayload = JSON.stringify({
          type: 'register',
          user: username,
          password: password || '',
        }) + '\n';
        tcpSocket.write(regPayload, 'utf-8');
      } else {
        const loginPayload = JSON.stringify({
          type: 'login',
          user: username,
          password: password || '',
        }) + '\n';
        tcpSocket.write(loginPayload, 'utf-8');
      }
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
      if (pendingAuthPromise) {
        pendingAuthPromise.reject(new Error(`Ошибка подключения: ${err.message}`));
        pendingAuthPromise = null;
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
      if (pendingAuthPromise) {
        pendingAuthPromise.reject(new Error('Сервер разорвал соединение до завершения авторизации'));
        pendingAuthPromise = null;
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
  // Если мы ожидали подтверждения регистрации:
  if (msg.type === 'register_result' && pendingAuthPromise) {
    if (msg.ok) {
      // Регистрация успешна! Сразу же отправляем логин на этом же сокете
      const loginPayload = JSON.stringify({
        type: 'login',
        user: currentCredentials.username,
        password: currentCredentials.password || '',
      }) + '\n';
      tcpSocket.write(loginPayload, 'utf-8');
    } else {
      pendingAuthPromise.reject(new Error(msg.reason || 'Ошибка регистрации'));
      pendingAuthPromise = null;
      disconnectTcp();
    }
    return;
  }

  // Если мы ожидали подтверждения входа:
  if (msg.type === 'login_result' && pendingAuthPromise) {
    if (msg.ok) {
      pendingAuthPromise.resolve(msg);
    } else {
      pendingAuthPromise.reject(new Error(msg.reason || 'Ошибка авторизации'));
      disconnectTcp();
    }
    pendingAuthPromise = null;
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

// Запрос на подключение: invoke('tcp:connect', { host, port, username, password, action })
ipcMain.handle('tcp:connect', async (_event, config) => {
  return await connectTcp(config);
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
