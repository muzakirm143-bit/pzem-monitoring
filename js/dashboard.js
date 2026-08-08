// ============ KONFIGURASI ============
// Gunakan port 8884 untuk WebSocket (bukan 8000)
const MQTT_BROKER = 'wss://broker.hivemq.com:8884/mqtt';
const MQTT_TOPIC = 'pzem/esp32/data';
const MQTT_TOPIC_RELAY = 'pzem/esp32/relay';

// ============ INISIALISASI ============
let client = null;
let powerData = [];
const MAX_DATA_POINTS = 30;
let updateCount = 0;

// ============ CHART ============
const ctx = document.getElementById('powerChart').getContext('2d');
const powerChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Daya (W)',
            data: [],
            borderColor: '#60a5fa',
            backgroundColor: 'rgba(96, 165, 250, 0.1)',
            tension: 0.4,
            fill: true,
            pointBackgroundColor: '#60a5fa',
            pointRadius: 3,
            pointHoverRadius: 6
        }]
    },
    options: {
        responsive: true,
        maintainAspectRatio: true,
        plugins: {
            legend: {
                labels: {
                    color: '#94a3b8',
                    font: { size: 12 }
                }
            }
        },
        scales: {
            y: {
                beginAtZero: true,
                grid: {
                    color: 'rgba(255, 255, 255, 0.05)'
                },
                ticks: {
                    color: '#94a3b8'
                }
            },
            x: {
                grid: {
                    display: false
                },
                ticks: {
                    color: '#94a3b8',
                    maxTicksLimit: 10
                }
            }
        }
    }
});

// ============ MQTT CONNECTION ============
function connectMQTT() {
    updateStatus('mqttStatus', 'Connecting...', false);
    
    // Konfigurasi MQTT dengan opsi yang lebih robust
    const options = {
        clientId: 'pzem_dashboard_' + Math.random().toString(16).substr(2, 8),
        reconnectPeriod: 3000,
        connectTimeout: 30000,
        keepalive: 60,
        clean: true
    };
    
    client = mqtt.connect(MQTT_BROKER, options);
    
    client.on('connect', () => {
        console.log('✅ Connected to MQTT Broker');
        updateStatus('mqttStatus', 'Online', true);
        client.subscribe(MQTT_TOPIC);
        client.subscribe(MQTT_TOPIC_RELAY);
        console.log('📡 Subscribed to topics:');
        console.log('   - ' + MQTT_TOPIC);
        console.log('   - ' + MQTT_TOPIC_RELAY);
    });
    
    client.on('message', (topic, message) => {
        try {
            const data = JSON.parse(message.toString());
            
            if (topic === MQTT_TOPIC) {
                updateDashboard(data);
            } else if (topic === MQTT_TOPIC_RELAY) {
                updateRelayStatus(data);
            }
        } catch (e) {
            console.error('Error parsing JSON:', e);
        }
    });
    
    client.on('error', (err) => {
        console.error('MQTT Error:', err);
        updateStatus('mqttStatus', 'Error', false);
        setTimeout(connectMQTT, 5000);
    });
    
    client.on('close', () => {
        console.log('MQTT connection closed');
        updateStatus('mqttStatus', 'Disconnected', false);
        setTimeout(connectMQTT, 5000);
    });
    
    client.on('offline', () => {
        console.log('MQTT offline');
        updateStatus('mqttStatus', 'Offline', false);
    });
}

// ============ UPDATE STATUS ============
function updateStatus(elementId, text, isOnline) {
    const container = document.getElementById(elementId);
    if (!container) return;
    
    const dot = container.querySelector('.status-dot');
    const textEl = container.querySelector('span:last-child');
    
    if (dot) {
        dot.className = 'status-dot';
        if (isOnline) {
            dot.classList.add('online');
        } else {
            dot.classList.add('offline');
        }
    }
    
    if (textEl) {
        textEl.textContent = text;
    }
}

// ============ UPDATE DASHBOARD ============
function updateDashboard(data) {
    // Update values
    document.getElementById('voltage').textContent = data.voltage?.toFixed(1) || '0.0';
    document.getElementById('current').textContent = data.current?.toFixed(2) || '0.00';
    document.getElementById('power').textContent = data.power?.toFixed(1) || '0.0';
    document.getElementById('pf').textContent = data.pf?.toFixed(2) || '0.00';
    document.getElementById('energy').textContent = data.energy?.toFixed(3) || '0.000';
    document.getElementById('totalEnergy').textContent = data.totalEnergy?.toFixed(3) || '0.000';
    
    const cost = data.estimatedCost || 0;
    document.getElementById('cost').textContent = `Rp ${Math.round(cost).toLocaleString('id-ID')}`;
    
    // Update relay status
    if (data.relay1 !== undefined) updateRelayButton(1, data.relay1);
    if (data.relay2 !== undefined) updateRelayButton(2, data.relay2);
    if (data.relay3 !== undefined) updateRelayButton(3, data.relay3);
    
    // Update chart
    if (data.power !== undefined && data.power > 0) {
        addDataToChart(data.power);
    }
    
    // Update timestamp
    if (data.timestamp) {
        const date = new Date(data.timestamp * 1000);
        document.getElementById('lastUpdate').textContent = 
            `Last update: ${date.toLocaleTimeString('id-ID')}`;
    }
    
    updateCount++;
}

// ============ UPDATE RELAY BUTTON ============
function updateRelayButton(relayId, status) {
    const btn = document.getElementById(`relay${relayId}`);
    if (!btn) return;
    
    if (status) {
        btn.textContent = 'ON';
        btn.className = 'relay-btn on';
        btn.style.background = '#34d399';
        btn.style.color = '#064e3b';
    } else {
        btn.textContent = 'OFF';
        btn.className = 'relay-btn off';
        btn.style.background = '#ef4444';
        btn.style.color = '#7f1d1d';
    }
}

function updateRelayStatus(data) {
    const relay = data.relay;
    const status = data.status;
    
    if (relay >= 1 && relay <= 3) {
        updateRelayButton(relay, status);
        console.log(`🔄 Relay ${relay} -> ${status ? 'ON' : 'OFF'}`);
    }
}

// ============ CHART FUNCTIONS ============
function addDataToChart(power) {
    powerData.push(power);
    if (powerData.length > MAX_DATA_POINTS) {
        powerData.shift();
    }
    
    const now = new Date();
    const label = now.toLocaleTimeString('id-ID');
    
    powerChart.data.labels.push(label);
    powerChart.data.datasets[0].data.push(power);
    
    if (powerChart.data.labels.length > MAX_DATA_POINTS) {
        powerChart.data.labels.shift();
        powerChart.data.datasets[0].data.shift();
    }
    
    powerChart.update('none');
}

// ============ RELAY CONTROL ============
function controlRelay(relay, status) {
    if (!client || !client.connected) {
        alert('⚠️ MQTT tidak terhubung!');
        return;
    }
    
    const message = JSON.stringify({
        relay: relay,
        status: status
    });
    
    client.publish(MQTT_TOPIC_RELAY, message);
    console.log(`📤 Relay ${relay} -> ${status ? 'ON' : 'OFF'}`);
}

// ============ EVENT LISTENERS ============
document.addEventListener('DOMContentLoaded', () => {
    console.log('📊 PZEM Dashboard Loaded!');
    console.log('MQTT Broker: ' + MQTT_BROKER);
    console.log('Topics:');
    console.log('  - Data: ' + MQTT_TOPIC);
    console.log('  - Relay: ' + MQTT_TOPIC_RELAY);
    
    // Relay buttons
    document.querySelectorAll('.relay-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const relay = parseInt(btn.dataset.relay);
            const currentStatus = btn.textContent === 'ON';
            controlRelay(relay, !currentStatus);
        });
    });
    
    // All ON
    document.getElementById('allOn').addEventListener('click', () => {
        for (let i = 1; i <= 3; i++) {
            controlRelay(i, true);
        }
    });
    
    // All OFF
    document.getElementById('allOff').addEventListener('click', () => {
        for (let i = 1; i <= 3; i++) {
            controlRelay(i, false);
        }
    });
    
    // Connect MQTT
    connectMQTT();
});

// ============ RECONNECT ============
setInterval(() => {
    if (client && !client.connected) {
        console.log('🔄 Attempting to reconnect...');
        connectMQTT();
    }
}, 30000);

// ============ KEYBOARD SHORTCUTS ============
document.addEventListener('keydown', (e) => {
    if (e.key === '1') controlRelay(1, true);
    if (e.key === '!') controlRelay(1, false);
    if (e.key === '2') controlRelay(2, true);
    if (e.key === '@') controlRelay(2, false);
    if (e.key === '3') controlRelay(3, true);
    if (e.key === '#') controlRelay(3, false);
});

console.log('✅ Dashboard ready!');
