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
from binance_ppq.um_futures import UMFutures as Client
from binance_ppq.error import ClientError

# 导入工具模块 - 支持单独运行和作为模块导入两种场景
try:
    # 优先尝试绝对导入（作为模块被导入时，从项目根目录导入）
    from utils.decorators import retry
    from utils.logger import get_logger
except ImportError:
    # 如果绝对导入失败，尝试相对导入（单独运行时）
    try:
        from utils.decorators import retry
        from utils.logger import get_logger
    except ImportError:
        raise ImportError("无法导入 utils.decorators 或 utils.logger 模块")

# 导入 prepare_env - 支持单独运行和作为模块导入两种场景
try:
    # 优先尝试从 trade 包导入（作为模块被导入时）
    from trade.prepare_env import get_api_key_read, proxies_env
except ImportError:
    # 如果失败，尝试从当前目录导入（单独运行时）
    try:
        from prepare_env import get_api_key_read, proxies_env
    except ImportError:
        raise ImportError("无法导入 prepare_env 模块，请确保 prepare_env.py 文件存在")

logger = get_logger(__name__)

# 全局配置 - 根据运行环境自动查找配置文件
__config__ = {}
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
                __config__ = json.load(file)
            break
    except (FileNotFoundError, json.JSONDecodeError):
        continue

if not __config__:
    if _is_main_module:
        print(f"交易模块初始化警告: 未找到有效的配置文件。尝试的路径: {config_paths}")
    # 作为模块导入时，不打印错误，让调用方处理

def parase_api_key(api_key):
    # 提取第 45 到第 49 个字母
    sub_string = api_key[44:50]
    reversed_sub_string = sub_string[::-1]
    api_key = api_key[:44] + reversed_sub_string + api_key[50:]
    return api_key

# 初始化 U 本位合约客户端
# 如果配置为空，在调用函数时会报错，这样可以避免模块导入时出错
try:
    api_key, api_secret = get_api_key_read(__config__)
    if api_key and api_secret:
        client = Client(
            parase_api_key(api_key), 
            bytes(parase_api_key(api_secret), 'utf-8'), 
            base_url="https://fapi.binance.com",  
            proxies=proxies_env
        )
    else:
        # 配置为空时，设置为 None，函数调用时会检查
        client = None
        if _is_main_module:
            logger.warning("API密钥为空，客户端未初始化。请检查配置文件。")
except Exception as e:
    client = None
    logger.warning(f"初始化币安客户端失败: {e}。请检查配置文件。")


def get_open_orders(symbol=None):
    """
    查询当前挂单。
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    try:
        params = {}
        if symbol:
            params["symbol"] = symbol
        response = client.get_open_orders(**params)
        logger.info(f"查询挂单成功: {response}")
        return response
    except ClientError as error:
        logger.error(
            "查询挂单失败. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
        return None
    

@retry(max_attempts=2, delay=5, exceptions=(ClientError,))  # 重试装饰器
def get_future_orders(params):
    """
    params = {"symbol": symbol}
        if orderId:
            params["orderId"] = orderId
        if origClientOrderId:
            params["origClientOrderId"] = origClientOrderId

            返回
            Contract status (contractStatus, status):

PENDING_TRADING 
TRADING
PRE_DELIVERING
DELIVERING
DELIVERED
PRE_SETTLE
SETTLING
CLOSE  
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    response = client.query_order(**params)
    logger.info(f"查询订单信息成功: {response}")
    return response
def get_all_orders(symbol=None, startTime=None):
    """
    查询所有订单信息。
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    try:
        params = {}
        if symbol:
            params["symbol"] = symbol
            params["startTime"] = startTime
        response = client.get_all_orders(**params)
        logger.info(f"查询所有订单信息成功: {response}")
        return response
    except ClientError as error:
        logger.error(
            "查询所有订单信息失败. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
        return None

def get_position_info(symbol=None):
    """
    查询持仓信息。
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    try:
        params = {}
        if symbol:
            params["symbol"] = symbol
        response = client.get_position_risk(**params)
        logger.info(f"查询持仓信息成功: {response}")
        return response
    except ClientError as error:
        logger.error(
            "查询持仓信息失败. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
        return None

def get_account_balance():
    """
    查询合约账户余额。
    """
    if client is None:
        logger.error("客户端未初始化，请检查配置文件")
        return None
    try:
        response = client.balance()
        logger.info(f"查询合约账户余额成功: {response}")
        return response
    except ClientError as error:
        logger.error(
            "查询合约账户余额失败. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
        return None

if __name__ == "__main__":
    from utils.logger import init_logging, get_logger
    init_logging(level='info', to_console=True, to_file=False, path='act_tradelog', force=True)
    logger = get_logger(__name__)
    # 示例用法 (需要替换为实际的 symbol, quantity, price 等)
    # 限价买入

    # 查询挂单 1767531337950 是2026-01-05 10:00:00
    get_all_orders("ETHUSDC", startTime=1767541657396)

    # 查询持仓信息
    # get_position_info("BTCUSDT")
