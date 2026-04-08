#pragma once
// html_report_html_parts.h — HTML 报告的 CSS/JS 模板片段
// 被 html_report_dll_main.cpp 包含，避免单文件过长

#include <string>

namespace report_html {

// ============================================================================
// CSS 样式
// ============================================================================
inline const char* GetCssStyle() {
    return R"CSS(
<style>
:root {
    --bg: #f8f9fa; --card-bg: #ffffff; --text: #212529;
    --border: #dee2e6; --primary: #0d6efd; --success: #198754;
    --danger: #dc3545; --warning: #ffc107; --info: #0dcaf0;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
       background: var(--bg); color: var(--text); padding: 20px; }
.container { max-width: 1600px; margin: 0 auto; }
.header { text-align: center; padding: 20px; margin-bottom: 20px;
          background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
          color: white; border-radius: 12px; }
.header h1 { font-size: 24px; margin-bottom: 8px; }
.header .subtitle { font-size: 14px; opacity: 0.9; }
.tabs { display: flex; gap: 0; margin-bottom: 0; border-bottom: 2px solid var(--border);
        background: var(--card-bg); border-radius: 8px 8px 0 0; overflow: hidden; }
.tabs button { flex: 1; padding: 12px 16px; cursor: pointer; border: none;
               background: transparent; font-size: 14px; font-weight: 500;
               color: #6c757d; transition: all 0.2s; }
.tabs button:hover { background: #f0f0f0; }
.tabs button.active { color: var(--primary); border-bottom: 3px solid var(--primary);
                      background: var(--card-bg); }
.tab-panel { display: none; background: var(--card-bg); padding: 20px;
             border-radius: 0 0 8px 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.06);
             margin-bottom: 20px; }
.tab-panel.active { display: block; }
.card { background: var(--card-bg); border-radius: 8px; padding: 20px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.06); margin-bottom: 16px; }
.card-title { font-size: 16px; font-weight: 600; margin-bottom: 12px;
              padding-bottom: 8px; border-bottom: 1px solid var(--border); }
.metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
                gap: 16px; margin-bottom: 20px; }
.metric-card { background: var(--card-bg); border-radius: 8px; padding: 16px;
               text-align: center; box-shadow: 0 1px 4px rgba(0,0,0,0.08);
               border-left: 4px solid var(--primary); }
.metric-card .label { font-size: 12px; color: #6c757d; margin-bottom: 4px; }
.metric-card .value { font-size: 22px; font-weight: 700; }
.metric-card .value.positive { color: var(--success); }
.metric-card .value.negative { color: var(--danger); }
table { width: 100%; border-collapse: collapse; font-size: 13px; }
th { background: #f1f3f5; padding: 10px 8px; text-align: left;
     font-weight: 600; border-bottom: 2px solid var(--border);
     position: sticky; top: 0; }
td { padding: 8px; border-bottom: 1px solid #eee; }
tr:hover { background: #f8f9fa; }
.table-container { max-height: 600px; overflow-y: auto; }
.badge { display: inline-block; padding: 2px 8px; border-radius: 12px;
         font-size: 11px; font-weight: 600; }
.badge-success { background: #d1e7dd; color: #0f5132; }
.badge-danger { background: #f8d7da; color: #842029; }
.badge-warning { background: #fff3cd; color: #664d03; }
.badge-secondary { background: #e2e3e5; color: #41464b; }
.filter-bar { display: flex; align-items: center; gap: 12px; padding: 10px 0;
              margin-bottom: 12px; border-bottom: 1px solid #eee; }
.filter-bar label { display: flex; align-items: center; gap: 4px;
                    cursor: pointer; font-size: 13px; user-select: none; }
.filter-bar input[type="checkbox"] { width: 16px; height: 16px; cursor: pointer; }
.chart-container { width: 100%; height: 600px; }
.chart-container-sm { width: 100%; height: 400px; }
.dot-chart { display: flex; flex-wrap: wrap; gap: 4px; }
.dot { width: 28px; height: 28px; border-radius: 4px; display: flex;
       align-items: center; justify-content: center; font-size: 9px;
       color: white; cursor: pointer; position: relative; }
.dot.positive { background: var(--success); }
.dot.negative { background: var(--danger); }
.dot-tooltip { position: absolute; bottom: 110%; left: 50%; transform: translateX(-50%);
               background: #333; color: white; padding: 4px 8px; border-radius: 4px;
               font-size: 11px; white-space: nowrap; display: none; z-index: 10; }
.dot:hover .dot-tooltip { display: block; }
</style>
)CSS";
}

// ============================================================================
// JavaScript: Tab切换 + 订单过滤
// ============================================================================
inline const char* GetJsTabsAndFilter() {
    return R"JS(
function switchTab(i) {
    document.querySelectorAll('.tab-panel').forEach(function(p, k) {
        p.classList.toggle('active', k === i);
    });
    document.querySelectorAll('.tabs button').forEach(function(b, k) {
        b.classList.toggle('active', k === i);
    });
    if (i === 0 && window.klineChart) window.klineChart.resize();
    if (i === 1 && window.equityChart) window.equityChart.resize();
}

function filterOrders() {
    var hideCancelled = document.getElementById('hideCancelled').checked;
    var hideUnfilled = document.getElementById('hideUnfilled').checked;
    var rows = document.querySelectorAll('#ordersTable tbody tr');
    var visible = 0;
    rows.forEach(function(row) {
        var status = parseInt(row.getAttribute('data-status'));
        var hide = false;
        if (hideCancelled && status === 3) hide = true;
        if (hideUnfilled && status === 0) hide = true;
        row.style.display = hide ? 'none' : '';
        if (!hide) visible++;
    });
    document.getElementById('orderCount').textContent =
        '\u663E\u793A ' + visible + ' / ' + rows.length + ' \u6761';
}
document.addEventListener('DOMContentLoaded', filterOrders);
)JS";
}

} // namespace report_html