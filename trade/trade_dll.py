#!/usr/bin/env python3
"""
trade_dll.py — 一个简单的 Python 层封装，用于被 C++ DLL 调用。

此模块在导入时会尽量使用真实的 binance_futures_contract 模块，
若不可用则回退到内置的 robot 模拟器。对外提供统一的函数：
  init(config_path)
  place_order(req_dict)
  cancel_order(symbol, order_id)
  query_order(symbol, order_id)
  place_algo_order(req_dict)
  cancel_algo_order(algo_order_id)
  get_mode()

返回值尽量规范化为 dict，包含字段：
  success (0/1), order_id, status, fill_price, fill_qty, error_msg
"""
import os
import sys
from typing import Dict, Any, Optional

# 尝试延迟导入真实模块，导入失败则使用 robot 模拟
_client = None
_acct = None
_mode = "future"


def _ensure_path(config_path: Optional[str]):
    if not config_path:
        # 将项目根加入路径，方便相对导入
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    else:
        root = os.path.dirname(os.path.abspath(config_path))
    if root not in sys.path:
        sys.path.insert(0, root)


def init(config_path: Optional[str] = None) -> int:
    """初始化：把工程路径加入 sys.path，并选择真实客户端或模拟器。"""
    global _client, _acct, _mode
    _ensure_path(config_path)
    try:
        # 优先使用真实的期货合约模块（如果可用）
        from trade import binance_futures_contract as bf_contract
        from trade import binance_futures_account as bf_account
        _client = bf_contract
        _acct = bf_account
        _mode = "future"
        return 0
    except Exception:
        # 回退到内置模拟器
        try:
            from trade import robot as robot_mod
            _client = robot_mod
            _acct = robot_mod
            _mode = "sim"
            return 0
        except Exception as e:
            _client = None
            _acct = None
            _mode = "unknown"
            return -1


def _normalize_response(resp: Any) -> Dict[str, Any]:
    # 标准化响应字典
    out = {
        "success": 0,
        "order_id": "",
        "status": "",
        "fill_price": 0.0,
        "fill_qty": 0.0,
        "error_msg": "",
    }
    if resp is None:
        out["error_msg"] = "no response"
        return out

    # 如果已经是 dict-like
    if isinstance(resp, dict):
        # success 优先使用显式字段
        if "success" in resp:
            out["success"] = int(bool(resp.get("success")))
        else:
            # 若存在 orderId 等则视为成功
            if any(k in resp for k in ("orderId", "clientOrderId", "algoId")):
                out["success"] = 1

        out["order_id"] = str(resp.get("orderId", resp.get("clientOrderId", resp.get("algoId", ""))))
        out["status"] = str(resp.get("status", ""))
        # 常见成交价/数量字段映射
        out["fill_price"] = float(resp.get("avgPrice", resp.get("fill_price", resp.get("price", 0.0)))) if resp.get("avgPrice", None) is not None or resp.get("fill_price", None) is not None or resp.get("price", None) is not None else 0.0
        out["fill_qty"] = float(resp.get("executedQty", resp.get("fill_qty", 0.0))) if resp.get("executedQty", None) is not None or resp.get("fill_qty", None) is not None else 0.0
        out["error_msg"] = str(resp.get("error_msg", resp.get("msg", resp.get("error", ""))))
        return out

    # 如果是模拟器返回的对象（如 SimpleNamespace），尝试转换
    try:
        order_id = getattr(resp, "orderId", None) or getattr(resp, "clientOrderId", None)
        if order_id is not None:
            out["success"] = 1
            out["order_id"] = str(order_id)
            out["status"] = str(getattr(resp, "status", ""))
            out["fill_price"] = float(getattr(resp, "avgPrice", getattr(resp, "price", 0.0)))
            out["fill_qty"] = float(getattr(resp, "executedQty", 0.0))
            return out
    except Exception:
        pass

    # 兜底转换
    out["error_msg"] = str(resp)
    return out


def place_order(req: Dict[str, Any]) -> Dict[str, Any]:
    if _client is None:
        return {"success": 0, "error_msg": "client not initialized"}

    # 将请求映射为目标函数所需参数
    params = {}
    params["symbol"] = req.get("symbol") or req.get("symbol", "")
    params["side"] = req.get("side") or req.get("side", "")
    params["positionSide"] = req.get("position_side") or req.get("positionSide") or req.get("position_side", "BOTH")
    params["type"] = req.get("order_type") or req.get("type") or "LIMIT"
    if "price" in req:
        params["price"] = req.get("price")
    if "quantity" in req:
        params["quantity"] = req.get("quantity")

    try:
        if hasattr(_client, "place_limit_order"):
            resp = _client.place_limit_order(params)
        elif hasattr(_client, "new_order"):
            resp = _client.new_order(params)
        else:
            resp = None
    except Exception as e:
        return {"success": 0, "error_msg": str(e)}

    return _normalize_response(resp)


def cancel_order(symbol: str, order_id: str) -> Dict[str, Any]:
    if _client is None:
        return {"success": 0, "error_msg": "client not initialized"}
    params = {"symbol": symbol}
    # 支持 orderId 或 origClientOrderId
    try:
        # 如果是数字字符串，尝试转为 int
        try:
            params["orderId"] = int(order_id)
        except Exception:
            params["origClientOrderId"] = order_id

        if hasattr(_client, "cancel_futures_order"):
            resp = _client.cancel_futures_order(params)
        elif hasattr(_client, "cancel_order"):
            resp = _client.cancel_order(params)
        else:
            resp = None
    except Exception as e:
        return {"success": 0, "error_msg": str(e)}

    # cancel API 有时返回状态字符串
    if isinstance(resp, str):
        return {"success": 1, "status": resp}
    return _normalize_response(resp)


def query_order(symbol: str, order_id: str) -> Dict[str, Any]:
    if _acct is None:
        return {"success": 0, "error_msg": "account client not initialized"}
    params = {"symbol": symbol}
    try:
        try:
            params["orderId"] = int(order_id)
        except Exception:
            params["origClientOrderId"] = order_id

        # 优先尝试账号查询接口
        if hasattr(_acct, "get_future_orders"):
            resp = _acct.get_future_orders(params)
        elif hasattr(_acct, "get_all_orders"):
            resp = _acct.get_all_orders(symbol)
        else:
            resp = None
    except Exception as e:
        return {"success": 0, "error_msg": str(e)}
    return _normalize_response(resp)


def place_algo_order(req: Dict[str, Any]) -> Dict[str, Any]:
    if _client is None:
        return {"success": 0, "error_msg": "client not initialized"}

    # 支持两类：止盈/止损（TAKE_PROFIT / STOP）
    algo_type = req.get("algo_type") or req.get("type") or "TAKE_PROFIT"
    params = {}
    params["symbol"] = req.get("symbol", "")
    params["side"] = req.get("side", "")
    params["quantity"] = req.get("quantity", 0)
    if "price" in req:
        params["price"] = req.get("price")
    if "trigger_price" in req:
        params["triggerPrice"] = req.get("trigger_price")
    if "position_side" in req:
        params["positionSide"] = req.get("position_side")

    try:
        if hasattr(_client, "place_take_algo_order"):
            resp = _client.place_take_algo_order(params)
        elif algo_type.upper().startswith("TAKE") and hasattr(_client, "place_take_profit_order"):
            resp = _client.place_take_profit_order(params.get("symbol"), params.get("side"), params.get("quantity"), params.get("price"))
        elif algo_type.upper().startswith("STOP") and hasattr(_client, "place_stop_loss_order"):
            resp = _client.place_stop_loss_order(params.get("symbol"), params.get("side"), params.get("quantity"), params.get("price"))
        elif hasattr(_client, "new_algo_order"):
            resp = _client.new_algo_order(params)
        else:
            resp = None
    except Exception as e:
        return {"success": 0, "error_msg": str(e)}

    return _normalize_response(resp)


def cancel_algo_order(algo_order_id: str) -> Dict[str, Any]:
    if _client is None:
        return {"success": 0, "error_msg": "client not initialized"}
    try:
        if hasattr(_client, "cancel_algo_order"):
            # 某些实现接受关键字参数
            try:
                resp = _client.cancel_algo_order(algoOrderId=algo_order_id)
            except TypeError:
                resp = _client.cancel_algo_order(algo_order_id)
        else:
            resp = None
    except Exception as e:
        return {"success": 0, "error_msg": str(e)}
    return _normalize_response(resp)


def get_mode() -> str:
    return _mode


if __name__ == "__main__":
    # 简单自测
    print("trade_dll.py self-test")
    print("init ->", init(None))
    print("mode ->", get_mode())
