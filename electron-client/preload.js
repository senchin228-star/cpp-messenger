/**
 * preload.js - Мост безопасности (Preload Bridge)
 *
 * Безопасно предоставляет браузерному окну (renderer) доступ к API сокета
 * через contextBridge, не открывая полный доступ к Node.js в веб-странице.
 */

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('api', {
  /**
   * Подключение к C++ серверу и логин / регистрация
   * @param {{ host: string, port: number, username: string, password?: string, action?: 'login'|'register' }} config
   * @returns {Promise<{ ok: boolean, reason: string }>}
   */
  connect: (config) => ipcRenderer.invoke('tcp:connect', config),

  /**
   * Отправка JSON-сообщения на сервер
   * @param {object} message
   */
  send: (message) => ipcRenderer.send('tcp:send', message),

  /**
   * Отключение от сервера
   */
  disconnect: () => ipcRenderer.send('tcp:disconnect'),

  /**
   * Подписка на сообщения от сервера (text, private, system, error, rooms, users)
   * @param {(msg: object) => void} callback
   */
  onServerMessage: (callback) => {
    ipcRenderer.on('server-message', (_event, msg) => callback(msg));
  },

  /**
   * Подписка на изменение статуса сетевого подключения
   * @param {(statusInfo: { status: 'disconnected' | 'error', error?: string }) => void} callback
   */
  onStatusChange: (callback) => {
    ipcRenderer.on('connection-status', (_event, statusInfo) => callback(statusInfo));
  },
});
