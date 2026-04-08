from cryptography.fernet import Fernet
import sys

def generate_key():
    """
    生成加密密钥
    """
    return Fernet.generate_key()


def encrypt_text(text, key):
    """
    加密文本
    :param text: 待加密的文本
    :param key: 加密密钥
    :return: 加密后的文本
    """
    f = Fernet(key)
    encrypted_text = f.encrypt(text.encode())
    # 加随机3个字节
    random_bytes = os.urandom(3)
    encrypted_text = random_bytes + encrypted_text
    return encrypted_text


def decrypt_text(encrypted_text, key):
    """
    解密文本
    :param encrypted_text: 加密后的文本
    :param key: 加密密钥
    :return: 解密后的文本
    """
    f = Fernet(key)
    decrypted_text = f.decrypt(encrypted_text).decode()
    return decrypted_text


if __name__ == "__main__":
    if len(sys.argv) > 1:
        command = sys.argv[1]
        if command == "generate_key":
            print(generate_key())
        elif command == "encrypt_text":
            text = sys.argv[2]
            key = sys.argv[3]
            print(encrypt_text(text, key))
        elif command == "decrypt_text":
            encrypted_text = eval(sys.argv[2])
            key = sys.argv[3]
            original_order = eval(sys.argv[4])
            print(decrypt_text(encrypted_text, key, original_order))
    else:
        print("请提供命令：generate_key, encrypt_text, decrypt_text")