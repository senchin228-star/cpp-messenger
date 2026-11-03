/**
 * app.js - Клиентская логика интерфейса мессенджера
 *
 * Код написан максимально понятно и структурированно:
 * 1. Состояние приложения (State)
 * 2. Инициализация и обработчики событий интерфейса
 * 3. Сетевые обработчики (прием сообщений от сервера)
 * 4. Функции рендеринга (отрисовка сообщений, комнат, пользователей)
 */

// ============================================================
// 1. СОСТОЯНИЕ ПРИЛОЖЕНИЯ (STATE)
// ============================================================

const state = {
  myUsername: '', // Никнейм текущего пользователя
  serverHost: '127.0.0.1',
  serverPort: 5050,

  // Активный выбранный чат:
  // { type: 'room', id: 'general' } или { type: 'private', id: 'dima' }
  activeChat: {
    type: 'room',
    id: 'general',
  },

  // Набор комнат, в которых состоит пользователь
  rooms: new Set(['general']),

  // Набор пользователей, с которыми открыты личные диалоги
  privateUsers: new Set(),

  // Список пользователей онлайн
  onlineUsers: new Set(),

  // Хранилище сообщений текущей сессии в оперативной памяти:
  // Ключ: "room:general" или "private:dima"
  // Значение: массив объектов сообщений { id, sender, text, image, time, type: 'text'|'system'|'error', isMine: bool }
  messages: new Map(),

  // Счётчики непрочитанных сообщений:
  // Ключ: "room:name" или "private:name" -> число
  unreads: new Map(),

  // Текущий режим формы входа: 'login' | 'register'
  authMode: 'login',

  // Текущее прикреплённое изображение:
  // { dataUrl: string, name: string, width: number, height: number, size: number } | null
  attachedImage: null,
};

// ============================================================
// 2. DOM-ЭЛЕМЕНТЫ (Ссылки на элементы интерфейса)
// ============================================================

const dom = {
  // Экраны
  loginScreen: document.getElementById('loginScreen'),
  chatScreen: document.getElementById('chatScreen'),

  // Форма входа и регистрации
  loginForm: document.getElementById('loginForm'),
  tabLogin: document.getElementById('tabLogin'),
  tabRegister: document.getElementById('tabRegister'),
  authToggleLink: document.getElementById('authToggleLink'),
  inputUsername: document.getElementById('inputUsername'),
  inputPassword: document.getElementById('inputPassword'),
  btnTogglePassword: document.getElementById('btnTogglePassword'),
  inputHost: document.getElementById('inputHost'),
  inputPort: document.getElementById('inputPort'),
  btnLogin: document.getElementById('btnLogin'),
  loginError: document.getElementById('loginError'),

  // Профиль
  myAvatar: document.getElementById('myAvatar'),
  myNickname: document.getElementById('myNickname'),
  btnDisconnect: document.getElementById('btnDisconnect'),

  // Списки в боковом меню
  roomsList: document.getElementById('roomsList'),
  privateList: document.getElementById('privateList'),
  onlineUsersList: document.getElementById('onlineUsersList'),
  onlineCount: document.getElementById('onlineCount'),

  // Модальные окна
  btnOpenJoinModal: document.getElementById('btnOpenJoinModal'),
  modalJoinRoom: document.getElementById('modalJoinRoom'),
  formJoinRoom: document.getElementById('formJoinRoom'),
  inputRoomName: document.getElementById('inputRoomName'),
  btnCloseJoinModal: document.getElementById('btnCloseJoinModal'),

  btnOpenPrivateModal: document.getElementById('btnOpenPrivateModal'),
  modalPrivate: document.getElementById('modalPrivate'),
  formPrivate: document.getElementById('formPrivate'),
  inputPrivateRecipient: document.getElementById('inputPrivateRecipient'),
  btnClosePrivateModal: document.getElementById('btnClosePrivateModal'),

  // Шапка чата
  currentChatIcon: document.getElementById('currentChatIcon'),
  currentChatTitle: document.getElementById('currentChatTitle'),
  currentChatSubtitle: document.getElementById('currentChatSubtitle'),
  btnLeaveRoom: document.getElementById('btnLeaveRoom'),

  // Лента и ввод
  messagesContainer: document.getElementById('messagesContainer'),
  messagesList: document.getElementById('messagesList'),
  messageForm: document.getElementById('messageForm'),
  inputMessage: document.getElementById('inputMessage'),
  btnSend: document.getElementById('btnSend'),

  // Прикрепление фото и предпросмотр
  fileInput: document.getElementById('fileInput'),
  btnAttachImage: document.getElementById('btnAttachImage'),
  imagePreviewBar: document.getElementById('imagePreviewBar'),
  imagePreviewImg: document.getElementById('imagePreviewImg'),
  imagePreviewName: document.getElementById('imagePreviewName'),
  imagePreviewDimensions: document.getElementById('imagePreviewDimensions'),
  btnCancelImage: document.getElementById('btnCancelImage'),

  // Drag and drop overlay
  dragDropOverlay: document.getElementById('dragDropOverlay'),

  // Lightbox просмотр фото
  modalLightbox: document.getElementById('modalLightbox'),
  lightboxBackdrop: document.getElementById('lightboxBackdrop'),
  lightboxImage: document.getElementById('lightboxImage'),
  lightboxCaption: document.getElementById('lightboxCaption'),
  btnCloseLightbox: document.getElementById('btnCloseLightbox'),
};

// ============================================================
// 3. ИНИЦИАЛИЗАЦИЯ И СЕТЕВЫЕ СОБЫТИЯ
// ============================================================

document.addEventListener('DOMContentLoaded', () => {
  setupEventListeners();
  setupNetworkListeners();
});

/**
 * Подписка на события из главного процесса Electron (preload.js)
 */
function setupNetworkListeners() {
  // Прием сообщений от сервера
  window.api.onServerMessage((msg) => {
    handleServerMessage(msg);
  });

  // Отслеживание разрыва связи или сетевых ошибок
  window.api.onStatusChange((statusInfo) => {
    if (statusInfo.status === 'disconnected') {
      alert('Соединение с сервером разорвано');
      showLoginScreen();
    } else if (statusInfo.status === 'error') {
      showError(statusInfo.error || 'Ошибка сетевого сокета');
    }
  });
}

/**
 * Привязка событий кнопок и форм
 */
function setupEventListeners() {
  // Переключение вкладок авторизации (Вход / Регистрация)
  dom.tabLogin.addEventListener('click', () => setAuthMode('login'));
  dom.tabRegister.addEventListener('click', () => setAuthMode('register'));
  dom.authToggleLink.addEventListener('click', (e) => {
    e.preventDefault();
    setAuthMode(state.authMode === 'login' ? 'register' : 'login');
  });

  // Показ / скрытие пароля
  dom.btnTogglePassword.addEventListener('click', () => {
    const isPassword = dom.inputPassword.type === 'password';
    dom.inputPassword.type = isPassword ? 'text' : 'password';
    dom.btnTogglePassword.textContent = isPassword ? '🙈' : '👁️';
    dom.btnTogglePassword.title = isPassword ? 'Скрыть пароль' : 'Показать пароль';
    dom.btnTogglePassword.setAttribute('aria-label', isPassword ? 'Скрыть пароль' : 'Показать пароль');
  });

  // Вход / Регистрация (отправка формы)
  dom.loginForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    await handleLogin();
  });

  // Отключение
  dom.btnDisconnect.addEventListener('click', () => {
    window.api.disconnect();
    showLoginScreen();
  });

  // Отправка сообщения
  dom.messageForm.addEventListener('submit', (e) => {
    e.preventDefault();
    sendMessage();
  });

  // Модальное окно входа в комнату
  dom.btnOpenJoinModal.addEventListener('click', () => {
    dom.modalJoinRoom.classList.remove('hidden');
    dom.inputRoomName.value = '';
    dom.inputRoomName.focus();
  });

  dom.btnCloseJoinModal.addEventListener('click', () => {
    dom.modalJoinRoom.classList.add('hidden');
  });

  dom.formJoinRoom.addEventListener('submit', (e) => {
    e.preventDefault();
    const room = dom.inputRoomName.value.trim();
    if (room) {
      joinRoom(room);
      dom.modalJoinRoom.classList.add('hidden');
    }
  });

  // Модальное окно нового личного диалога
  dom.btnOpenPrivateModal.addEventListener('click', () => {
    dom.modalPrivate.classList.remove('hidden');
    dom.inputPrivateRecipient.value = '';
    dom.inputPrivateRecipient.focus();
  });

  dom.btnClosePrivateModal.addEventListener('click', () => {
    dom.modalPrivate.classList.add('hidden');
  });

  dom.formPrivate.addEventListener('submit', (e) => {
    e.preventDefault();
    const recipient = dom.inputPrivateRecipient.value.trim();
    if (recipient) {
      startPrivateChat(recipient);
      dom.modalPrivate.classList.add('hidden');
    }
  });

  // Покинуть текущую комнату
  dom.btnLeaveRoom.addEventListener('click', () => {
    if (state.activeChat.type === 'room' && state.activeChat.id !== 'general') {
      leaveRoom(state.activeChat.id);
    }
  });

  // Выбор изображения через кнопку-скрепку / камеру
  dom.btnAttachImage.addEventListener('click', () => {
    dom.fileInput.click();
  });

  dom.fileInput.addEventListener('change', (e) => {
    if (e.target.files && e.target.files[0]) {
      processImageFile(e.target.files[0]);
    }
  });

  // Удаление прикреплённого изображения
  dom.btnCancelImage.addEventListener('click', () => {
    clearAttachedImage();
  });

  // Просмотр полноразмерного фото (Lightbox)
  dom.btnCloseLightbox.addEventListener('click', closeLightbox);
  dom.lightboxBackdrop.addEventListener('click', closeLightbox);
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') closeLightbox();
  });

  // Вставка фото из буфера обмена (Ctrl + V)
  document.addEventListener('paste', handlePasteEvent);

  // Перетаскивание файлов (Drag and drop)
  window.addEventListener('dragover', handleDragOver);
  window.addEventListener('dragleave', handleDragLeave);
  window.addEventListener('drop', handleDrop);
}

// ============================================================
// 4. ЛОГИКА ВХОДА И УПРАВЛЕНИЯ ПОДКЛЮЧЕНИЕМ
// ============================================================

function setAuthMode(mode) {
  state.authMode = mode;
  hideError();
  resetPasswordVisibility();
  if (mode === 'login') {
    dom.tabLogin.classList.add('active');
    dom.tabRegister.classList.remove('active');
    dom.btnLogin.textContent = 'Войти';
    dom.authToggleLink.textContent = 'Нет аккаунта? Зарегистрироваться';
  } else {
    dom.tabRegister.classList.add('active');
    dom.tabLogin.classList.remove('active');
    dom.btnLogin.textContent = 'Зарегистрироваться';
    dom.authToggleLink.textContent = 'Уже есть аккаунт? Войти';
  }
}

function resetPasswordVisibility() {
  if (dom.inputPassword) {
    dom.inputPassword.type = 'password';
  }
  if (dom.btnTogglePassword) {
    dom.btnTogglePassword.textContent = '👁️';
    dom.btnTogglePassword.title = 'Показать пароль';
    dom.btnTogglePassword.setAttribute('aria-label', 'Показать пароль');
  }
}

async function handleLogin() {
  const username = dom.inputUsername.value.trim();
  const password = dom.inputPassword.value.trim();
  const host = dom.inputHost.value.trim() || '127.0.0.1';
  const port = parseInt(dom.inputPort.value.trim(), 10) || 5050;

  if (!username) {
    showError('Введите никнейм');
    return;
  }

  if (username.length > 16) {
    showError('Никнейм не должен превышать 16 символов');
    return;
  }

  if (!password) {
    showError('Введите пароль');
    return;
  }

  if (password.length > 64) {
    showError('Пароль не должен превышать 64 символа');
    return;
  }

  dom.btnLogin.disabled = true;
  dom.btnLogin.textContent = state.authMode === 'register' ? 'Регистрация...' : 'Вход...';
  hideError();

  try {
    // Подготавливаем состояние до подключения, чтобы любые входящие сообщения истории сразу попадали в структуры
    state.myUsername = username;
    state.serverHost = host;
    state.serverPort = port;
    state.rooms = new Set(['general']);
    state.privateUsers = new Set();
    state.messages = new Map();
    state.unreads = new Map();
    state.onlineUsers = new Set([username]);
    state.activeChat = { type: 'room', id: 'general' };
    clearAttachedImage();

    // Вызываем подключение через preload мост с авторизацией
    const result = await window.api.connect({
      host,
      port,
      username,
      password,
      action: state.authMode,
    });

    if (result && result.ok) {
      // Запрашиваем актуальный список комнат и пользователей с сервера
      window.api.send({ type: 'rooms' });
      window.api.send({ type: 'users' });

      // Переключаемся в интерфейс мессенджера
      showChatScreen();
      switchChat('room', 'general');
    } else {
      showError(result.reason || (state.authMode === 'register' ? 'Ошибка регистрации' : 'Ошибка авторизации'));
    }
  } catch (err) {
    showError(err.message || 'Не удалось подключиться к серверу');
  } finally {
    dom.btnLogin.disabled = false;
    dom.btnLogin.textContent = state.authMode === 'register' ? 'Зарегистрироваться' : 'Войти';
  }
}

function showLoginScreen() {
  dom.chatScreen.classList.add('hidden');
  dom.loginScreen.classList.remove('hidden');
  dom.inputPassword.value = '';
  setAuthMode('login');
}

function showChatScreen() {
  dom.loginScreen.classList.add('hidden');
  dom.chatScreen.classList.remove('hidden');

  // Установка данных профиля
  dom.myNickname.textContent = '@' + state.myUsername;
  dom.myAvatar.textContent = state.myUsername.charAt(0).toUpperCase();

  renderSidebar();
}

function showError(text) {
  dom.loginError.textContent = text;
  dom.loginError.classList.remove('hidden');
}

function hideError() {
  dom.loginError.classList.add('hidden');
}

// ============================================================
// 5. ОБРАБОТКА СООБЩЕНИЙ ОТ СЕРВЕРА
// ============================================================

function handleServerMessage(msg) {
  const type = msg.type;

  switch (type) {
    case 'text':
    case 'message':
    case 'image':
      // Групповое сообщение в комнату:
      // { type: "text", nickname: "dima", room: "general", message: "Привет", image?: "..." }
      onTextMessage(msg);
      break;

    case 'private':
      // Личное сообщение:
      // { type: "private", from: "vova", message: "Секретно", image?: "..." }
      onPrivateMessage(msg);
      break;

    case 'system':
      // Системное уведомление:
      // { type: "system", room: "general", message: "vova joined" }
      onSystemMessage(msg);
      break;

    case 'rooms':
      // Список комнат, в которых состоит пользователь:
      // { type: "rooms", rooms: ["general", "dev"] }
      if (Array.isArray(msg.rooms)) {
        msg.rooms.forEach((r) => state.rooms.add(r));
        renderSidebar();
      }
      break;

    case 'users':
      // Список активных пользователей онлайн:
      // { type: "users", users: ["alice", "bob"] }
      if (Array.isArray(msg.users)) {
        state.onlineUsers = new Set(msg.users);
        renderOnlineUsers();
      }
      break;

    case 'error':
      // Ошибка от сервера:
      // { type: "error", er_text: "Invalid room name" }
      onErrorMessage(msg);
      break;

    default:
      console.log('Неизвестный тип сообщения от сервера:', msg);
  }
}

/**
 * Обработка входящего текстового сообщения в комнате
 */
function onTextMessage(msg) {
  const room = msg.room || 'general';
  const chatKey = `room:${room}`;
  const isMine = msg.nickname === state.myUsername;

  // Добавляем отправителя в список онлайн, если сообщение не из истории
  if (msg.nickname && !msg.history) {
    state.onlineUsers.add(msg.nickname);
    renderOnlineUsers();
  }

  // Создаем объект сообщения
  const messageObj = {
    id: msg.id || (Date.now() + Math.random()),
    msgId: msg.id,
    sender: msg.nickname,
    text: msg.message || '',
    image: msg.image || null,
    time: msg.time || formatTime(),
    type: 'text',
    isMine: isMine,
  };

  // Сохраняем в памяти текущей сессии (с проверкой на дубликаты)
  const added = addMessageToStore(chatKey, messageObj);
  if (!added) return;

  // Если это активный открытый чат — сразу отображаем на экране
  if (state.activeChat.type === 'room' && state.activeChat.id === room) {
    appendMessageToView(messageObj);
    scrollToBottom();
  } else if (!msg.history) {
    // Иначе инкрементируем счетчик непрочитанных (только для свежих сообщений)
    incrementUnread(chatKey);
    renderSidebar();
  }
}

/**
 * Обработка входящего личного сообщения
 */
function onPrivateMessage(msg) {
  const isMine = msg.from === state.myUsername;
  const partner = isMine ? msg.to : msg.from;
  if (!partner) return;
  const chatKey = `private:${partner}`;

  // Автоматически добавляем собеседника в список ЛС
  state.privateUsers.add(partner);
  if (!msg.history && !isMine) {
    state.onlineUsers.add(partner);
    renderOnlineUsers();
  }

  const messageObj = {
    id: msg.id || (Date.now() + Math.random()),
    msgId: msg.id,
    sender: msg.from,
    text: msg.message || '',
    image: msg.image || null,
    time: msg.time || formatTime(),
    type: 'text',
    isMine: isMine,
  };

  const added = addMessageToStore(chatKey, messageObj);
  if (!added) return;

  if (state.activeChat.type === 'private' && state.activeChat.id === partner) {
    appendMessageToView(messageObj);
    scrollToBottom();
  } else if (!msg.history) {
    incrementUnread(chatKey);
  }

  renderSidebar();
}

/**
 * Обработка системного сообщения (например, "dima joined")
 */
function onSystemMessage(msg) {
  const room = msg.room || state.activeChat.id;
  const chatKey = `room:${room}`;

  const messageObj = {
    id: Date.now() + Math.random(),
    text: msg.message,
    time: formatTime(),
    type: 'system',
  };

  // Обновляем список онлайн на основе текста системного сообщения
  const match = msg.message.match(/^(\S+)\s+(joined|left)$/);
  if (match) {
    const user = match[1];
    const action = match[2];
    if (action === 'joined') {
      state.onlineUsers.add(user);
    } else if (action === 'left') {
      state.onlineUsers.delete(user);
    }
    renderOnlineUsers();
  }

  addMessageToStore(chatKey, messageObj);

  if (state.activeChat.type === 'room' && state.activeChat.id === room) {
    appendMessageToView(messageObj);
    scrollToBottom();
  }
}

/**
 * Обработка ошибки от сервера
 */
function onErrorMessage(msg) {
  const currentKey = getActiveChatKey();
  const messageObj = {
    id: Date.now() + Math.random(),
    text: `⚠️ Ошибка: ${msg.er_text || 'Неизвестная ошибка'}`,
    time: formatTime(),
    type: 'error',
  };

  addMessageToStore(currentKey, messageObj);
  appendMessageToView(messageObj);
  scrollToBottom();
}

// ============================================================
// 6. ОТПРАВКА СООБЩЕНИЙ И ДЕЙСТВИЯ ПОЛЬЗОВАТЕЛЯ
// ============================================================

function sendMessage() {
  const text = dom.inputMessage.value.trim();
  const image = state.attachedImage ? state.attachedImage.dataUrl : null;

  // Не отправляем абсолютно пустое сообщение без текста и без картинки
  if (!text && !image) return;

  if (state.activeChat.type === 'room') {
    // Отправка в текущую комнату
    const payload = {
      type: 'text',
      room: state.activeChat.id,
      message: text,
    };
    if (image) {
      payload.image = image;
    }
    window.api.send(payload);

    // Очищаем поле ввода и сбрасываем прикрепленное изображение
    dom.inputMessage.value = '';
    clearAttachedImage();
  } else if (state.activeChat.type === 'private') {
    // Отправка в ЛС
    const recipient = state.activeChat.id;
    const payload = {
      type: 'private',
      to: recipient,
      message: text,
    };
    if (image) {
      payload.image = image;
    }
    window.api.send(payload);

    // Поскольку сервер отправляет private-сообщение только получателю,
    // отправитель сразу фиксирует его у себя локально:
    const myMsg = {
      id: Date.now() + Math.random(),
      sender: state.myUsername,
      text: text,
      image: image,
      time: formatTime(),
      type: 'text',
      isMine: true,
    };

    const chatKey = `private:${recipient}`;
    addMessageToStore(chatKey, myMsg);
    appendMessageToView(myMsg);
    scrollToBottom();

    dom.inputMessage.value = '';
    clearAttachedImage();
  }
}

/**
 * Присоединение к новой комнате
 */
function joinRoom(roomName) {
  if (!roomName) return;
  state.rooms.add(roomName);

  window.api.send({
    type: 'join',
    room: roomName,
  });

  renderSidebar();
  switchChat('room', roomName);
}

/**
 * Выход из комнаты
 */
function leaveRoom(roomName) {
  if (roomName === 'general') return; // Из general выходить не стоит

  window.api.send({
    type: 'leave',
    room: roomName,
  });

  state.rooms.delete(roomName);
  renderSidebar();

  // Возвращаемся в general
  switchChat('room', 'general');
}

/**
 * Открытие диалога в ЛС
 */
function startPrivateChat(username) {
  if (!username) return;
  if (username === state.myUsername) {
    alert('Нельзя написать самому себе');
    return;
  }

  state.privateUsers.add(username);
  renderSidebar();
  switchChat('private', username);
}

// ============================================================
// 7. УПРАВЛЕНИЕ АКТИВНЫМ ЧАТОМ И РЕНДЕРИНГ
// ============================================================

function switchChat(type, id) {
  state.activeChat = { type, id };
  const chatKey = getActiveChatKey();

  // Сбрасываем непрочитанные
  state.unreads.set(chatKey, 0);

  // Обновляем шапку
  if (type === 'room') {
    dom.currentChatIcon.textContent = '#';
    dom.currentChatTitle.textContent = id;
    dom.currentChatSubtitle.textContent =
      id === 'general' ? 'Общая комната для всех участников' : `Комната #${id}`;

    // Кнопка выхода доступна для любых комнат, кроме general
    if (id !== 'general') {
      dom.btnLeaveRoom.classList.remove('hidden');
    } else {
      dom.btnLeaveRoom.classList.add('hidden');
    }

    dom.inputMessage.placeholder = `Написать в #${id}... (Enter для отправки)`;
  } else {
    dom.currentChatIcon.textContent = '@';
    dom.currentChatTitle.textContent = id;
    dom.currentChatSubtitle.textContent = 'Личная переписка (Private)';
    dom.btnLeaveRoom.classList.add('hidden');

    dom.inputMessage.placeholder = `Личное сообщение для @${id}...`;
  }

  // Перерисовываем список сообщений текущего чата
  renderCurrentChatMessages();
  renderSidebar();

  dom.inputMessage.focus();
}

function getActiveChatKey() {
  return `${state.activeChat.type}:${state.activeChat.id}`;
}

function addMessageToStore(chatKey, messageObj) {
  if (!state.messages.has(chatKey)) {
    state.messages.set(chatKey, []);
  }
  const list = state.messages.get(chatKey);
  // Предотвращаем дублирование сообщений при повторном получении истории
  if (messageObj.msgId && list.some((m) => m.msgId === messageObj.msgId)) {
    return false;
  }
  list.push(messageObj);
  return true;
}

function incrementUnread(chatKey) {
  const current = state.unreads.get(chatKey) || 0;
  state.unreads.set(chatKey, current + 1);
}

/**
 * Полная перерисовка сообщений выбранного чата
 */
function renderCurrentChatMessages() {
  dom.messagesList.innerHTML = '';
  const chatKey = getActiveChatKey();
  const msgs = state.messages.get(chatKey) || [];

  if (msgs.length === 0) {
    const placeholder = document.createElement('div');
    placeholder.className = 'system-msg';
    placeholder.textContent =
      state.activeChat.type === 'room'
        ? `Это начало комнаты #${state.activeChat.id}. Напишите первое сообщение!`
        : `Диалог с @${state.activeChat.id}. Сообщения видны только вам двоим.`;
    dom.messagesList.appendChild(placeholder);
  } else {
    msgs.forEach((m) => appendMessageToView(m));
  }

  scrollToBottom();
}

/**
 * Добавление одного сообщения в ленту
 */
function appendMessageToView(m) {
  if (m.type === 'system') {
    const div = document.createElement('div');
    div.className = 'system-msg';
    div.textContent = `ℹ️ ${m.text}`;
    dom.messagesList.appendChild(div);
    return;
  }

  if (m.type === 'error') {
    const div = document.createElement('div');
    div.className = 'error-msg';
    div.textContent = m.text;
    dom.messagesList.appendChild(div);
    return;
  }

  // Обычное текстовое сообщение
  const row = document.createElement('div');
  row.className = `msg-row ${m.isMine ? 'outgoing' : 'incoming'}`;

  const meta = document.createElement('div');
  meta.className = 'msg-meta';

  const sender = document.createElement('span');
  sender.className = 'msg-sender';
  sender.textContent = m.isMine ? 'Вы' : `@${m.sender}`;

  const time = document.createElement('span');
  time.className = 'msg-time';
  time.textContent = m.time;

  meta.appendChild(sender);
  meta.appendChild(time);

  const bubble = document.createElement('div');
  bubble.className = 'msg-bubble';

  // Если прикреплено изображение
  if (m.image) {
    const imgWrap = document.createElement('div');
    imgWrap.className = 'msg-image-wrap';
    imgWrap.title = 'Нажмите, чтобы открыть фото на весь экран';

    const img = document.createElement('img');
    img.className = 'msg-image';
    const src = m.image.startsWith('data:') ? m.image : `data:image/png;base64,${m.image}`;
    img.src = src;
    img.alt = 'Фото';
    img.loading = 'lazy';

    imgWrap.appendChild(img);

    imgWrap.addEventListener('click', () => {
      openLightbox(src, m.text || '');
    });

    bubble.appendChild(imgWrap);
  }

  // Текстовая подпись или обычное сообщение
  if (m.text) {
    if (m.image) {
      const caption = document.createElement('div');
      caption.className = 'msg-caption';
      caption.textContent = m.text;
      bubble.appendChild(caption);
    } else {
      bubble.textContent = m.text;
    }
  }

  row.appendChild(meta);
  row.appendChild(bubble);

  dom.messagesList.appendChild(row);
}

/**
 * Отрисовка списка комнат и личных сообщений в сайдбаре
 */
function renderSidebar() {
  // 1. Комнаты
  dom.roomsList.innerHTML = '';
  state.rooms.forEach((room) => {
    const key = `room:${room}`;
    const isActive = state.activeChat.type === 'room' && state.activeChat.id === room;
    const unread = state.unreads.get(key) || 0;

    const item = document.createElement('div');
    item.className = `nav-item ${isActive ? 'active' : ''}`;
    item.onclick = () => switchChat('room', room);

    const nameSpan = document.createElement('span');
    nameSpan.className = 'nav-item-name';
    nameSpan.innerHTML = `<span>#</span> ${escapeHtml(room)}`;
    item.appendChild(nameSpan);

    if (unread > 0) {
      const badge = document.createElement('span');
      badge.className = 'badge';
      badge.textContent = unread;
      item.appendChild(badge);
    }

    dom.roomsList.appendChild(item);
  });

  // 2. Личные диалоги
  dom.privateList.innerHTML = '';
  state.privateUsers.forEach((user) => {
    const key = `private:${user}`;
    const isActive = state.activeChat.type === 'private' && state.activeChat.id === user;
    const unread = state.unreads.get(key) || 0;

    const item = document.createElement('div');
    item.className = `nav-item ${isActive ? 'active' : ''}`;
    item.onclick = () => switchChat('private', user);

    const nameSpan = document.createElement('span');
    nameSpan.className = 'nav-item-name';
    nameSpan.innerHTML = `<span>@</span> ${escapeHtml(user)}`;
    item.appendChild(nameSpan);

    if (unread > 0) {
      const badge = document.createElement('span');
      badge.className = 'badge';
      badge.textContent = unread;
      item.appendChild(badge);
    }

    dom.privateList.appendChild(item);
  });
}

/**
 * Отрисовка пользователей онлайн
 */
function renderOnlineUsers() {
  dom.onlineUsersList.innerHTML = '';
  dom.onlineCount.textContent = state.onlineUsers.size;

  state.onlineUsers.forEach((user) => {
    const item = document.createElement('div');
    item.className = 'online-user-item';
    const isMe = user === state.myUsername;

    item.innerHTML = `<span class="status-dot"></span> ${escapeHtml(user)} ${isMe ? '(вы)' : ''}`;

    if (!isMe) {
      item.title = 'Нажмите, чтобы написать в ЛС';
      item.onclick = () => startPrivateChat(user);
    }

    dom.onlineUsersList.appendChild(item);
  });
}

// ============================================================
// 8. ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================

function scrollToBottom() {
  dom.messagesContainer.scrollTop = dom.messagesContainer.scrollHeight;
}

function formatTime() {
  const d = new Date();
  const hh = String(d.getHours()).padStart(2, '0');
  const mm = String(d.getMinutes()).padStart(2, '0');
  return `${hh}:${mm}`;
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

// ============================================================
// 9. РАБОТА С ФОТО (ЧТЕНИЕ, ОПТИМИЗАЦИЯ BASE64, LIGHTBOX, DRAG-N-DROP)
// ============================================================

/**
 * Чтение файла изображения и конвертация в base64 (DataURL) с умной оптимизацией
 */
function processImageFile(file) {
  if (!file || !file.type.startsWith('image/')) {
    alert('Пожалуйста, выберите файл изображения (PNG, JPG, WebP, GIF)');
    return;
  }

  const reader = new FileReader();
  reader.onload = (e) => {
    const rawDataUrl = e.target.result;

    const img = new Image();
    img.onload = () => {
      const maxDim = 1600;
      let width = img.width;
      let height = img.height;

      // Если изображение слишком большое (например 4K/8K или файл > 1.5 МБ),
      // оптимизируем размер через canvas, сохраняя превосходное качество
      if (width > maxDim || height > maxDim || file.size > 1.5 * 1024 * 1024) {
        if (width > height) {
          if (width > maxDim) {
            height = Math.round((height * maxDim) / width);
            width = maxDim;
          }
        } else {
          if (height > maxDim) {
            width = Math.round((width * maxDim) / height);
            height = maxDim;
          }
        }

        const canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0, width, height);

        const optimizedDataUrl = canvas.toDataURL('image/jpeg', 0.85);
        setAttachedImage({
          dataUrl: optimizedDataUrl,
          name: file.name || 'photo.jpg',
          width,
          height,
          size: Math.round((optimizedDataUrl.length * 3) / 4),
        });
      } else {
        // Оставляем исходный base64
        setAttachedImage({
          dataUrl: rawDataUrl,
          name: file.name || 'photo.png',
          width,
          height,
          size: file.size,
        });
      }
    };
    img.src = rawDataUrl;
  };
  reader.readAsDataURL(file);
}

/**
 * Установка прикрепленного изображения в состояние и отображение превью
 */
function setAttachedImage(imgData) {
  state.attachedImage = imgData;
  dom.imagePreviewImg.src = imgData.dataUrl;
  dom.imagePreviewName.textContent = imgData.name;
  const sizeKb = Math.round(imgData.size / 1024);
  dom.imagePreviewDimensions.textContent = `${imgData.width}×${imgData.height} px (${sizeKb} KB)`;
  dom.imagePreviewBar.classList.remove('hidden');
  dom.inputMessage.focus();
}

/**
 * Очистка прикреплённого изображения
 */
function clearAttachedImage() {
  state.attachedImage = null;
  dom.imagePreviewImg.src = '';
  dom.imagePreviewName.textContent = '';
  dom.imagePreviewDimensions.textContent = '';
  dom.imagePreviewBar.classList.add('hidden');
  dom.fileInput.value = '';
}

/**
 * Открытие изображения на весь экран
 */
function openLightbox(src, captionText) {
  dom.lightboxImage.src = src;
  if (captionText) {
    dom.lightboxCaption.textContent = captionText;
    dom.lightboxCaption.classList.remove('hidden');
  } else {
    dom.lightboxCaption.textContent = '';
    dom.lightboxCaption.classList.add('hidden');
  }
  dom.modalLightbox.classList.remove('hidden');
}

/**
 * Закрытие модального окна просмотра фото
 */
function closeLightbox() {
  dom.modalLightbox.classList.add('hidden');
  dom.lightboxImage.src = '';
  dom.lightboxCaption.textContent = '';
}

/**
 * Обработка вставки из буфера обмена (Ctrl + V)
 */
function handlePasteEvent(e) {
  // Работает только если находимся на экране чата
  if (dom.chatScreen.classList.contains('hidden')) return;

  if (!e.clipboardData) return;
  const items = e.clipboardData.items;
  if (!items) return;

  for (let i = 0; i < items.length; i++) {
    if (items[i].type.indexOf('image') !== -1) {
      const file = items[i].getAsFile();
      if (file) {
        processImageFile(file);
        e.preventDefault();
        break;
      }
    }
  }
}

/**
 * Обработчики Drag and Drop
 */
function handleDragOver(e) {
  if (dom.chatScreen.classList.contains('hidden')) return;
  e.preventDefault();
  e.stopPropagation();
  if (e.dataTransfer && e.dataTransfer.types && Array.from(e.dataTransfer.types).includes('Files')) {
    dom.dragDropOverlay.classList.remove('hidden');
  }
}

function handleDragLeave(e) {
  e.preventDefault();
  e.stopPropagation();
  // Скрываем оверлей только если курсор вышел за пределы окна
  if (e.relatedTarget === null || e.clientX <= 0 || e.clientY <= 0) {
    dom.dragDropOverlay.classList.add('hidden');
  }
}

function handleDrop(e) {
  if (dom.chatScreen.classList.contains('hidden')) return;
  e.preventDefault();
  e.stopPropagation();
  dom.dragDropOverlay.classList.add('hidden');

  if (e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length > 0) {
    const file = e.dataTransfer.files[0];
    if (file.type.startsWith('image/')) {
      processImageFile(file);
    }
  }
}
