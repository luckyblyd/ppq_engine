"""
基于Python之禅：扁平优于嵌套，可读性至上，简单优于复杂
"""

"""
币安合约交易 API 模拟器
基于Python之禅：扁平优于嵌套，可读性至上，简单优于复杂
模拟所有接口调用，返回 JSON 格式数据
"""
import json
import time
from typing import Dict, Any, Optional


class BinanceFuturesRobot:
    """币安合约交易模拟器"""
    
    def __init__(self):
        self._order_id_counter = int(time.time() * 1000)
        self._algo_id_counter = int(time.time() * 1000)
        self._orders = {}  # 存储订单
        self._algo_orders = {}  # 存储算法订单
    
    def _generate_order_id(self) -> int:
        """生成订单ID"""
        self._order_id_counter += 1
        return self._order_id_counter
    
    def _generate_algo_id(self) -> int:
        """生成算法订单ID"""
        self._algo_id_counter += 1
        return self._algo_id_counter
    
    def place_limit_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """
        模拟下达限价单
        
        Args:
            params: 订单参数
                - symbol: 交易对
                - side: 方向 (BUY/SELL)
                - positionSide: 持仓方向 (LONG/SHORT)
                - type: 订单类型 (LIMIT)
                - quantity: 数量
                - price: 价格
                - timeInForce: 有效期 (GTC/IOC/FOK)
        
        Returns:
            订单响应 JSON
        """
        order_id = self._generate_order_id()
        order = {
            "orderId": order_id,
            "symbol": params.get("symbol", ""),
            "status": "NEW",
            "clientOrderId": f"mock_{order_id}",
            "price": str(params.get("price", "0")),
            "avgPrice": "0.00000000",
            "origQty": str(params.get("quantity", "0")),
            "executedQty": "0",
            "cumQuote": "0.00000000",
            "timeInForce": params.get("timeInForce", "GTC"),
            "type": params.get("type", "LIMIT"),
            "reduceOnly": params.get("reduceOnly", False),
            "closePosition": params.get("closePosition", False),
            "side": params.get("side", "BUY"),
            "positionSide": params.get("positionSide", "BOTH"),
            "stopPrice": "0.00000000",
            "workingType": "CONTRACT_PRICE",
            "priceProtect": False,
            "origType": "LIMIT",
            "time": int(time.time() * 1000),
            "updateTime": int(time.time() * 1000)
        }
        self._orders[order_id] = order
        return order
    
    def cancel_futures_order(self, params: Dict[str, Any]) -> Optional[str]:
        """
        模拟撤销订单
        
        Args:
            params: 撤销参数
                - symbol: 交易对
                - orderId: 订单ID (可选)
                - origClientOrderId: 客户端订单ID (可选)
        
        Returns:
            订单状态字符串
        """
        order_id = params.get("orderId")
        if order_id and order_id in self._orders:
            self._orders[order_id]["status"] = "CANCELED"
            return "CANCELED"
        return None
    
    def cancel_all_open_orders(self, symbol: str) -> Optional[Dict[str, Any]]:
        """
        模拟撤销所有挂单
        
        Args:
            symbol: 交易对
        
        Returns:
            撤销响应 JSON
        """
        canceled_count = 0
        for order_id, order in self._orders.items():
            if order["symbol"] == symbol and order["status"] == "NEW":
                order["status"] = "CANCELED"
                canceled_count += 1
        
        return {
            "code": 200,
            "msg": "success",
            "data": {
                "symbol": symbol,
                "canceledCount": canceled_count
            }
        }
    
    def cancel_algo_order(self, algoOrderId: Optional[int] = None, 
                         origClientOrderId: Optional[str] = None, 
                         **kwargs) -> Optional[Dict[str, Any]]:
        """
        模拟撤销算法订单
        
        Args:
            algoOrderId: 算法订单ID
            origClientOrderId: 客户端订单ID
            **kwargs: 其他参数
        
        Returns:
            撤销响应 JSON
        """
        if not algoOrderId and not origClientOrderId:
            return None
        
        if algoOrderId and algoOrderId in self._algo_orders:
            self._algo_orders[algoOrderId]["status"] = "CANCELED"
            return {
                "code": 200,
                "msg": "success",
                "data": {
                    "algoId": algoOrderId,
                    "success": True
                }
            }
        return None
    
    def get_open_algo_orders(self, algoId: Optional[int] = None, **kwargs) -> Optional[Dict[str, Any]]:
        """
        模拟查询算法订单
        
        Args:
            algoId: 算法订单ID，None 则查询所有
            **kwargs: 其他参数
        
        Returns:
            算法订单列表 JSON
        """
        if algoId:
            if algoId in self._algo_orders:
                return {
                    "code": 200,
                    "msg": "success",
                    "data": [self._algo_orders[algoId]]
                }
            return {
                "code": 200,
                "msg": "success",
                "data": []
            }
        
        # 返回所有未取消的算法订单
        open_orders = [
            order for order in self._algo_orders.values()
            if order.get("status") != "CANCELED"
        ]
        return {
            "code": 200,
            "msg": "success",
            "data": open_orders
        }
    
    def place_take_profit_order(self, symbol: str, side: str, quantity: float, 
                                price: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
        """
        模拟下达止盈单
        
        Args:
            symbol: 交易对
            side: 方向 (BUY/SELL)
            quantity: 数量
            price: 价格
            **kwargs: 其他参数
                - type: 订单类型 (TAKE_PROFIT/TAKE_PROFIT_MARKET)
                - positionSide: 持仓方向
                - triggerPrice: 触发价格
                - timeInForce: 有效期
        
        Returns:
            算法订单响应 JSON
        """
        algo_id = self._generate_algo_id()
        order_type = kwargs.get("type", "TAKE_PROFIT")
        trigger_price = kwargs.get("triggerPrice") or kwargs.get("stopPrice") or price
        
        algo_order = {
            "algoId": algo_id,
            "symbol": symbol,
            "side": side,
            "type": order_type,
            "quantity": str(quantity),
            "price": str(price) if price else "0",
            "triggerPrice": str(trigger_price) if trigger_price else "0",
            "positionSide": kwargs.get("positionSide", "BOTH"),
            "timeInForce": kwargs.get("timeInForce", "GTC"),
            "status": "NEW",
            "createTime": int(time.time() * 1000)
        }
        
        self._algo_orders[algo_id] = algo_order
        return algo_order
    
    def place_stop_loss_order(self, symbol: str, side: str, quantity: float,
                              price: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
        """
        模拟下达止损单
        
        Args:
            symbol: 交易对
            side: 方向 (BUY/SELL)
            quantity: 数量
            price: 价格
            **kwargs: 其他参数
                - type: 订单类型 (STOP/STOP_MARKET)
                - positionSide: 持仓方向
                - triggerPrice: 触发价格
                - timeInForce: 有效期
        
        Returns:
            算法订单响应 JSON
        """
        algo_id = self._generate_algo_id()
        order_type = kwargs.get("type", "STOP")
        trigger_price = kwargs.get("triggerPrice") or kwargs.get("stopPrice") or price
        
        algo_order = {
            "algoId": algo_id,
            "symbol": symbol,
            "side": side,
            "type": order_type,
            "quantity": str(quantity),
            "price": str(price) if price else "0",
            "triggerPrice": str(trigger_price) if trigger_price else "0",
            "positionSide": kwargs.get("positionSide", "BOTH"),
            "timeInForce": kwargs.get("timeInForce", "GTC"),
            "status": "NEW",
            "createTime": int(time.time() * 1000)
        }
        
        self._algo_orders[algo_id] = algo_order
        return algo_order


# 创建全局实例
_robot = BinanceFuturesRobot()


# 导出函数接口，与 binance_futures_contract.py 保持一致
def place_limit_order(params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    """下达限价单"""
    return _robot.place_limit_order(params)


def cancel_futures_order(params: Dict[str, Any]) -> Optional[str]:
    """撤销订单"""
    return _robot.cancel_futures_order(params)


def cancel_all_open_orders(symbol: str) -> Optional[Dict[str, Any]]:
    """撤销所有挂单"""
    return _robot.cancel_all_open_orders(symbol)


def cancel_algo_order(algoOrderId: Optional[int] = None, 
                     origClientOrderId: Optional[str] = None, 
                     **kwargs) -> Optional[Dict[str, Any]]:
    """撤销算法订单"""
    return _robot.cancel_algo_order(algoOrderId, origClientOrderId, **kwargs)


def get_open_algo_orders(algoId: Optional[int] = None, **kwargs) -> Optional[Dict[str, Any]]:
    """查询算法订单"""
    return _robot.get_open_algo_orders(algoId, **kwargs)


def place_take_profit_order(symbol: str, side: str, quantity: float,
                            price: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
    """下达止盈单"""
    return _robot.place_take_profit_order(symbol, side, quantity, price, **kwargs)


def place_stop_loss_order(symbol: str, side: str, quantity: float,
                          price: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
    """下达止损单"""
    return _robot.place_stop_loss_order(symbol, side, quantity, price, **kwargs)

