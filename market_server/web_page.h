#pragma once
// web_page.h — 内嵌管理页面 HTML (3个Tab)
#include <string>
#include "web_server.h"

inline std::string WebServer::GetPageHtml() {
    return R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>行情管理端</title>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/@picocss/pico@2/css/pico.min.css">
<style>
  :root { --pico-font-size: 14px; }
  body { padding: 1rem; }
  .tabs { display: flex; gap: 0; margin-bottom: 1rem; }
  .tabs button { flex: 1; padding: 0.6rem; cursor: pointer; border: 1px solid var(--pico-muted-border-color);
                 background: var(--pico-card-background-color); }
  .tabs button.active { background: var(--pico-primary); color: #fff; }
  .tab-panel { display: none; }
  .tab-panel.active { display: block; }
  table { font-size: 12px; }
  .status { padding: 0.5rem; margin: 0.5rem 0; border-radius: 4px; }
  .status.ok { background: #d4edda; color: #155724; }
  .status.err { background: #f8d7da; color: #721c24; }
  .gap-row { background: #fff3cd !important; }
</style>
</head>
<body>
<h3>📊 币安行情管理端</h3>

<div class="tabs">
  <button class="active" onclick="switchTab(0)">爬取历史数据</button>
  <button onclick="switchTab(1)">共享内存缓存</button>
  <button onclick="switchTab(2)">数据库查询</button>
</div>

<!-- Tab 1: 爬取历史数据 -->
<div class="tab-panel active" id="tab0">
  <form onsubmit="doCrawl(event)">
    <div class="grid">
      <label>币种 <input id="c_pair" value="BTC/USDT" required></label>
      <label>周期 <select id="c_tf"><option>1m</option><option>5m</option></select></label>
    </div>
    <div class="grid">
      <label>开始日期 <input type="date" id="c_start" required></label>
      <label>结束日期 <input type="date" id="c_end" required></label>
    </div>
    <button type="submit">开始爬取</button>
  </form>
  <div id="crawl_status"></div>
</div>

<!-- Tab 2: 共享内存缓存 -->
<div class="tab-panel" id="tab1">
  <div class="grid">
    <label>币种(可选) <input id="s_pair" placeholder="留空查全部"></label>
    <label>条数 <input type="number" id="s_count" value="50"></label>
    <div><button onclick="loadShm()">刷新</button></div>
  </div>
  <div style="overflow-x:auto"><table id="shm_table"><thead><tr>
    <th>时间(北京)</th><th>币种</th><th>周期</th><th>开</th><th>高</th><th>低</th>
    <th>收</th><th>成交量</th><th>成交额</th><th>涨跌%</th><th>不连续</th>
  </tr></thead><tbody></tbody></table></div>
</div>

<!-- Tab 3: 数据库查询 -->
<div class="tab-panel" id="tab2">
  <div class="grid">
    <label>币种 <input id="d_pair" value="BTC/USDT" required></label>
    <label>周期 <select id="d_tf"><option>1m</option><option>5m</option></select></label>
  </div>
  <div class="grid">
    <label>开始日期 <input type="date" id="d_start" required></label>
    <label>结束日期 <input type="date" id="d_end" required></label>
    <div><button onclick="loadDb()">查询</button></div>
  </div>
  <div id="db_count"></div>
  <div style="overflow-x:auto"><table id="db_table"><thead><tr>
    <th>时间(北京)</th><th>币种</th><th>周期</th><th>开</th><th>高</th><th>低</th>
    <th>收</th><th>成交量</th><th>成交额</th><th>涨跌%</th>
  </tr></thead><tbody></tbody></table></div>
</div>

<script>
function switchTab(i) {
  document.querySelectorAll('.tab-panel').forEach((p,k) => {
    p.classList.toggle('active', k===i);
  });
  document.querySelectorAll('.tabs button').forEach((b,k) => {
    b.classList.toggle('active', k===i);
  });
}

function doCrawl(e) {
  e.preventDefault();
  const st = document.getElementById('crawl_status');
  st.className='status'; st.textContent='提交中...';
  fetch('/api/crawl', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({
      pair: document.getElementById('c_pair').value,
      timeframe: document.getElementById('c_tf').value,
      start: document.getElementById('c_start').value,
      end: document.getElementById('c_end').value
    })
  }).then(r=>r.json()).then(d=>{
    st.className='status ok';
    st.textContent='任务已提交: '+JSON.stringify(d);
  }).catch(err=>{
    st.className='status err';
    st.textContent='错误: '+err;
  });
}

function fillTable(tableId, rows, showGap) {
  const tb = document.querySelector('#'+tableId+' tbody');
  tb.innerHTML = '';
  rows.forEach(r => {
    const tr = document.createElement('tr');
    if (showGap && r.gap) tr.className='gap-row';
    const cols = [r.time, r.pair, r.tf,
                  r.open, r.high, r.low, r.close,
                  r.volume?.toFixed(2), r.amount?.toFixed(2),
                  r.change?.toFixed(4)];
    if (showGap) cols.push(r.gap ? '⚠️' : '');
    cols.forEach(v => { const td=document.createElement('td'); td.textContent=v; tr.appendChild(td); });
    tb.appendChild(tr);
  });
}

function loadShm() {
  let url = '/api/shm?count='+document.getElementById('s_count').value;
  const p = document.getElementById('s_pair').value;
  if (p) url += '&pair='+encodeURIComponent(p);
  fetch(url).then(r=>r.json()).then(d => fillTable('shm_table', d, true));
}

function loadDb() {
  const params = new URLSearchParams({
    pair: document.getElementById('d_pair').value,
    timeframe: document.getElementById('d_tf').value,
    start: document.getElementById('d_start').value,
    end: document.getElementById('d_end').value
  });
  fetch('/api/db?'+params).then(r=>r.json()).then(d => {
    document.getElementById('db_count').textContent = '查询到 '+d.length+' 条记录';
    fillTable('db_table', d, false);
  });
}
</script>
</body>
</html>
)HTML";
}
