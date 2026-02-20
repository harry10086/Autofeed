// Autofeed 2.2 — Cloudflare Worker 控制面板
// 部署方式:命令 `wrangler deploy` 或在 Cloudflare Dashboard 中粘贴此代码

export default {
  async fetch(request) {
    return new Response(HTML_CONTENT, {
      headers: { 'Content-Type': 'text/html; charset=utf-8' },
    });
  },
};

const HTML_CONTENT = `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<link rel="icon" href="https://fav.farm/🐢" />
<title>Autofeed 控制面板</title>
<meta name="description" content="ESP32 远程控制面板">
<link rel="preconnect" href="https://googlefonts.mirrors.sjtug.sjtu.edu.cn">
<link href="https://googlefonts.mirrors.sjtug.sjtu.edu.cn/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<script src="https://unpkg.com/mqtt@5/dist/mqtt.min.js"></script>
<style>
  :root {
    --bg: #0f1117;
    --surface: #1a1d27;
    --surface2: #242836;
    --border: #2e3348;
    --text: #e4e6f0;
    --text2: #8b8fa8;
    --accent: #6c5ce7;
    --accent-glow: rgba(108,92,231,0.3);
    --green: #00b894;
    --green-glow: rgba(0,184,148,0.3);
    --red: #e17055;
    --red-glow: rgba(225,112,85,0.3);
    --blue: #0984e3;
    --blue-glow: rgba(9,132,227,0.3);
    --yellow: #fdcb6e;
    --radius: 16px;
    --radius-sm: 10px;
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }

  body {
    font-family: 'Inter', -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    padding: 20px;
  }

  .container {
    max-width: 680px;
    margin: 0 auto;
  }

  /* Header */
  .header {
    text-align: center;
    padding: 30px 0 20px;
  }
  .header h1 {
    font-size: 1.8rem;
    font-weight: 700;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
  }
  .header h1 span {
    background: linear-gradient(135deg, #6c5ce7, #a29bfe);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
  }
  .status-badge {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 14px;
    border-radius: 20px;
    font-size: 0.8rem;
    font-weight: 500;
    background: var(--surface);
    border: 1px solid var(--border);
    transition: all 0.3s;
  }
  .status-badge.connected {
    border-color: var(--green);
    box-shadow: 0 0 12px var(--green-glow);
  }
  .status-badge.disconnected {
    border-color: var(--red);
    box-shadow: 0 0 12px var(--red-glow);
  }
  .status-dot {
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--red);
    transition: background 0.3s;
  }
  .status-badge.connected .status-dot { background: var(--green); }

  /* Cards */
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 24px;
    margin-bottom: 16px;
    transition: border-color 0.3s, box-shadow 0.3s;
  }
  .card:hover {
    border-color: rgba(108,92,231,0.4);
    box-shadow: 0 4px 24px rgba(0,0,0,0.3);
  }
  .card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 18px;
  }
  .card-title {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 1.05rem;
    font-weight: 600;
  }
  .card-icon {
    font-size: 1.3rem;
  }
  .running-badge {
    font-size: 0.7rem;
    padding: 3px 10px;
    border-radius: 12px;
    font-weight: 600;
    background: var(--green-glow);
    color: var(--green);
    display: none;
  }
  .running-badge.active { display: inline-block; }

  /* Toggle */
  .toggle {
    position: relative;
    width: 52px; height: 28px;
    cursor: pointer;
  }
  .toggle input { display: none; }
  .toggle .slider {
    position: absolute;
    inset: 0;
    background: var(--surface2);
    border: 2px solid var(--border);
    border-radius: 14px;
    transition: all 0.3s;
  }
  .toggle .slider::before {
    content: '';
    position: absolute;
    width: 20px; height: 20px;
    left: 2px; top: 2px;
    background: var(--text2);
    border-radius: 50%;
    transition: all 0.3s;
  }
  .toggle input:checked + .slider {
    background: var(--accent);
    border-color: var(--accent);
    box-shadow: 0 0 12px var(--accent-glow);
  }
  .toggle input:checked + .slider::before {
    transform: translateX(24px);
    background: white;
  }

  /* Form controls */
  .field-group {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-top: 14px;
  }
  .field {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .field.full { grid-column: 1 / -1; }
  .field label {
    font-size: 0.78rem;
    color: var(--text2);
    font-weight: 500;
  }
  .field input, .field select {
    background: var(--surface2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 10px 14px;
    color: var(--text);
    font-size: 0.9rem;
    font-family: inherit;
    outline: none;
    transition: border-color 0.2s;
  }
  .field input:focus, .field select:focus {
    border-color: var(--accent);
  }
  .field input[type="number"] {
    -moz-appearance: textfield;
  }

  /* Time list */
  .time-list {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 8px;
  }
  .time-tag {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 6px 12px;
    background: var(--surface2);
    border: 1px solid var(--border);
    border-radius: 8px;
    font-size: 0.85rem;
    font-weight: 500;
  }
  .time-tag .remove {
    cursor: pointer;
    color: var(--red);
    font-size: 1rem;
    line-height: 1;
    opacity: 0.7;
    transition: opacity 0.2s;
  }
  .time-tag .remove:hover { opacity: 1; }

  .add-time-row {
    display: flex;
    gap: 8px;
    margin-top: 10px;
  }
  .add-time-row input {
    flex: 1;
    background: var(--surface2);
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 8px 12px;
    color: var(--text);
    font-family: inherit;
    outline: none;
  }
  .btn-add {
    background: var(--accent);
    border: none;
    border-radius: var(--radius-sm);
    padding: 8px 16px;
    color: white;
    font-weight: 600;
    cursor: pointer;
    font-size: 0.85rem;
    transition: opacity 0.2s;
  }
  .btn-add:hover { opacity: 0.85; }

  /* Send button */
  .btn-send {
    width: 100%;
    padding: 14px;
    background: linear-gradient(135deg, #6c5ce7, #a29bfe);
    border: none;
    border-radius: var(--radius-sm);
    color: white;
    font-size: 1rem;
    font-weight: 600;
    font-family: inherit;
    cursor: pointer;
    margin-top: 20px;
    transition: opacity 0.2s, transform 0.1s;
  }
  .btn-send:hover { opacity: 0.9; }
  .btn-send:active { transform: scale(0.98); }

  /* Status panel */
  .status-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }
  .status-item {
    background: var(--surface2);
    border-radius: var(--radius-sm);
    padding: 14px;
  }
  .status-item .label {
    font-size: 0.75rem;
    color: var(--text2);
    margin-bottom: 4px;
  }
  .status-item .value {
    font-size: 0.95rem;
    font-weight: 600;
  }
  .status-item .value.on { color: var(--green); }
  .status-item .value.off { color: var(--text2); }
  .status-item .value.warn { color: var(--yellow); }
  .status-item.full { grid-column: 1 / -1; }

  /* Toast */
  .toast {
    position: fixed;
    bottom: 30px;
    left: 50%;
    transform: translateX(-50%) translateY(100px);
    background: var(--surface);
    border: 1px solid var(--accent);
    color: var(--text);
    padding: 12px 24px;
    border-radius: var(--radius-sm);
    font-size: 0.85rem;
    font-weight: 500;
    box-shadow: 0 8px 32px rgba(0,0,0,0.4);
    opacity: 0;
    transition: all 0.3s;
    z-index: 100;
  }
  .toast.show {
    opacity: 1;
    transform: translateX(-50%) translateY(0);
  }

  /* Responsive */
  @media (max-width: 500px) {
    body { padding: 12px; }
    .header h1 { font-size: 1.5rem; }
    .field-group { grid-template-columns: 1fr; }
    .status-grid { grid-template-columns: 1fr; }
  }
</style>
</head>
<body>
<div class="container">

  <!-- Header -->
  <div class="header">
    <h1>🐢 <span>Autofeed 控制面板</span></h1>
    <div class="status-badge disconnected" id="mqttBadge">
      <span class="status-dot"></span>
      <span id="mqttStatusText">未连接</span>
    </div>
  </div>

  <!-- 电机控制 -->
  <div class="card" id="motorCard">
    <div class="card-header">
      <div class="card-title">
        <span class="card-icon">⚙️</span> 电机控制
        <span class="running-badge" id="motorRunBadge">运行中</span>
      </div>
      <label class="toggle">
        <input type="checkbox" id="motorEnabled">
        <span class="slider"></span>
      </label>
    </div>
    <div class="field-group">
      <div class="field">
        <label>每次运行时长 (秒)</label>
        <input type="number" id="motorRunSeconds" value="30" min="1" max="3600">
      </div>
      <div class="field">
        <label>间隔天数</label>
        <select id="motorIntervalDays">
          <option value="1">每天</option>
          <option value="2">隔天 (每2天)</option>
          <option value="3">每3天</option>
          <option value="4">每4天</option>
          <option value="5">每5天</option>
        </select>
      </div>
      <div class="field full">
        <label>定时启动时间</label>
        <div class="time-list" id="timeList"></div>
        <div class="add-time-row">
          <input type="time" id="newTime" value="08:00">
          <button class="btn-add" onclick="addTime()">添加</button>
        </div>
      </div>
    </div>
  </div>

  <!-- 进水泵控制 -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">
        <span class="card-icon">💧</span> 进水泵
        <span class="running-badge" id="pumpInRunBadge">运行中</span>
      </div>
      <label class="toggle">
        <input type="checkbox" id="pumpInEnabled">
        <span class="slider"></span>
      </label>
    </div>
    <p style="color:var(--text2);font-size:0.85rem;">开启后由水位传感器自动控制进水泵工作</p>
  </div>

  <!-- 循环泵控制 -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">
        <span class="card-icon">🔄</span> 循环泵
        <span class="running-badge" id="pumpOutRunBadge">运行中</span>
      </div>
      <label class="toggle">
        <input type="checkbox" id="pumpOutEnabled">
        <span class="slider"></span>
      </label>
    </div>
    <div class="field-group">
      <div class="field">
        <label>开启时间 (秒)</label>
        <input type="number" id="pumpOutOn" value="30" min="1" max="3600">
      </div>
      <div class="field">
        <label>关闭时间 (秒)</label>
        <input type="number" id="pumpOutOff" value="30" min="1" max="3600">
      </div>
    </div>
  </div>

  <!-- 发送按钮 -->
  <button class="btn-send" onclick="sendCommand()">📡 发送配置</button>

  <!-- 设备状态 -->
  <div class="card" style="margin-top: 16px;">
    <div class="card-header">
      <div class="card-title">
        <span class="card-icon">📊</span> 设备状态
      </div>
    </div>
    <div class="status-grid">
      <div class="status-item">
        <div class="label">电机</div>
        <div class="value off" id="stMotor">--</div>
      </div>
      <div class="status-item">
        <div class="label">进水泵</div>
        <div class="value off" id="stPumpIn">--</div>
      </div>
      <div class="status-item">
        <div class="label">循环泵</div>
        <div class="value off" id="stPumpOut">--</div>
      </div>
      <div class="status-item">
        <div class="label">WiFi</div>
        <div class="value off" id="stWifi">--</div>
      </div>
      <div class="status-item">
        <div class="label">低水位报警</div>
        <div class="value off" id="stWaterLow">--</div>
      </div>
      <div class="status-item">
        <div class="label">水位已满</div>
        <div class="value off" id="stWaterHigh">--</div>
      </div>
      <div class="status-item full">
        <div class="label">设备时间</div>
        <div class="value" id="stTime">--</div>
      </div>
    </div>
  </div>

</div>

<!-- Toast -->
<div class="toast" id="toast"></div>

<script>
// ==================== HiveMQ 配置 (请填写) ====================
const MQTT_CONFIG = {
  broker: 'wss://xxx.hivemq.cloud:8884/mqtt',   // HiveMQ WebSocket 地址
  username: 'name',                               // HiveMQ 项目中新建用户，用户名
  password: 'password',                               // HiveMQ 密码
  clientId: 'autofeed-web-' + Math.random().toString(16).slice(2, 8),
};

const TOPIC_CMD = 'autofeed/cmd';
const TOPIC_STATUS = 'autofeed/status';

// ==================== 全局状态 ====================
let client = null;
let scheduleTimes = [];
let lastInteraction = 0; // 上次用户操作时间

// ==================== MQTT 连接 ====================
function connectMQTT() {
  const badge = document.getElementById('mqttBadge');
  const statusText = document.getElementById('mqttStatusText');

  statusText.textContent = '连接中...';

  client = mqtt.connect(MQTT_CONFIG.broker, {
    username: MQTT_CONFIG.username,
    password: MQTT_CONFIG.password,
    clientId: MQTT_CONFIG.clientId,
    protocolVersion: 4,
    clean: true,
    reconnectPeriod: 5000,
  });

  client.on('connect', () => {
    badge.className = 'status-badge connected';
    statusText.textContent = '已连接';
    client.subscribe(TOPIC_STATUS);
    showToast('✅ MQTT 已连接');
  });

  client.on('error', (err) => {
    badge.className = 'status-badge disconnected';
    statusText.textContent = '连接失败';
    console.error('MQTT error:', err);
  });

  client.on('offline', () => {
    badge.className = 'status-badge disconnected';
    statusText.textContent = '已断开';
  });

  client.on('reconnect', () => {
    statusText.textContent = '重连中...';
  });

  client.on('message', (topic, message) => {
    if (topic === TOPIC_STATUS) {
      try {
        const data = JSON.parse(message.toString());
        updateStatusPanel(data);
      } catch (e) {
        console.error('Parse error:', e);
      }
    }
  });
}

// ==================== 更新状态面板 ====================
function updateStatusPanel(data) {
  setStatus('stMotor', data.motor?.running, data.motor?.enabled ? '已启用' : '已禁用');
  setStatus('stPumpIn', data.pumpIn?.running, data.pumpIn?.enabled ? '已启用' : '已禁用');
  setStatus('stPumpOut', data.pumpOut?.running, data.pumpOut?.enabled ? '已启用' : '已禁用');
  setStatus('stWifi', data.wifi, '');
  setStatusWarn('stWaterLow', data.waterLow);
  setStatusWarn('stWaterHigh', data.waterHigh);

  document.getElementById('stTime').textContent = data.time || '--';

  // 更新运行徽章
  toggleBadge('motorRunBadge', data.motor?.running);
  toggleBadge('pumpInRunBadge', data.pumpIn?.running);
  toggleBadge('pumpOutRunBadge', data.pumpOut?.running);

  // 同步控制面板与设备状态 (如果用户最近没有操作)
  if (Date.now() - lastInteraction > 30000) {
    syncUI(data);
  }
}

function setStatus(id, running, fallback) {
  const el = document.getElementById(id);
  if (running) {
    el.textContent = '运行中';
    el.className = 'value on';
  } else {
    el.textContent = fallback || '关闭';
    el.className = 'value off';
  }
}

function setStatusWarn(id, active) {
  const el = document.getElementById(id);
  if (active) {
    el.textContent = '是';
    el.className = 'value warn';
  } else {
    el.textContent = '否';
    el.className = 'value off';
  }
}

function toggleBadge(id, active) {
  const el = document.getElementById(id);
  el.className = active ? 'running-badge active' : 'running-badge';
}

// 将设备状态同步到 UI 控件
function syncUI(data) {
  if (data.motor) {
    document.getElementById('motorEnabled').checked = data.motor.enabled;
    document.getElementById('motorRunSeconds').value = data.motor.runSeconds || 30;
    if (data.motor.schedule) {
      document.getElementById('motorIntervalDays').value = data.motor.schedule.intervalDays || 1;
      if (data.motor.schedule.times) {
        scheduleTimes = [...data.motor.schedule.times];
        renderTimes();
      }
    }
  }
  if (data.pumpIn) {
    document.getElementById('pumpInEnabled').checked = data.pumpIn.enabled;
  }
  if (data.pumpOut) {
    document.getElementById('pumpOutEnabled').checked = data.pumpOut.enabled;
    document.getElementById('pumpOutOn').value = data.pumpOut.onSeconds || 30;
    document.getElementById('pumpOutOff').value = data.pumpOut.offSeconds || 30;
  }
}

// ==================== 时间管理 ====================
function addTime() {
  const input = document.getElementById('newTime');
  const time = input.value;
  if (!time) return;
  const formatted = time.substring(0, 5);  // "HH:MM"
  if (scheduleTimes.includes(formatted)) {
    showToast('⚠️ 该时间已存在');
    return;
  }
  scheduleTimes.push(formatted);
  scheduleTimes.sort();
  renderTimes();
  recordInteraction();
}

function removeTime(index) {
  scheduleTimes.splice(index, 1);
  renderTimes();
  recordInteraction();
}

function renderTimes() {
  const container = document.getElementById('timeList');
  container.innerHTML = scheduleTimes.map((t, i) =>
    '<span class="time-tag">' + t +
    ' <span class="remove" onclick="removeTime(' + i + ')">×</span></span>'
  ).join('');
}

// ==================== 发送命令 ====================
function sendCommand() {
  if (!client || !client.connected) {
    showToast('❌ MQTT 未连接');
    return;
  }

  const cmd = {
    motor: {
      enabled: document.getElementById('motorEnabled').checked,
      runSeconds: parseInt(document.getElementById('motorRunSeconds').value) || 30,
      schedule: {
        intervalDays: parseInt(document.getElementById('motorIntervalDays').value) || 1,
        times: [...scheduleTimes],
      },
    },
    pumpIn: {
      enabled: document.getElementById('pumpInEnabled').checked,
    },
    pumpOut: {
      enabled: document.getElementById('pumpOutEnabled').checked,
      onSeconds: parseInt(document.getElementById('pumpOutOn').value) || 30,
      offSeconds: parseInt(document.getElementById('pumpOutOff').value) || 30,
    },
  };

  client.publish(TOPIC_CMD, JSON.stringify(cmd));
  showToast('✅ 配置已发送');
}

// ==================== Toast ====================
function showToast(msg) {
  const toast = document.getElementById('toast');
  toast.textContent = msg;
  toast.classList.add('show');
  setTimeout(() => toast.classList.remove('show'), 2500);
}

// ==================== 交互检测 ====================
function recordInteraction() {
  lastInteraction = Date.now();
  console.log('User interaction detected, pausing sync for 30s');
}

function initInteractionListeners() {
  document.querySelectorAll('input, select').forEach(el => {
    el.addEventListener('input', recordInteraction);
    el.addEventListener('change', recordInteraction);
  });
}

// ==================== 初始化 ====================
renderTimes();
initInteractionListeners();
connectMQTT();
</script>
</body>
</html>`;
