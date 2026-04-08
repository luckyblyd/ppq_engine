"""
交易策略接口和工厂方法
基于Python之禅：扁平优于嵌套，可读性至上，简单优于复杂
"""

from abc import ABC, abstractmethod
from typing import Dict, Any, Optional
from core.enums import Operate
from utils.tradeutils import truncate_decimal, get_price_decimal, get_qty_decimal
from data_module.models import Orders


class UnsupportedOperationError(Exception):
    """不支持的操作异常"""
    pass


class TradeStrategy(ABC):
    """交易策略抽象基类"""
    
    @abstractmethod
    def place_order(self, order: Orders) -> Optional[Dict[str, Any]]:
        """
        下单
        
        Args:
            order: 订单对象
            position_side: 持仓方向（LONG/SHORT，仅合约模式）
            price_match: 价格匹配模式（仅合约模式限价单）
            
        Returns:
            订单响应，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: place_order")
    
    @abstractmethod
    def cancel_order(self, params: Dict[str, Any]) -> Optional[str]:
        """
        撤销订单
        
        Args:
            params: 撤销参数
            
        Returns:
            订单状态，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: cancel_order")
    
    @abstractmethod
    def query_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """
        查询订单
        
        Args:
            params: 查询参数
            
        Returns:
            订单信息，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: query_order")
    
    def borrow_repay(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """
        借贷/还币（仅杠杆模式支持）
        
        Args:
            params: 借贷参数
            
        Returns:
            响应结果，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: borrow_repay")
    
    def borrow_for_buy(self, currency_pair: str, quantity: float, price: float):
        """
        买入前借USDT（仅杠杆模式支持）
        
        Args:
            currency_pair: 货币对
            quantity: 数量
            price: 价格
        """
        raise UnsupportedOperationError("不支持的操作: borrow_for_buy")
    
    def borrow_for_sell(self, currency_pair: str, quantity: float):
        """
        卖出前借币（仅杠杆模式支持）
        
        Args:
            currency_pair: 货币对
            quantity: 数量
        """
        raise UnsupportedOperationError("不支持的操作: borrow_for_sell")
    
    def repay_after_close(self, currency_pair: str, quantity: float, price: float, 
                         position_type: str):
        """
        平仓后还币（仅杠杆模式支持）
        
        Args:
            currency_pair: 货币对
            quantity: 数量
            price: 价格
            position_type: 持仓类型
        """
        raise UnsupportedOperationError("不支持的操作: repay_after_close")
    
    def place_take_profit_order(self, order: Orders, price: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
        """
        下达止盈单（仅合约模式支持）
        
        Args:
            symbol: 交易对
            side: 方向
            quantity: 数量
            price: 价格
            **kwargs: 其他参数
            
        Returns:
            算法订单响应，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: place_take_profit_order")
    
    def place_stop_loss_order(self, order: Orders, price: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """
        下达止损单（仅合约模式支持）
        
        Args:
            symbol: 交易对
            side: 方向
            quantity: 数量
            price: 价格
            **kwargs: 其他参数
            
        Returns:
            算法订单响应，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: place_stop_loss_order")
    
    def cancel_algo_order(self, algoOrderId: Optional[int] = None,
                         origClientOrderId: Optional[str] = None,
                         **kwargs) -> Optional[Dict[str, Any]]:
        """
        撤销算法订单（仅合约模式支持）
        
        Args:
            algoOrderId: 算法订单ID
            origClientOrderId: 客户端订单ID
            **kwargs: 其他参数
            
        Returns:
            撤销响应，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: cancel_algo_order")
    
    def get_open_algo_orders(self, algoId: Optional[int] = None,
                            **kwargs) -> Optional[Dict[str, Any]]:
        """
        查询算法订单（仅合约模式支持）
        
        Args:
            algoId: 算法订单ID
            **kwargs: 其他参数
            
        Returns:
            算法订单信息，失败返回 None
        """
        raise UnsupportedOperationError("不支持的操作: get_open_algo_orders")
    
    def process_order_response(self, order: Orders, response: Dict[str, Any]) -> None:
        """
        处理订单响应报文，更新订单对象的状态和价格
        
        Args:
            order: 订单对象
            response: 交易所返回的响应数据
        返回 macthstatus
        """
        # 1) 记录外部委托号
        outorder_id = str(response.get("orderId") or "")
        if outorder_id:
            order.outorderid = outorder_id
        
        # 2) 如果成交状态为 FILLED，则立即更新 matchstatus 和 实际成交价格
        external_status = response.get("status", "")
        if external_status == "FILLED":
            from core.trade_executor_tool import MATCH_STATUS_FILLED
            order.matchstatus = MATCH_STATUS_FILLED
            
            fill_price = float(response.get("price"))
            if fill_price == 0.0:
                fill_price = float(response.get("avgPrice"))
            if fill_price != 0.0:
                order.price = fill_price
            order.quantity = float(response.get("origQty"))
            order.amount = order.quantity * order.price
            return 1
        elif external_status == "CANCELED" or external_status == "REJECTED":
            return 3
        return 0
    def process_algoorder_response(self, order: Orders, response: Dict[str, Any], 
                                   algo_order_id: int) -> Optional[float]:
        """
        处理算法订单响应报文，更新订单对象的状态和价格
        
        Args:
            order: 订单对象
            response: 算法订单响应数据
            algo_order_id: 算法订单ID（用于验证）
            
        Returns:
            返回int 0未成交 1成交 3撤销
        """
        # 验证算法订单ID
        if str(response.get("algoId")) != str(algo_order_id):
            return 0
        
        # 获取算法订单状态
        algo_status = response.get("algoStatus", response.get("status", ""))
        
        # FINISHED 表示委托已成交
        if algo_status == "FINISHED":
            # 判断当前是否有成交价格
            if response.get("actualPrice",None):
                fill_price = float(response.get("actualPrice", 0))
            else:
                # 提取成交价格
                fill_price = float(response.get("price", 0))
            
            if fill_price > 0:
                # 更新订单价格
                order.price = fill_price
                return 1
        elif algo_status == "CANCELED" or algo_status == "REJECTED":
            return 3
        
        return 0        
            
    
    @property
    def supports_borrow(self) -> bool:
        """是否支持借贷"""
        return False
    
    @property
    def supports_algo_orders(self) -> bool:
        """是否支持算法订单（止盈止损）"""
        return False
    
    @property
    def recv_window(self) -> int:
        """接收窗口时间（毫秒）"""
        return 5000

class BinanceFuturesStrategy(TradeStrategy):
    """币安合约交易策略"""
    
    def __init__(self):
        from trade.binance_futures_contract import (
            place_limit_order, cancel_futures_order,
            place_take_algo_order,
            cancel_algo_order, get_open_algo_orders
        )
        self._place_order_func = place_limit_order
        self._cancel_order_func = cancel_futures_order
        self._place_take_algo_order = place_take_algo_order
        self._cancel_algo_func = cancel_algo_order
        self._get_algo_orders_func = get_open_algo_orders

        self.OPERATE_MAP = {
            Operate.LO: ("BUY", "LONG"),    # 做多开仓
            Operate.LE: ("SELL", "LONG"),   # 做多平仓（卖出平多）
            Operate.SO: ("SELL", "SHORT"),  # 做空开仓
            Operate.SE: ("BUY", "SHORT"),   # 做空平仓（买入平空）
        }
    
    @property
    def supports_algo_orders(self) -> bool:
        return True
    
    @property
    def recv_window(self) -> int:
        return 9000
    
    def _get_symbol(self, currency_pair: str) -> str:
        """获取交易符号（去掉斜杠）"""
        # 标准化货币对（USDT转USDC）
        if currency_pair in ['ETH/USDT', 'SOL/USDT', 'BTC/USDT']:
            currency_pair = currency_pair.replace("USDT", "USDC")
        return currency_pair.replace("/", "")
    
    def place_order(self, order: Orders) -> Optional[Dict[str, Any]]:
        """合约下单"""
        operate = Operate(order.type)
        symbol = self._get_symbol(order.currency_pair)
        quantity = float(order.quantity) if order.quantity else 0
        price = float(order.price) if order.price else 0
        
        # 将枚举转换为币安API需要的side字符串
        api_side, pos_side = self.OPERATE_MAP.get(operate, (None, None))
        
        
        params = {
            "symbol": symbol,
            "side": api_side,
            "positionSide": pos_side,
            "quantity": truncate_decimal(quantity, get_qty_decimal(symbol)),
            "recvWindow": self.recv_window,
            #"timeInForce": "GTC",
        }
        
        # 0限价挂单 1市价直接成交单  默认None时取配置里限价单
        if order.order_type == 0:
            # 止盈单使用固定价格
            params["type"] = "LIMIT"
            params["timeInForce"] = "GTC"
            params["price"] = truncate_decimal(price, get_price_decimal(symbol))
        else:
            params["type"] = "MARKET"
            #params["priceMatch"] = "OPPONENT_5"
        
        return self._place_order_func(params)
    
    def cancel_order(self, params: Dict[str, Any]) -> Optional[str]:
        return self._cancel_order_func(params)
    
    def query_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        from trade.binance_futures_account import get_future_orders
        return get_future_orders(params)
    
    def place_take_profit_order(self, order: Orders, price: Optional[float] = None) -> Optional[Dict[str, Any]]:
        # 将枚举转换为币安API需要的side字符串
        api_side, pos_side = self.OPERATE_MAP.get(Operate(order.type), (None, None))
        symbol = self._get_symbol(order.currency_pair)
        bli = 1
        params={}
        if api_side == "SELL":
            # 做多止盈单下单价需低于当前价
            bli = 0.995
        elif api_side == "BUY":
            # 做空止盈单下单价需高于当前价
            bli = 1.005
        if not symbol.endswith("USDC"):
            #条件单
            params = {
                "algotype": "CONDITIONAL",
                "symbol": symbol,
                "side": api_side,
                "positionSide": pos_side,
                "type": "TAKE_PROFIT",
                "quantity": truncate_decimal(order.quantity, get_qty_decimal(symbol)),
                "triggerprice": truncate_decimal(price, get_price_decimal(symbol)),
                "price": truncate_decimal(price*bli, get_price_decimal(symbol)),
                "timeInForce":"GTC" 
            }
        else:
            params = {
                "algotype": "CONDITIONAL",
                "symbol": symbol,
                "side": api_side,
                "positionSide": pos_side,
                "type": "TAKE_PROFIT",
                "quantity": truncate_decimal(order.quantity, get_qty_decimal(symbol)),
                "triggerprice": truncate_decimal(price, get_price_decimal(symbol)),
                "priceMatch": "QUEUE",
                "timeInForce":"GTC" 
            }
        return self._place_take_algo_order(params)
    
    def place_stop_loss_order(self, order: Orders, price: Optional[float] = None) -> Optional[Dict[str, Any]]:
        # 将枚举转换为币安API需要的side字符串
        api_side, pos_side = self.OPERATE_MAP.get(Operate(order.type), (None, None))
        symbol = self._get_symbol(order.currency_pair)
        bli = 1
        if api_side == "SELL":
            # 做多止盈单下单价需低于当前价
            bli = 0.995
        elif api_side == "BUY":
            # 做空止盈单下单价需高于当前价
            bli = 1.005
        if not symbol.endswith("USDC"):
            #条件单
            params = {
                "algotype": "CONDITIONAL",
                "symbol": symbol,
                "side": api_side,
                "positionSide": pos_side,
                "type": "STOP",
                "quantity": truncate_decimal(order.quantity, get_qty_decimal(symbol)),
                "triggerprice": truncate_decimal(price, get_price_decimal(symbol)),
                "price": truncate_decimal(price*bli, get_price_decimal(symbol)),
                "timeInForce":"GTC" 
            }
        else:
            params = {
                "algotype": "CONDITIONAL",
                "symbol": symbol,
                "side": api_side,
                "positionSide": pos_side,
                "type": "STOP",
                "quantity": truncate_decimal(order.quantity, get_qty_decimal(symbol)),
                "triggerprice": truncate_decimal(price, get_price_decimal(symbol)),
                "priceMatch": "QUEUE",
                "timeInForce":"GTC" 
            }
        return self._place_take_algo_order(params)
    
    def cancel_algo_order(self, algoOrderId: Optional[int] = None,
                         origClientOrderId: Optional[str] = None,
                         **kwargs) -> Optional[Dict[str, Any]]:
        return self._cancel_algo_func(algoOrderId, origClientOrderId, **kwargs)
    
    def get_open_algo_orders(self, algoId: Optional[int] = None,
                            **kwargs) -> Optional[Dict[str, Any]]:
        return self._get_algo_orders_func(algoId, **kwargs)


class BinanceMarginStrategy(TradeStrategy):
    """币安杠杆交易策略"""
    
    # 价格调整系数
    PRICE_ADJUST_BUY = 1.005
    PRICE_ADJUST_SELL = 0.995
    # 还币比例（仅杠杆模式使用）
    REPAY_RATIO = 0.9
    
    def __init__(self):
        from trade.binance_margin_order import (
            new_margin_order, cancel_margin_order, borrow_repay
        )
        self._place_order_func = new_margin_order
        self._cancel_order_func = cancel_margin_order
        self._borrow_repay_func = borrow_repay
    
    @property
    def supports_borrow(self) -> bool:
        return True
    
    @property
    def recv_window(self) -> int:
        return 5000
    
    def place_order(self, order: Orders) -> Optional[Dict[str, Any]]:
        """杠杆下单"""
        operate = Operate(order.type)
        # 标准化货币对（USDT转USDC）
        currency_pair = order.currency_pair
        if currency_pair in ['ETH/USDT', 'SOL/USDT', 'BTC/USDT']:
            currency_pair = currency_pair.replace("USDT", "USDC")
        symbol = currency_pair.replace("/", "")
        quantity = float(order.quantity) if order.quantity else 0
        price = float(order.price) if order.price else 0
        order_type = order.order_type
        
        # 将枚举转换为币安API需要的side字符串
        if operate in (Operate.LO, Operate.SE):
            api_side = "BUY"
        elif operate in (Operate.SO, Operate.LE):
            api_side = "SELL"
        else:
            raise ValueError(f"不支持的操作类型: {operate}")
        
        # 判断订单类型：1=市价单, 0或None=限价单
        is_market_order = order_type == 1
        
        params = {
            "symbol": symbol,
            "side": api_side,
            "type": "MARKET" if is_market_order else "LIMIT",
            "quantity": truncate_decimal(quantity, get_qty_decimal(currency_pair)),
            "recvWindow": self.recv_window,
        }
        
        # 限价单需要设置 price 和 timeInForce
        if not is_market_order:
            params["timeInForce"] = "GTC"
            # 杠杆模式：使用调整后的价格
            adjusted_price = price * self.PRICE_ADJUST_BUY if operate in (Operate.LO, Operate.LE) else price * self.PRICE_ADJUST_SELL
            params["price"] = truncate_decimal(adjusted_price, get_price_decimal(currency_pair))
        
        return self._place_order_func(params)
    
    def cancel_order(self, params: Dict[str, Any]) -> Optional[str]:
        return self._cancel_order_func(params)
    
    def query_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        from trade.binance_margin_account import margin_one_orders
        return margin_one_orders(params)
    
    def borrow_repay(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        return self._borrow_repay_func(params)
    
    def borrow_for_buy(self, currency_pair: str, quantity: float, price: float):
        """
        买入前借USDT
        
        Args:
            currency_pair: 货币对
            quantity: 数量
            price: 价格
        """
        symbol = currency_pair.replace("/", "")
        params = {
            "asset": "USDT",
            "isIsolated": "FALSE",
            "symbol": symbol,
            "amount": truncate_decimal(quantity * price, 2),
            "type": "BORROW",
        }
        self.borrow_repay(params)
    
    def borrow_for_sell(self, currency_pair: str, quantity: float):
        """
        卖出前借币
        
        Args:
            currency_pair: 货币对
            quantity: 数量
        """
        symbol = currency_pair.replace("/", "")
        asset = currency_pair.replace("/USDT", "").replace("/USDC", "")
        params = {
            "asset": asset,
            "isIsolated": "FALSE",
            "symbol": symbol,
            "amount": truncate_decimal(quantity, get_qty_decimal(currency_pair)),
            "type": "BORROW",
        }
        self.borrow_repay(params)
    
    def repay_after_close(self, currency_pair: str, quantity: float, price: float, 
                         position_type: str):
        """
        平仓后还币
        
        Args:
            currency_pair: 货币对
            quantity: 数量
            price: 价格
            position_type: 持仓类型（POSITION_TYPE_LONG 或 POSITION_TYPE_SHORT）
        """
        symbol = currency_pair.replace("/", "")
        from core.trade_executor_tool import POSITION_TYPE_LONG, POSITION_TYPE_SHORT
        
        if position_type == POSITION_TYPE_LONG:
            # 买入平仓后还USDT
            params = {
                "asset": "USDT",
                "isIsolated": "FALSE",
                "symbol": symbol,
                "amount": truncate_decimal(quantity * price * self.REPAY_RATIO, 2),
                "type": "REPAY",
            }
        else:
            # 卖出平仓后还币
            asset = currency_pair.replace("/USDT", "").replace("/USDC", "")
            params = {
                "asset": asset,
                "isIsolated": "FALSE",
                "symbol": symbol,
                "amount": truncate_decimal(quantity, get_qty_decimal(currency_pair)),
                "type": "REPAY",
            }
        try:
            self.borrow_repay(params)
        except Exception as e:
            from utils.logger import get_logger
            logger = get_logger(__name__)
            logger.error(f"借贷操作失败: {e}")


class BinanceSpotStrategy(TradeStrategy):
    """币安现货交易策略"""
    
    def __init__(self):
        pass
    
    def place_order(self, order: Orders) -> Optional[Dict[str, Any]]:
        """现货下单"""
        pass
    
    def cancel_order(self, params: Dict[str, Any]) -> Optional[str]:
        pass
    
    def query_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        pass
    


class RobotStrategy(TradeStrategy):
    """机器人模拟交易策略"""
    
    # 价格调整系数（模拟合约模式）
    PRICE_ADJUST_BUY = 1.005
    PRICE_ADJUST_SELL = 0.995
    
    def __init__(self):
        from trade.robot import (
            place_limit_order, cancel_futures_order,
            place_take_profit_order, place_stop_loss_order,
            cancel_algo_order, get_open_algo_orders
        )
        self._place_order_func = place_limit_order
        self._cancel_order_func = cancel_futures_order
        self._place_take_profit_func = place_take_profit_order
        self._place_stop_loss_func = place_stop_loss_order
        self._cancel_algo_func = cancel_algo_order
        self._get_algo_orders_func = get_open_algo_orders
    
    @property
    def supports_algo_orders(self) -> bool:
        return True
    
    @property
    def recv_window(self) -> int:
        return 9000
    
    def place_order(self, order: Orders) -> Optional[Dict[str, Any]]:
        """机器人下单（模拟合约模式）"""
        operate = Operate(order.type)
        # 标准化货币对（USDT转USDC）
        currency_pair = order.currency_pair
        if currency_pair in ['ETH/USDT', 'SOL/USDT', 'BTC/USDT']:
            currency_pair = currency_pair.replace("USDT", "USDC")
        symbol = currency_pair.replace("/", "")
        quantity = float(order.quantity) if order.quantity else 0
        price = float(order.price) if order.price else 0
        order_type = order.order_type
        
        # 将枚举转换为币安API需要的side字符串
        if operate in (Operate.LO, Operate.SE):
            api_side = "BUY"
        elif operate in (Operate.SO, Operate.LE):
            api_side = "SELL"
        else:
            raise ValueError(f"不支持的操作类型: {operate}")
        
        # 判断订单类型：1=市价单, 0或None=限价单
        is_market_order = order_type == 1
        
        params = {
            "symbol": symbol,
            "side": api_side,
            "type": "MARKET" if is_market_order else "LIMIT",
            "quantity": truncate_decimal(quantity, get_qty_decimal(currency_pair)),
            "recvWindow": self.recv_window,
        }
        
        # 限价单需要设置 price 和 timeInForce
        if not is_market_order:
            params["timeInForce"] = "GTC"
            if position_side:
                params["positionSide"] = position_side
            # 判断是否为止盈单（通过 remark 判断）
            is_take_profit = order.remark in ['止盈委托', '止损委托']
            if is_take_profit:
                # 止盈单使用固定价格
                adjusted_price = price * self.PRICE_ADJUST_BUY if operate in (Operate.LO, Operate.LE) else price * self.PRICE_ADJUST_SELL
                params["price"] = truncate_decimal(adjusted_price, get_price_decimal(currency_pair))
            else:
                # 正常单使用价格匹配（如果指定了 price_match 则使用，否则使用默认值）
                params["priceMatch"] = price_match if price_match else "OPPONENT_5"
        else:
            # 市价单：合约模式需要设置 positionSide
            if position_side:
                params["positionSide"] = position_side
        
        return self._place_order_func(params)
    
    def cancel_order(self, params: Dict[str, Any]) -> Optional[str]:
        return self._cancel_order_func(params)
    
    def query_order(self, params: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        return {"status": "NEW"}  # 模拟返回
    
    def place_take_profit_order(self, order: Orders, price: Optional[float] = None) -> Optional[Dict[str, Any]]:
        pass
    
    def place_stop_loss_order(self, order: Orders, price: Optional[float] = None) -> Optional[Dict[str, Any]]:
        pass
    
    def cancel_algo_order(self, algoOrderId: Optional[int] = None,
                         origClientOrderId: Optional[str] = None,
                         **kwargs) -> Optional[Dict[str, Any]]:
        return self._cancel_algo_func(algoOrderId, origClientOrderId, **kwargs)
    
    def get_open_algo_orders(self, algoId: Optional[int] = None,
                            **kwargs) -> Optional[Dict[str, Any]]:
        return self._get_algo_orders_func(algoId, **kwargs)


class TradeStrategyFactory:
    """交易策略工厂类 - 使用字典映射替代 if-else"""
    
    _strategies = {
        'margin': BinanceMarginStrategy,
        'future': BinanceFuturesStrategy,
        'spot': BinanceSpotStrategy,
        'robot': RobotStrategy
    }
    
    @classmethod
    def get_strategy(cls, mode: str) -> TradeStrategy:
        """
        获取交易策略实例
        
        Args:
            mode: 交易模式 ('margin', 'future', 'spot', 'robot')
            
        Returns:
            交易策略实例
            
        Raises:
            ValueError: 不支持的交易模式
        """
        handler_class = cls._strategies.get(mode.lower())
        if not handler_class:
            raise ValueError(f"不支持的交易模式: {mode}，支持的模式: {list(cls._strategies.keys())}")
        return handler_class()
    
    @classmethod
    def register_strategy(cls, mode: str, strategy_class: type):
        """
        注册新的交易策略（扩展用）
        
        Args:
            mode: 交易模式名称
            strategy_class: 策略类
        """
        cls._strategies[mode.lower()] = strategy_class

