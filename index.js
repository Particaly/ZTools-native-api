const os = require('os');

// 根据平台加载对应的原生模块
const addon = require('./build/Release/ztools_native.node');
const platform = os.platform();

class ClipboardMonitor {
  constructor() {
    this._callback = null;
    this._isMonitoring = false;
  }

  /**
   * 启动剪贴板监控
   * @param {Function} callback - 剪贴板变化时的回调函数（无参数）
   */
  start(callback) {
    if (this._isMonitoring) {
      throw new Error('Monitor is already running');
    }

    if (typeof callback !== 'function') {
      throw new TypeError('Callback must be a function');
    }

    this._callback = callback;
    this._isMonitoring = true;

    addon.startMonitor(() => {
      if (this._callback) {
        this._callback();
      }
    });
  }

  /**
   * 停止剪贴板监控
   */
  stop() {
    if (!this._isMonitoring) {
      return;
    }

    addon.stopMonitor();
    this._isMonitoring = false;
    this._callback = null;
  }

  /**
   * 是否正在监控
   */
  get isMonitoring() {
    return this._isMonitoring;
  }
}

class WindowMonitor {
  constructor() {
    this._callback = null;
    this._isMonitoring = false;
  }

  /**
   * 启动窗口监控
   * @param {Function} callback - 窗口切换时的回调函数
   * - macOS: { appName: string, bundleId: string }
   * - Windows: { appName: string, processId: number }
   */
  start(callback) {
    if (this._isMonitoring) {
      throw new Error('Window monitor is already running');
    }

    if (typeof callback !== 'function') {
      throw new TypeError('Callback must be a function');
    }

    this._callback = callback;
    this._isMonitoring = true;

    addon.startWindowMonitor((windowInfo) => {
      if (this._callback) {
        this._callback(windowInfo);
      }
    });
  }

  /**
   * 停止窗口监控
   */
  stop() {
    if (!this._isMonitoring) {
      return;
    }

    addon.stopWindowMonitor();
    this._isMonitoring = false;
    this._callback = null;
  }

  /**
   * 是否正在监控
   */
  get isMonitoring() {
    return this._isMonitoring;
  }
}


// 窗口管理类
class WindowManager {
  /**
   * 获取当前激活的窗口信息
   * @returns {{appName: string, bundleId?: string, processId?: number}|null} 窗口信息对象
   * - macOS: { appName, bundleId }
   * - Windows: { appName, processId }
   */
  static getActiveWindow() {
    const result = addon.getActiveWindow();
    if (!result || result.error) {
      return null;
    }
    return result;
  }

  /**
   * 根据标识符激活指定应用的窗口
   * @param {string|number} identifier - 应用标识符
   * - macOS: bundleId (string)
   * - Windows: processId (number)
   * @returns {boolean} 是否激活成功
   */
  static activateWindow(identifier) {
    if (platform === 'darwin') {
      // macOS: bundleId 是字符串
      if (typeof identifier !== 'string') {
        throw new TypeError('On macOS, identifier must be a bundleId (string)');
      }
    } else if (platform === 'win32') {
      // Windows: processId 是数字
      if (typeof identifier !== 'number') {
        throw new TypeError('On Windows, identifier must be a processId (number)');
      }
    }
    return addon.activateWindow(identifier);
  }

  /**
   * 获取当前平台
   * @returns {string} 'darwin' | 'win32'
   */
  static getPlatform() {
    return platform;
  }
}

// 导出三个类
module.exports = {
  ClipboardMonitor,
  WindowMonitor,
  WindowManager
};

// 为了向后兼容，默认导出 ClipboardMonitor
module.exports.default = ClipboardMonitor;
