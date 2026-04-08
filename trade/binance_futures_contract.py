#!/usr/bin/env python
# 当前文件需要支持单个文件测试，所以需要引入全局配置
import os
import sys
import json

# 判断是否为单独运行（直接执行此文件）
_is_main_module = __name__ == "__main__"

# 只在单独运行时设置路径，确保能找到依赖模块
if _is_main_module:
    current_dir = os.path.dirname(os.path.abspath(__file__))
    parent_dir = os.path.dirname(current_dir)
    if current_dir not in sys.path:
        sys.path.insert(0, current_dir)
    if parent_dir not in sys.path:
        sys.path.insert(0, parent_dir)

#from binance_ppq.cm_futures import CMFutures #pip install binance-futures-connector
# 导入 binance 模块 - 支持单独运行和作为模块导入两种场景

from binance_ppq.um_futures import UMFutures as Client
from binance_ppq.error import ClientError


# 导入工具模块 - 支持单独运行和作为模块导入两种场景
try:
    # 优先尝试绝对导入（作为模块被导入时，从项目根目录导入）
    from utils.decorators import retry, log_calls
    from utils.logger import get_logger
except ImportError:
    # 如果绝对导入失败，尝试相对导入（单独运行时）
    try:
        from utils.decorators import retry, log_calls
        from utils.logger import get_logger
    except ImportError:
        raise ImportError("无法导入 utils.decorators 或 utils.logger 模块")

# 导入 prepare_env - 支持单独运行和作为模块导入两种场景
try:
    # 优先尝试从 trade 包导入（作为模块被导入时）
    from trade.prepare_env import get_api_key, proxies_env, private_key_pass_env
except ImportError:
    # 如果失败，尝试从当前目录导入（单独运行时）
    try:
        from prepare_env import get_api_key, proxies_env, private_key_pass_env
    except ImportError:
        raise ImportError("无法导入 prepare_env 模块，请确保 prepare_env.py 文件存在")

logger = get_logger(__name__)

# 全局配置 - 根据运行环境自动查找配置文件
config = {}
# 获取项目根目录（向上两级：trade -> ppq -> 项目根目录）
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
config_paths = [
    os.path.join(project_root, 'config.json'),  # 项目根目录
    os.path.join(os.path.dirname(os.path.abspath(__file__)), 'config.json'),  # trade 目录
    'config.json'  # 当前工作目录
]

for file_path in config_paths:
    try:
        if os.path.exists(file_path):
            with open(file_path, 'r', encoding='utf-8') as file:
                config = json.load(file)
            break
    except (FileNotFoundError, json.JSONDecodeError):
        continue

if not config:
    if _is_main_module:
        print(f"交易模块初始化警告: 未找到有效的配置文件。尝试的路径: {config_paths}")
    # 作为模块导入时，不打印错误，让调用方处理

# 从 config.json 读取代理配置，如果没有则使用 prepare_env 中的默认值
def get_proxies_from_config(config):
    """从配置文件中读取代理设置"""
    # 优先从 trade 配置中读取
    proxies_config = config.get('proxies')
    
    # 如果 trade 配置中没有，尝试从根配置读取
    if not proxies_config:
        proxies_config = config.get('proxies')
    
    # 如果配置中有代理设置，使用配置的值
    if proxies_config:
        if isinstance(proxies_config, str):
            # 如果是字符串，假设是单个代理地址，同时用于 http 和 https
            return {'http': proxies_config, 'https': proxies_config}
        else:
            return proxies_env
    
    # 如果没有配置，使用 prepare_env 中的默认值作为后备
    return proxies_env

# 从 config.json 读取 private_key_pass，如果没有则使用 prepare_env 中的默认值
def get_private_key_pass_from_config(config):
    """从配置文件中读取 private_key_pass"""
    # 优先从 trade 配置中读取
    trade_config = config.get('trade', {})
    private_key_pass = trade_config.get('private_key_pass')
    
    # 如果 trade 配置中没有，尝试从根配置读取
    if not private_key_pass:
        private_key_pass = config.get('private_key_pass')
    
    # 如果配置中有，使用配置的值，否则使用默认值
    return private_key_pass if private_key_pass else private_key_pass_env

def parase_api_key(api_key):
    # 提取第 45 到第 49 个字母
    sub_string = api_key[44:50]
    reversed_sub_string = sub_string[::-1]
    api_key = api_key[:44] + reversed_sub_string + api_key[50:]
    return api_key

# 初始化 U 本位合约客户端
# 如果配置为空，在调用函数时会报错，这样可以避免模块导入时出错
try:
    api_key, api_secret = get_api_key(config)
    if api_key and api_secret:
        # 从 config.json 读取代理配置
        proxies = get_proxies_from_config(config)
        private_key_pass = get_private_key_pass_from_config(config)
        
        client = Client(
            key=parase_api_key(api_key), 
            private_key=parase_api_key(api_secret), 
            base_url="https://fapi.binance.com", 
            proxies=proxies,
            private_key_pass=private_key_pass
        )
    else:
        # 配置为空时，设置为 None，函数调用时会检查
        client = None
        if _is_main_module:
            logger.warning("API密钥为空，客户端未初始化。请检查配置文件。")
except Exception as e:
    client = None
    logger.warning(f"初始化币安客户端失败: {e}。请检查配置文件。")

@log_calls(level='info', log_args=True, log_result=True) # 入参出参日志装饰器
@retry(max_attempts=2, delay=2, exceptions=(ClientError,))  # 重试装饰器
def place_limit_order(params):
    """
    下达限价单。
    params = {
            "symbol": symbol,
            "side": side,
            "positionSide":"SHORT" if side == "SELL" else "LONG",  # 确保 positionSide 正确
            "type": "LIMIT",
            "quantity": quantity,
            "price": price,
            "timeInForce": timeInForce,
        }
        params.update(kwargs)
    #盘口价下单模式:
priceMatch
OPPONENT (盘口对手价)
OPPONENT_5 (盘口对手5档价)
OPPONENT_10 (盘口对手10档价)
OPPONENT_20
QUEUE (盘口同向价)
QUEUE_5 (盘口同向排队5档价)
QUEUE_10 (盘口同向排队10档价)
QUEUE_20 (盘口同向排队20档价)
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    response = client.new_order(**params)
    # 返回完整响应，方便外部根据 status / 成交价格等字段做进一步处理
    return response

@log_calls(level='info', log_args=True, log_result=True) # 入参出参日志装饰器
@retry(max_attempts=2, delay=5, exceptions=(ClientError,))  # 重试装饰器
def cancel_futures_order(params):
    """
    撤销指定订单。
    params = {
            "symbol": symbol,
        }
        if orderId:
            params["orderId"] = orderId
        elif origClientOrderId:
            params["origClientOrderId"] = origClientOrderId
        else:
            logger.error("撤单失败: 必须提供 orderId 或 origClientOrderId。")
            return None
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    response = client.cancel_order(**params)
    return response.get("status")

def cancel_all_open_orders(symbol):
    """
    撤销某个交易对的所有挂单。
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    try:
        params = {
            "symbol": symbol,
        }
        response = client.cancel_open_orders(**params)
        logger.info(f"撤销所有挂单成功: {response}")
        return response
    except ClientError as error:
        logger.error(
            "撤销所有挂单失败. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
        return None

# ==================== 算法订单API函数 ====================
@log_calls(level='info', log_args=True, log_result=True) # 入参出参日志装饰器
@retry(max_attempts=2, delay=5, exceptions=(ClientError,))  # 重试装饰器
def cancel_algo_order(algoOrderId=None, origClientOrderId=None, **kwargs):
    """
    撤销算法订单（止盈止损单）
    
    Args:
        algoOrderId: 算法订单ID
        origClientOrderId: 原始客户端订单ID（暂未使用）
        **kwargs: 其他参数
        
    Returns:
        撤销响应，失败返回 None
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    
    if not algoOrderId and not origClientOrderId:
        logger.error("撤单失败: 必须提供 algoOrderId 或 origClientOrderId。")
        return None
    
    try:
        response = client.cancel_algo_order(algoOrderId)
        return response
    except ClientError as error:
        logger.error(
            f"撤销算法订单失败. status: {error.status_code}, "
            f"error code: {error.error_code}, error message: {error.error_message}"
        )
        return None

@log_calls(level='info', log_args=True, log_result=True) # 入参出参日志装饰器
def get_open_algo_orders(algoId:int = None, **kwargs):
    """
    查询当前挂起的算法订单（止盈止损单）
    
    Args:
        algoId: 算法订单ID，如果为None则查询所有
        **kwargs: 其他查询参数
        
    Returns:
        算法订单信息，失败返回 None
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    
    response = client.get_open_algo_orders(algoId)
    return response

@log_calls(level='info', log_args=True, log_result=True) # 入参出参日志装饰器
@retry(max_attempts=2, delay=5, exceptions=(ClientError,))  # 重试装饰器
def place_take_algo_order(params):
    """
    下达止盈单
    
    支持两种类型：
    - TAKE_PROFIT: 限价止盈单，需要指定 price 和 timeInForce
    - TAKE_PROFIT_MARKET: 市价止盈单，不需要指定 price
    
    Args:
    params = {
        "algotype": "CONDITIONAL",
        "symbol": symbol,
        "side": side,
        "type": order_type,  "TAKE_PROFIT"/"STOP"/"STOP_MARKET"/"TAKE_PROFIT_MARKET"
        "quantity": quantity,
        "price": trigger_price,
        "timeInForce":"GTC" 
    }
        
    Returns:
        订单响应，包含 algoId 等信息，失败返回 None
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    
    response = client.new_algo_order(**params)
    return response


if __name__ == "__main__":
    import time
    from utils.logger import init_logging, get_logger
    # 导入账户查询模块
    try:
        from trade.binance_futures_account import get_future_orders
    except ImportError:
        try:
            from binance_futures_account import get_future_orders
        except ImportError:
            logger.error("无法导入 binance_futures_account 模块")
            get_future_orders = None
    
    init_logging(level='info', to_console=True, to_file=False, path='act_tradelog', force=True)
    logger = get_logger(__name__)
    logger.info("交易模块初始化成功")
    
    # ========== 示例：ETH/USDT 开空单并设置止盈止损 ==========
    symbol = "ETHUSDT"
    quantity = 0.008
    take_profit_price = 3400  # 止盈价格
    stop_loss_price = 2800    # 止损价格
    
    logger.info(f"开始执行 ETH/USDT 开空单示例，数量: {quantity}")
    
    # 1. 挂卖一开空单（使用对手价，即买一价）
    logger.info("步骤1: 下达开空限价单（挂卖一）")
    params = {
        "symbol": symbol,
        "side": "SELL",  # 卖出开空
        "positionSide": "LONG",  # 空头方向
        "type": "LIMIT",
        "quantity": quantity,
        #"priceMatch": "OPPONENT",  # 挂卖一（对手价，即买一价）
        "price": 3000,
        "timeInForce": "GTC",
    }
    
    order_id =8389766068710283334#place_limit_order(params)
    
    if order_id:
        logger.info(f"开空单下达成功，订单ID: {order_id}")
        
        # 2. 查询委托下达时的订单状态
        logger.info("步骤2: 查询委托下达时的订单状态")
        if get_future_orders:
            order_status = get_future_orders({"symbol": symbol, "orderId": order_id})
            if order_status:
                logger.info(f"订单状态 - 状态: {order_status.get('status')}, "
                          f"已成交数量: {order_status.get('executedQty')}, "
                          f"订单价格: {order_status.get('price')}")
        else:
            logger.warning("无法查询订单状态，binance_futures_account 模块未导入")
        
        # 等待一小段时间，确保订单已处理
        time.sleep(1)
        
        # 3. 设置止盈单
        logger.info("步骤3: 设置止盈单")
        take_profit_result = place_take_profit_order(
            symbol=symbol,
            side="BUY",  # 买入平空
            quantity=quantity,
            price=2200,  # 触发后以3400价格下单
            positionSide="SHORT"  # 空头方向
        )
        if take_profit_result:
            logger.info(f"止盈单设置成功，订单ID: {take_profit_result.get('algoId')}")
        
        # 4. 设置止损单（价格涨到2800时平空）
        logger.info("步骤4: 设置止损单（价格2800）")
        stop_loss_result = place_stop_loss_order(
            symbol=symbol,
            side="BUY",  # 买入平空
            quantity=quantity,
            price=3400, 
            positionSide="SHORT"  # 空头方向
        )
        if stop_loss_result:
            logger.info(f"止损单设置成功，订单ID: {stop_loss_result.get('algoId')}")
        
        # 5. 撤销上面两笔算法单并修改成3650止损，2850止盈
        logger.info("步骤5: 撤销上面两笔算法单并修改成3650止损，2850止盈")
        cancel_algo_order(symbol=symbol, algoOrderId=take_profit_result.get('algoId'))
        cancel_algo_order(symbol=symbol, algoOrderId=stop_loss_result.get('algoId'))
        place_stop_loss_order(symbol=symbol, side="BUY", quantity=quantity, price=3650, stopPrice=3650, positionSide="SHORT")
        place_take_profit_order(symbol=symbol, side="BUY", quantity=quantity, price=2850, stopPrice=2850, positionSide="SHORT")
    else:
        logger.error("开空单下达失败")
    
    logger.info("示例执行完成")
    
    # ========== 其他示例用法 ==========
    # 限价买入
    # params = {
    #         "symbol": "ETHUSDC",
    #         "side": "BUY",
    #         "positionSide":"LONG",  # 确保 positionSide 正确
    #         "type": "LIMIT",
    #         "quantity": 0.02,#2500
    #         #"reduceOnly":True,
    #         #"price": price,
    #         "priceMatch":"QUEUE",
    #         "timeInForce": "GTC",
    #     }
    #    
    # place_limit_order(params)

    # 撤销订单 (需要替换为实际的 orderId 或 origClientOrderId)
    #cancel_order("STXUSDT", orderId=7640104796)

    # 撤销所有挂单
    # cancel_all_open_orders("BTCUSDT")

    # 止盈单
    # place_take_profit_order("BTCUSDT", "SELL", 0.001, 21000, 20500, type="TAKE_PROFIT_MARKET")

    # 止损单
    # place_stop_loss_order("BTCUSDT", "SELL", 0.001, 19000, 19500, type="STOP_MARKET")

    # 平仓 (假设当前持有 BTCUSDT 多头，平仓方向为 SELL)
    # close_position("BTCUSDT", "SELL", 0.001)


