#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <WiFi.h>
#include <WebServer.h>

// ================= PENGATURAN WI-FI =================
const char* ssid = ""; // Masukan nama hotspot Anda     
const char* password = "";  // Masukan password hotspot Anda

WebServer server(80);

// ================= DEFINISI PIN =================
#define DHTPIN 23       
#define DHTTYPE DHT22   
#define PIRPIN 19       
#define LAMPUPIN 5     // Perubahan: Pin 5 untuk Relay Lampu (Disinkronkan)
#define RELAYPIN 17    // Pin 17 untuk Kipas (Active High)

#define SAKLAR_LAMPU_PIN 25  
#define SAKLAR_KIPAS_PIN 26  

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);

// ================= VARIABEL SISTEM =================
unsigned long waktuSekarang = 0;
unsigned long waktuTerakhirDHT = 0;
const unsigned long intervalDHT = 2000;
unsigned long waktuTerakhirLogGrafik = 0;
const unsigned long intervalLogGrafik = 5000; 

// Variabel Debounce Saklar
unsigned long waktuDebounceLampu = 0;
unsigned long waktuDebounceKipas = 0;
const unsigned long intervalDebounce = 50; 
int statusTerakhirSaklarLampu = HIGH;
int statusTerakhirSaklarKipas = HIGH;
int statusStabilSaklarLampu = HIGH;
int statusStabilSaklarKipas = HIGH;

// Variabel Filter PIR (TIDAK DIUBAH)
int hitunganPIR = 0;
const int ambangPIR = 50; 
unsigned long waktuTerakhirGerakan = 0;       
const unsigned long rentangTahanPIR = 4000;   

float suhu = 0.0;
float kelembapan = 0.0;
int statusGerak = 0;

float ambangSuhuPanas = 30.0; 
bool pirSensorEnabled = true; 
bool modeLampuAuto = true;  
bool modeKipasAuto = true;  
bool modeTvAuto = true; 
bool statusManualLampu = false;
bool statusManualKipas = false;
bool statusTV = false;
bool statusKunci = false;
bool oledEnabled = true;                       // Status ON/OFF Layar (Dikendalikan tombol Smart TV baru)

// ================= STORAGE UNTUK RIWAYAT GRAFIK =================
float hSuhu[10] = {0}, hKelembapan[10] = {0};
int hPir[10] = {0}, hTv[10] = {0}, hLampu[10] = {0}, hKipas[10] = {0};

void simpanDataGrafik() {
  for (int i = 0; i < 9; i++) {
    hSuhu[i] = hSuhu[i+1];
    hKelembapan[i] = hKelembapan[i+1];
    hPir[i] = hPir[i+1];
    hTv[i] = hTv[i+1];
    hLampu[i] = hLampu[i+1];
    hKipas[i] = hKipas[i+1];
  }
  hSuhu[9] = suhu;
  hKelembapan[9] = kelembapan;
  hPir[9] = statusGerak;
  hTv[9] = oledEnabled ? 1 : 0; // PERUBAHAN: Garis grafik TV sekarang membaca data status keaktifan Layar OLED
  hLampu[9] = (modeLampuAuto ? (statusGerak ? 1 : 0) : (statusManualLampu ? 1 : 0));
  hKipas[9] = digitalRead(RELAYPIN);
}

String logSistem[5] = {"Sistem dimulai...", "-", "-", "-", "-"};
void tambahLog(String pesan) { 
  for (int i = 4; i > 0; i--) { logSistem[i] = logSistem[i-1]; }
  logSistem[0] = pesan;
  Serial.println("[LOG] " + pesan);
}

// ================= HTML BINDING =================
const char* HALAMAN_WEB = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Smart Room IoT</title>
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@tabler/icons-webfont@latest/tabler-icons.min.css" />
  <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        .app { display: flex; min-height: 760px; font-family: var(--font-sans, sans-serif); background: #EEF2FF; }
        .main { flex: 1; overflow: hidden; }
        .sidebar { width: 220px; background: #1a3fa0; flex-shrink: 0; display: flex; flex-direction: column; }
        .sb-brand { padding: 22px 18px 18px; border-bottom: 1px solid rgba(255, 255, 255, 0.12); }
        .sb-logo { display: flex; align-items: center; gap: 10px; margin-bottom: 4px; }
        .sb-icon { width: 36px; height: 36px; background: rgba(255, 255, 255, 0.15); border-radius: 10px; display: flex; align-items: center; justify-content: center; color: white; font-size: 19px; }
        .sb-name { color: white; font-size: 16px; font-weight: 500; line-height: 1.15; }
        .sb-sub { color: rgba(255, 255, 255, 0.5); font-size: 11px; margin-top: 5px; }
        .sb-nav { padding: 14px 10px; flex: 1; }
        .nav-item { display: flex; align-items: center; gap: 9px; padding: 9px 11px; border-radius: 9px; color: rgba(255, 255, 255, 0.65); font-size: 13px; cursor: pointer; margin-bottom: 3px; transition: all 0.15s; }
        .nav-item:hover { background: rgba(255, 255, 255, 0.1); color: white; }
        .nav-item.active { background: white; color: #1a3fa0; font-weight: 500; }
        .nav-item i { font-size: 16px; }
        .sb-footer { padding: 14px; border-top: 1px solid rgba(255, 255, 255, 0.12); }
        .sb-status { display: flex; align-items: center; gap: 6px; font-size: 11px; color: rgba(255, 255, 255, 0.55); }
        .pulse { width: 7px; height: 7px; border-radius: 50%; background: #22c55e; animation: blink 1.4s infinite; }
        @keyframes blink { 0%, 100% { opacity: 1; } 50% { opacity: 0.3; } }
        .page { display: none; padding: 24px; }
        .page.active { display: block; }
        .ph { display: flex; align-items: flex-start; justify-content: space-between; margin-bottom: 20px; }
        .pt { font-size: 21px; font-weight: 500; color: #1a1a2e; }
        .ps { font-size: 12px; color: #777; margin-top: 3px; }
        .clock-box { background: #1a3fa0; color: white; border-radius: 10px; padding: 9px 16px; text-align: center; }
        .ct { font-size: 20px; font-weight: 500; letter-spacing: 1px; }
        .cd { font-size: 10px; opacity: 0.75; margin-top: 2px; }
        .g3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; margin-bottom: 14px; }
        .g2 { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; margin-bottom: 14px; }
        .scard { background: white; border-radius: 12px; padding: 16px; border: 0.5px solid #e8e8f4; }
        .sico { width: 42px; height: 42px; border-radius: 11px; display: flex; align-items: center; justify-content: center; font-size: 20px; margin-bottom: 10px; }
        .slbl { font-size: 11px; color: #888; margin-bottom: 3px; }
        .sval { font-size: 22px; font-weight: 500; color: #1a1a2e; line-height: 1; }
        .sbadge { display: inline-flex; align-items: center; gap: 4px; font-size: 11px; font-weight: 500; padding: 3px 9px; border-radius: 20px; margin-top: 6px; }
        .bg { background: #dcfce7; color: #15803d; }
        .br { background: #fee2e2; color: #b91c1c; }
        .bb { background: #dbeafe; color: #1d4ed8; }
        .by { background: #fff7ed; color: #c2410c; }
        .card { background: white; border-radius: 12px; padding: 18px; border: 0.5px solid #e8e8f4; margin-bottom: 12px; }
        .card-title { font-size: 13px; font-weight: 500; color: #1a1a2e; display: flex; align-items: center; gap: 6px; margin-bottom: 14px; }
        .pir-banner { border-radius: 10px; padding: 14px 16px; display: flex; align-items: center; justify-content: space-between; margin-bottom: 14px; }
        .pir-on { background: #f0fdf4; }
        .pir-off { background: #f9fafb; }
        .pir-title { font-size: 14px; font-weight: 500; }
        .pir-sub { font-size: 12px; margin-top: 3px; }
        .dev3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-bottom: 14px; }
        .devcard { background: white; border-radius: 12px; padding: 14px; border: 0.5px solid #e8e8f4; display: flex; flex-direction: column; align-items: center; text-align: center; gap: 6px; }
        .devcard-ico { width: 48px; height: 48px; border-radius: 14px; display: flex; align-items: center; justify-content: center; font-size: 24px; }
        .devcard-name { font-size: 13px; font-weight: 500; color: #1a1a2e; }
        .devcard-status { font-size: 11px; font-weight: 500; }
        .green { color: #16a34a; }
        .red { color: #dc2626; }
        .tgl { width: 42px; height: 23px; border-radius: 12px; border: none; cursor: pointer; position: relative; transition: background 0.2s; }
        .tgl.on { background: #22c55e; }
        .tgl.off { background: #d1d5db; }
        .tgl::after { content: ''; position: absolute; width: 17px; height: 17px; background: white; border-radius: 50%; top: 3px; transition: left 0.2s; }
        .tgl.on::after { left: 22px; }
        .tgl.off::after { left: 3px; }
        .tbl { width: 100%; border-collapse: collapse; font-size: 12px; }
        .tbl th { text-align: left; padding: 7px 10px; font-size: 10px; font-weight: 500; color: #999; border-bottom: 0.5px solid #eee; text-transform: uppercase; }
        .tbl td { padding: 9px 10px; border-bottom: 0.5px solid #f5f5fa; color: #1a1a2e; }
        .sett-row { display: flex; align-items: center; justify-content: space-between; padding: 13px 0; border-bottom: 0.5px solid #f0f0f8; }
        .sett-lbl { font-size: 13px; color: #1a1a2e; }
        .sett-sub { font-size: 11px; color: #999; margin-top: 2px; }
  </style>
</head>
<body>
<div class="app">
  <div class="sidebar">
    <div class="sb-brand">
      <div class="sb-logo">
        <div class="sb-icon"><i class="ti ti-home-bolt"></i></div>
        <div class="sb-name">Smart<br>Room</div>
      </div>
      <div class="sb-sub">Otomatisasi ruangan via IoT</div>
    </div>
    <div class="sb-nav">
      <div class="nav-item active" onclick="nav('dashboard', this)"><i class="ti ti-layout-dashboard"></i> Dashboard</div>
      <div class="nav-item" onclick="nav('perangkat', this)"><i class="ti ti-plug"></i> Perangkat</div>
      <div class="nav-item" onclick="nav('grafik', this)"><i class="ti ti-chart-line"></i> Grafik</div>
      <div class="nav-item" onclick="nav('pengaturan', this)"><i class="ti ti-settings"></i> Pengaturan</div>
    </div>
    <div class="sb-footer"><div class="sb-status"><span class="pulse"></span> ESP32 · Server Aktif</div></div>
  </div>

  <div class="main">
    <div class="page active" id="p-dashboard">
      <div class="ph">
        <div>
          <div class="pt">Dashboard Smart Room</div>
          <div class="ps">Monitoring otomatis TV, lampu & kipas via sensor PIR</div>
        </div>
        <div class="clock-box"><div class="ct" id="clock">--:--:--</div><div class="cd" id="cdate">-</div></div>
      </div>

      <div class="pir-banner pir-on" id="pir-banner">
        <div>
          <div class="pir-title" style="color:#15803d"><i class="ti ti-run"></i> Gerakan terdeteksi</div>
          <div class="pir-sub" style="color:#16a34a">Sensor PIR aktif · Otomatisasi aktif</div>
        </div>
        <span class="sbadge bg" style="font-size:12px;padding:5px 14px">PIR AKTIF</span>
      </div>

      <div class="dev3">
        <div class="devcard" id="dc-tv">
          <div class="devcard-ico" style="background:#dbeafe"><i class="ti ti-device-tv" style="color:#1d4ed8"></i></div>
          <div class="devcard-name">Smart TV (OLED)</div>
          <div class="devcard-status green" id="dc-tv-s">ON</div>
          <span class="sbadge bg" id="dc-tv-b">Menyala</span>
        </div>
        <div class="devcard" id="dc-lmp">
          <div class="devcard-ico" style="background:#fefce8"><i class="ti ti-bulb" style="color:#ca8a04"></i></div>
          <div class="devcard-name">Lampu</div>
          <div class="devcard-status green" id="dc-lmp-s">ON</div>
          <span class="sbadge bg" id="dc-lmp-b">Menyala</span>
        </div>
        <div class="devcard" id="dc-fan">
          <div class="devcard-ico" style="background:#f0fdf4"><i class="ti ti-wind" style="color:#16a34a"></i></div>
          <div class="devcard-name">Kipas</div>
          <div class="devcard-status green" id="dc-fan-s">ON</div>
          <span class="sbadge bg" id="dc-fan-b">Menyala</span>
        </div>
      </div>

      <div class="g3">
        <div class="scard"><div class="slbl">Suhu ruangan</div><div class="sval" id="suhu-v">0.0<span style="font-size:14px;font-weight:400;color:#888"> °C</span></div><span class="sbadge bg">Normal</span></div>
        <div class="scard"><div class="slbl">Kelembaban</div><div class="sval" id="hum-v">0<span style="font-size:14px;font-weight:400;color:#888"> %</span></div><span class="sbadge bb">DHT22</span></div>
        <div class="scard"><div class="slbl">Sistem</div><div class="sval" style="color:#1a3fa0;font-size:16px;padding-top:5px;">ONLINE</div><span class="sbadge by">ESP32 Status</span></div>
      </div>

      <div class="card">
        <div class="card-title"><i class="ti ti-chart-bar"></i> Aktivitas lampu terakhir</div>
        <div style="position:relative;height:140px"><canvas id="chart-dash"></canvas></div>
      </div>
    </div>

    <div class="page" id="p-perangkat">
      <div class="ph"><div><div class="pt">Kontrol perangkat</div><div class="ps">Monitoring dan kontrol manual/otomatis</div></div></div>
      <div class="card">
        <div class="card-title"><i class="ti ti-plug"></i> Daftar perangkat</div>
        
        <div class="dev-row" style="display:flex; justify-content:space-between; align-items:center; padding:10px 0; border-bottom:1px solid #eee;">
          <div><div class="dev-name">Smart TV</div><div class="dev-sub" id="p-tv-sub">Status: --</div></div>
          <button class="tgl on" id="p-tv" onclick="togDev(this)"></button>
        </div>
        
        <div class="dev-row" style="display:flex; justify-content:space-between; align-items:center; padding:10px 0; border-bottom:1px solid #eee;">
          <div><div class="dev-name">Lampu</div><div class="dev-sub" id="p-lmp-sub">Status: --</div></div>
          <button class="tgl on" id="p-lmp" onclick="togDev(this)"></button>
        </div>
        <div class="dev-row" style="display:flex; justify-content:space-between; align-items:center; padding:10px 0; border-bottom:1px solid #eee;">
          <div><div class="dev-name">Kipas</div><div class="dev-sub" id="p-fan-sub">Status: --</div></div>
          <button class="tgl on" id="p-fan" onclick="togDev(this)"></button>
        </div>
        <div class="dev-row" style="display:flex; justify-content:space-between; align-items:center; padding:10px 0;">
          <div><div class="dev-name">Kunci Pintu</div><div class="dev-sub" id="p-lock-sub">Status: --</div></div>
          <button class="tgl on" id="p-lock" onclick="togDev(this)"></button>
        </div>
      </div>
    </div>

    <div class="page" id="p-grafik">
      <div class="ph"><div><div class="pt">Grafik monitoring</div><div class="ps">Monitoring realtime data sensor & alat</div></div></div>
      <div class="card"><div class="card-title"><i class="ti ti-device-tv"></i> Aktivitas Perangkat</div><div style="position:relative;height:160px"><canvas id="g-multi"></canvas></div></div>
      <div class="g2">
        <div class="card"><div class="card-title"><i class="ti ti-run"></i> Deteksi PIR</div><div style="position:relative;height:120px"><canvas id="g-pir"></canvas></div></div>
        <div class="card"><div class="card-title"><i class="ti ti-temperature"></i> Lingkungan</div><div style="position:relative;height:120px"><canvas id="g-suhu"></canvas></div></div>
      </div>
    </div>

    <div class="page" id="p-pengaturan">
      <div class="ph"><div><div class="pt">Pengaturan</div><div class="ps">Konfigurasi otomatisasi sistem IoT</div></div></div>
      <div class="card">
        <div class="card-title"><i class="ti ti-clock"></i> Otomatisasi Perangkat</div>
        <div class="sett-row">
          <div><div class="sett-lbl">Master Sensor PIR</div><div class="sett-sub">Aktifkan atau Matikan kerja seluruh Sensor PIR</div></div>
          <button class="tgl on" id="sett-pir-master" onclick="togPirMaster(this)"></button>
        </div>
        <div class="sett-row">
          <div><div class="sett-lbl">Mode otomatis — Lampu</div><div class="sett-sub">Lampu menyala mengikuti sensor PIR</div></div>
          <button class="tgl on" id="sett-lamp-auto" onclick="togLampAuto(this)"></button>
        </div>
        <div class="sett-row">
          <div><div class="sett-lbl">Mode otomatis — TV</div><div class="sett-sub">TV menyala mengikuti sensor PIR</div></div>
          <button class="tgl on" id="sett-tv-auto" onclick="togTvAuto(this)"></button>
        </div>
        <div class="sett-row">
          <div><div class="sett-lbl">Mode otomatis — Kipas</div><div class="sett-sub">Kipas menyala jika suhu melewati batas panas</div></div>
          <button class="tgl on" id="sett-fan-auto" onclick="togFanAuto(this)"></button>
        </div>
      </div>
    </div>
  </div>
</div>

<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.js"></script>
<script>
function nav(page, el) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
  document.getElementById('p-' + page).classList.add('active');
  el.classList.add('active');
  if (page === 'grafik') setTimeout(initCharts, 60);
}

function togDev(btn) {
  btn.classList.toggle('on'); btn.classList.toggle('off');
  const state = btn.classList.contains('on') ? "on" : "off";
  if (btn.id === "p-tv") fetch(`/oled/${state}`); // PERUBAHAN: Tombol p-tv sekarang mengirim request /oled/on atau /oled/off
  else if (btn.id === "p-lmp") fetch(`/lamp/${state}`);
  else if (btn.id === "p-fan") fetch(`/fan/${state}`);
  else if (btn.id === "p-lock") fetch(`/lock/${state}`);
}

function togPirMaster(btn) { btn.classList.toggle('on'); btn.classList.toggle('off'); fetch(`/pir/master/${btn.classList.contains('on')?"on":"off"}`); }
if(document.getElementById('sett-lamp-auto')) {
  function togLampAuto(btn) { btn.classList.toggle('on'); btn.classList.toggle('off'); fetch(`/lamp/auto/${btn.classList.contains('on')?"on":"off"}`); }
  function togTvAuto(btn) { btn.classList.toggle('on'); btn.classList.toggle('off'); fetch(`/tv/auto/${btn.classList.contains('on')?"on":"off"}`); }
  function togFanAuto(btn) { btn.classList.toggle('on'); btn.classList.toggle('off'); fetch(`/fan/auto/${btn.classList.contains('on')?"on":"off"}`); }
}

function updatePIRVisual(pirActive, pirMaster) {
  const b1 = document.getElementById('pir-banner');
  if (!b1) return;
  if (!pirMaster) {
    b1.className = 'pir-banner pir-off';
    b1.innerHTML = `<div><div class="pir-title" style="color:#dc2626"><i class="ti ti-eye-off"></i> Sensor PIR Dimatikan</div><div class="pir-sub" style="color:#ef4444">Fungsi deteksi gerakan tidak aktif</div></div><span class="sbadge br" style="background:#fee2e2; color:#b91c1c">MATI</span>`;
    return;
  }
  if (pirActive == 1) {
    b1.className = 'pir-banner pir-on';
    b1.innerHTML = `<div><div class="pir-title" style="color:#15803d"><i class="ti ti-run"></i> Gerakan Terdeteksi</div><div class="pir-sub" style="color:#16a34a">Sistem merespon secara realtime</div></div><span class="sbadge bg">PIR AKTIF</span>`;
  } else {
    b1.className = 'pir-banner pir-off';
    b1.innerHTML = `<div><div class="pir-title" style="color:#6b7280"><i class="ti ti-walk"></i> Tidak Ada Gerakan</div><div class="pir-sub" style="color:#9ca3af">Kondisi ruangan kosong/sepi</div></div><span class="sbadge br">PIR IDLE</span>`;
  }
}

function updateDeviceUI(oledStatus, lampStatus, fanStatus, lockStatus, lampAuto, tvAuto, fanAuto, pirMaster) {
  // PERUBAHAN: dc-tv (di dashboard) sekarang mengikuti status oledStatus (Layar OLED)
  document.getElementById('dc-tv-s').textContent = oledStatus ? 'ON' : 'OFF';
  document.getElementById('dc-tv-b').textContent = oledStatus ? 'Menyala' : 'Mati';
  document.getElementById('dc-tv-b').className = 'sbadge ' + (oledStatus ? 'bg' : 'br');
  document.getElementById('p-tv').className = 'tgl ' + (oledStatus ? 'on' : 'off');
  document.getElementById('p-tv-sub').textContent = `Status: ${oledStatus ? 'ON' : 'OFF'} · ${tvAuto ? 'Otomatis (PIR)' : 'Manual'}`;

  document.getElementById('dc-lmp-s').textContent = lampStatus ? 'ON' : 'OFF';
  document.getElementById('dc-lmp-b').textContent = lampStatus ? 'Menyala' : 'Mati';
  document.getElementById('dc-lmp-b').className = 'sbadge ' + (lampStatus ? 'bg' : 'br');
  document.getElementById('p-lmp').className = 'tgl ' + (lampStatus ? 'on' : 'off');
  document.getElementById('p-lmp-sub').textContent = `Status: ${lampStatus ? 'ON' : 'OFF'} · ${lampAuto ? 'Otomatis (PIR)' : 'Manual'}`;

  document.getElementById('dc-fan-s').textContent = fanStatus ? 'ON' : 'OFF';
  document.getElementById('dc-fan-b').textContent = fanStatus ? 'Menyala' : 'Mati';
  document.getElementById('dc-fan-b').className = 'sbadge ' + (fanStatus ? 'bg' : 'br');
  document.getElementById('p-fan').className = 'tgl ' + (fanStatus ? 'on' : 'off');
  document.getElementById('p-fan-sub').textContent = `Status: ${fanStatus ? 'ON' : 'OFF'} · ${fanAuto ? 'Otomatis (Suhu)' : 'Manual'}`;

  document.getElementById('p-lock').className = 'tgl ' + (lockStatus ? 'on' : 'off');
  document.getElementById('p-lock-sub').textContent = `Pintu: ${lockStatus ? 'TERKUNCI' : 'TERBUKA'}`;

  const setP = document.getElementById('sett-pir-master'); if(setP) setP.className = 'tgl ' + (pirMaster?'on':'off');
  const setL = document.getElementById('sett-lamp-auto'); if(setL) setL.className = 'tgl ' + (lampAuto?'on':'off');
  const setT = document.getElementById('sett-tv-auto'); if(setT) setT.className = 'tgl ' + (tvAuto?'on':'off');
  const setF = document.getElementById('sett-fan-auto'); if(setF) setF.className = 'tgl ' + (fanAuto?'on':'off');
}

function updateClock() {
  const now = new Date();
  document.getElementById('clock').textContent = now.getHours().toString().padStart(2, '0') + ":" + now.getMinutes().toString().padStart(2, '0') + ":" + now.getSeconds().toString().padStart(2, '0');
}
setInterval(updateClock, 1000);

async function updateSensor() {
  try {
    const res = await fetch('/sensor');
    const data = await res.json();

    if (document.getElementById('suhu-v')) document.getElementById('suhu-v').innerHTML = `${data.suhu} °C`;
    if (document.getElementById('hum-v')) document.getElementById('hum-v').innerHTML = `${data.kelembapan} %`;

    updatePIRVisual(data.pir, data.pirMaster);
    // PERUBAHAN: parameter pertama mengoper data.oled ke fungsi updateDeviceUI
    updateDeviceUI(data.oled, data.lampu, data.kipas, data.kunci, data.modeLampuAuto, data.modeTvAuto, data.modeKipasAuto, data.pirMaster);

    if (data.history) { updateChartData(data.history); updateMiniChart(data.history); }
  } catch(err) { console.log("Gagal mengambil data sensor:", err); }
}
setInterval(updateSensor, 2000);

let chartsInited = false, chartMulti, chartPir, chartSuhu, miniChart;
function initCharts() {
  if (chartsInited) return; chartsInited = true;
  const labels = ['1','2','3','4','5','6','7','8','9','10'];
  // PERUBAHAN: Judul legend tetap 'TV' sesuai permintaan Anda, namun data yang masuk nantinya adalah data status Layar OLED
  chartMulti = new Chart(document.getElementById('g-multi'), { type: 'line', data: { labels, datasets: [{ label: 'TV', borderColor: '#3b82f6' }, { label: 'Lampu', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#facc15' }, { label: 'Kipas', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#22c55e' }] }, options: { responsive: true, maintainAspectRatio: false } });
  chartPir = new Chart(document.getElementById('g-pir'), { type: 'bar', data: { labels, datasets: [{ label: 'Gerakan', data: [0,0,0,0,0,0,0,0,0,0], backgroundColor: '#8b5cf6' }] }, options: { responsive: true, maintainAspectRatio: false } });
  chartSuhu = new Chart(document.getElementById('g-suhu'), { type: 'line', data: { labels, datasets: [{ label: 'Suhu', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#f97316' }, { label: 'Kelembaban', data: [0,0,0,0,0,0,0,0,0,0], borderColor: '#3b82f6' }] }, options: { responsive: true, maintainAspectRatio: false } });
}
function updateChartData(history) {
  if (!chartsInited) return;
  chartMulti.data.datasets[0].data = history.tv; chartMulti.data.datasets[1].data = history.lampu; chartMulti.data.datasets[2].data = history.kipas; chartMulti.update();
  chartPir.data.datasets[0].data = history.pir; chartPir.update();
  chartSuhu.data.datasets[0].data = history.suhu; chartSuhu.data.datasets[1].data = history.kelembapan; chartSuhu.update();
}
setTimeout(() => { miniChart = new Chart(document.getElementById('chart-dash'), { type: 'bar', data: { labels: ['1','2','3','4','5','6','7','8','9','10'], datasets: [{label:'Aktivitas Lampu', data:[0,0,0,0,0,0,0,0,0,0], backgroundColor:'#facc15'}] }, options: { responsive: true, maintainAspectRatio: false } }); }, 500);
function updateMiniChart(history) { if (miniChart) { miniChart.data.datasets[0].data = history.lampu; miniChart.update(); } }
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", HALAMAN_WEB);
}

void setupWebServer() {
  server.on("/", handleRoot);

  server.on("/sensor", []() {
    bool lampuSkorNyata = modeLampuAuto ? (statusGerak == HIGH) : statusManualLampu;
    
    String json = "{";
    json += "\"suhu\":" + String(suhu, 1) + ",";
    json += "\"kelembapan\":" + String(kelembapan, 0) + ",";
    json += "\"pir\":" + String(statusGerak) + ",";
    json += "\"lampu\":" + String(lampuSkorNyata ? "true" : "false") + ",";
    
    json += "\"modeLampuAuto\":" + String(modeLampuAuto ? "true" : "false") + ",";
    json += "\"modeTvAuto\":" + String(modeTvAuto ? "true" : "false") + ",";
    json += "\"modeKipasAuto\":" + String(modeKipasAuto ? "true" : "false") + ",";
    json += "\"pirMaster\":" + String(pirSensorEnabled ? "true" : "false") + ","; 
    json += "\"oled\":" + String(oledEnabled ? "true" : "false") + ","; 
    
    json += "\"kipas\":" + String(digitalRead(RELAYPIN) ? "true" : "false") + ",";
    json += "\"kunci\":" + String(statusKunci ? "true" : "false") + ",";
    
    json += "\"history\":{";
    json += "\"suhu\":["; for(int i=0; i<10; i++) { json += String(hSuhu[i], 1) + (i==9 ? "" : ","); }
    json += "],\"kelembapan\":["; for(int i=0; i<10; i++) { json += String(hKelembapan[i], 0) + (i==9 ? "" : ","); }
    json += "],\"pir\":["; for(int i=0; i<10; i++) { json += String(hPir[i]) + (i==9 ? "" : ","); }
    json += "],\"tv\":["; for(int i=0; i<10; i++) { json += String(hTv[i]) + (i==9 ? "" : ","); } // Riwayat TV diisi status oled dari simpanDataGrafik()
    json += "],\"lampu\":["; for(int i=0; i<10; i++) { json += String(hLampu[i]) + (i==9 ? "" : ","); }
    json += "],\"kipas\":["; for(int i=0; i<10; i++) { json += String(hKipas[i]) + (i==9 ? "" : ","); }
    json += "]}";
    json += "}";

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.on("/pir/master/on", []() { pirSensorEnabled = true; tambahLog("PIR Master: AKTIF"); server.send(200, "text/plain", "OK"); });
  server.on("/pir/master/off", []() { pirSensorEnabled = false; statusGerak = 0; hitunganPIR = 0; tambahLog("PIR Master: MATI"); server.send(200, "text/plain", "OK"); });

  server.on("/lamp/on", []() { modeLampuAuto = false; statusManualLampu = true; tambahLog("Lampu: MANUAL ON"); server.send(200, "text/plain", "OK"); });
  server.on("/lamp/off", []() { modeLampuAuto = false; statusManualLampu = false; tambahLog("Lampu: MANUAL OFF"); server.send(200, "text/plain", "OK"); });
  server.on("/lamp/auto/on", []() { modeLampuAuto = true; tambahLog("Lampu: PIR AUTO ON"); server.send(200, "text/plain", "OK"); });
  server.on("/lamp/auto/off", []() { modeLampuAuto = false; tambahLog("Lampu: PIR AUTO OFF"); server.send(200, "text/plain", "OK"); });

  server.on("/tv/auto/on", []() { modeTvAuto = true; tambahLog("Smart TV: AUTO ON"); server.send(200, "text/plain", "OK"); });
  server.on("/tv/auto/off", []() { modeTvAuto = false; tambahLog("Smart TV: AUTO OFF"); server.send(200, "text/plain", "OK"); });

  server.on("/fan/on", []() { modeKipasAuto = false; statusManualKipas = true; tambahLog("Kipas: MANUAL ON"); server.send(200, "text/plain", "OK"); });
  server.on("/fan/off", []() { modeKipasAuto = false; statusManualKipas = false; tambahLog("Kipas: MANUAL OFF"); server.send(200, "text/plain", "OK"); });
  server.on("/fan/auto/on", []() { modeKipasAuto = true; tambahLog("Kipas: TEMP AUTO ON"); server.send(200, "text/plain", "OK"); });
  server.on("/fan/auto/off", []() { modeKipasAuto = false; tambahLog("Kipas: TEMP AUTO OFF"); server.send(200, "text/plain", "OK"); });

  server.on("/lock/on", []() { statusKunci = true; tambahLog("Pintu: DIKUNCI"); server.send(200, "text/plain", "OK"); });
  server.on("/lock/off", []() { statusKunci = false; tambahLog("Pintu: DIBUKA"); server.send(200, "text/plain", "OK"); });

  // PERUBAHAN: Endpoint oled sekarang dipetakan langsung dari tombol "Smart TV" di web halaman perangkat
  server.on("/oled/on", []() { oledEnabled = true; display.ssd1306_command(SSD1306_DISPLAYON); tambahLog("Layar OLED: DIHIDUPKAN"); server.send(200, "text/plain", "OK"); });
  server.on("/oled/off", []() { oledEnabled = false; display.ssd1306_command(SSD1306_DISPLAYOFF); tambahLog("Layar OLED: DIMATIKAN"); server.send(200, "text/plain", "OK"); });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED Gagal Ditemukan!"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SMART ROOM IoT");
  display.println("---------------------");
  display.println("Menghubungkan Wi-Fi...");
  display.display();

  dht.begin();
  pinMode(PIRPIN, INPUT);
  
  // Setup Relay Kipas (Pin 17)
  pinMode(RELAYPIN, OUTPUT);
  digitalWrite(RELAYPIN, LOW);

  // Setup Relay Lampu (Pin 5)
  pinMode(LAMPUPIN, OUTPUT);
  digitalWrite(LAMPUPIN, LOW); 

  pinMode(SAKLAR_LAMPU_PIN, INPUT_PULLUP);
  pinMode(SAKLAR_KIPAS_PIN, INPUT_PULLUP);

  WiFi.begin(ssid, password);
  int percobaan = 0;
  
  while (WiFi.status() != WL_CONNECTED && percobaan < 20) { 
    delay(500); 
    Serial.print("."); 
    display.print(".");
    display.display();
    percobaan++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    setupWebServer();
    tambahLog("Sistem Connected.");
    
    Serial.println("\n[INFO] Wi-Fi Berhasil Terhubung!");
    Serial.print("[INFO] Alamat IP ESP32: ");
    Serial.println(WiFi.localIP()); 

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WI-FI CONNECTED!");
    display.println("---------------------");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
    delay(3000); 
  } else {
    Serial.println("\nWi-Fi Gagal Terhubung! Sistem Offline.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WI-FI ERROR!");
    display.println("---------------------");
    display.println("Gagal Terhubung.");
    display.println("Mode Offline Aktif.");
    display.display();
    delay(3000);
  }
}

void loop() {
  waktuSekarang = millis();
  
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  // 1. LOGIKA DEBOUNCE SAKLAR MANUAL FISIK (LAMPU)
  int bacaSaklarLampu = digitalRead(SAKLAR_LAMPU_PIN);
  if (bacaSaklarLampu != statusTerakhirSaklarLampu) {
    waktuDebounceLampu = waktuSekarang;
  }
  if ((waktuSekarang - waktuDebounceLampu) > intervalDebounce) {
    if (bacaSaklarLampu != statusStabilSaklarLampu) {
      statusStabilSaklarLampu = bacaSaklarLampu;
      if (statusStabilSaklarLampu == LOW) { 
        modeLampuAuto = false; 
        statusManualLampu = !statusManualLampu;
        tambahLog("Saklar Lampu Fisik Ditekan");
      }
    }
  }
  statusTerakhirSaklarLampu = bacaSaklarLampu;

  // 2. LOGIKA DEBOUNCE SAKLAR MANUAL FISIK (KIPAS)
  int bacaSaklarKipas = digitalRead(SAKLAR_KIPAS_PIN);
  if (bacaSaklarKipas != statusTerakhirSaklarKipas) {
    waktuDebounceKipas = waktuSekarang;
  }
  if ((waktuSekarang - waktuDebounceKipas) > intervalDebounce) {
    if (bacaSaklarKipas != statusStabilSaklarKipas) {
      statusStabilSaklarKipas = bacaSaklarKipas;
      if (statusStabilSaklarKipas == LOW) { 
        modeKipasAuto = false;
        statusManualKipas = !statusManualKipas;
        tambahLog("Saklar Kipas Fisik Ditekan");
      }
    }
  }
  statusTerakhirSaklarKipas = bacaSaklarKipas;

  // 3. PEMBACAAN SENSOR DHT22 (SETIAP 2 DETIK)
  if (waktuSekarang - waktuTerakhirDHT >= intervalDHT) {
    waktuTerakhirDHT = waktuSekarang;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      suhu = t;
      kelembapan = h;
    }
  }

  // 4. PEMBACAAN SENSOR PIR (DIOPTIMALKAN UNTUK GERAKAN MINTA/LAMBAIAN - TIDAK DIUBAH)
  if (pirSensorEnabled) {
    int bacaPIR = digitalRead(PIRPIN);
    if (bacaPIR == HIGH) {
      waktuTerakhirGerakan = waktuSekarang; 
      
      hitunganPIR += 5; 
      if (hitunganPIR > ambangPIR) {
        hitunganPIR = ambangPIR;
        if (statusGerak == 0) {
          statusGerak = 1;
          tambahLog("Gerakan Terdeteksi");
        }
      }
    } else {
      if (waktuSekarang - waktuTerakhirGerakan >= rentangTahanPIR) {
        hitunganPIR -= 10; 
        if (hitunganPIR < 0) {
          hitunganPIR = 0;
          if (statusGerak == 1) {
            statusGerak = 0;
            tambahLog("Ruangan Sepi");
          }
        }
      }
    }
  } else {
    statusGerak = 0;
    hitunganPIR = 0;
  }

  // 5. LOGIKA OUTPUT AKTUAL - LAMPU
  bool lampuHarusNyala = modeLampuAuto ? (statusGerak == 1) : statusManualLampu;
  if (lampuHarusNyala) {
    digitalWrite(LAMPUPIN, HIGH);  
  } else {
    digitalWrite(LAMPUPIN, LOW);   
  }

  // 6. LOGIKA OUTPUT AKTUAL - KIPAS
  bool kipasHarusNyala = modeKipasAuto ? (suhu >= ambangSuhuPanas) : statusManualKipas;
  if (kipasHarusNyala) {
    digitalWrite(RELAYPIN, HIGH); 
  } else {
    digitalWrite(RELAYPIN, LOW);  
  }

  // 7. LOGIKA OTOMATISASI LAYAR/TV LEWAT MODE PIR TV AUTO
  if (modeTvAuto) {
    if (statusGerak == 1 && !oledEnabled) {
      oledEnabled = true;
      display.ssd1306_command(SSD1306_DISPLAYON);
    } else if (statusGerak == 0 && oledEnabled) {
      oledEnabled = false;
      display.ssd1306_command(SSD1306_DISPLAYOFF);
    }
  }

  // 8. LOG LOGIK GRAFIK (SETIAP 5 DETIK)
  if (waktuSekarang - waktuTerakhirLogGrafik >= intervalLogGrafik) {
    waktuTerakhirLogGrafik = waktuSekarang;
    simpanDataGrafik();
  }

  // 9. REFRESH TAMPILAN OLED REALTIME (Hanya berjalan jika oledEnabled == true)
  if (oledEnabled) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("--- SMART ROOM ---");
    display.print("Ip: "); display.print(WiFi.localIP()); display.println();
    display.print("Suhu: "); display.print(suhu, 1); display.println(" C");
    display.print("Hum : "); display.print(kelembapan, 0); display.println(" %");
    display.print("PIR : "); display.println(statusGerak == 1 ? "GERAKAN" : "TIDAK ADA");
    display.print("Lampu: "); display.println(lampuHarusNyala ? "HIDUP" : "MATI");
    display.print("Kipas: "); display.println(kipasHarusNyala ? "HIDUP" : "MATI");
    display.display();
  }

  delay(20); 
}